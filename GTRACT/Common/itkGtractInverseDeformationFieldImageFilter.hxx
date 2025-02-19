/*=========================================================================
 *
 *  Copyright SINAPSE: Scalable Informatics for Neuroscience, Processing and Software Engineering
 *            The University of Iowa
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __itkGtractInverseDisplacementFieldImageFilter.hxx
#define __itkGtractInverseDisplacementFieldImageFilter .hxx

#include "itkGtractInverseDisplacementFieldImageFilter.h"
#include "itkObjectFactory.h"
#include "itkProgressReporter.h"
#include "itkThinPlateSplineKernelTransform.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkResampleImageFilter.h"

namespace itk
{
/**
 * Initialize new instance
 */
template <typename TInputImage, typename TOutputImage>
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::GtractInverseDisplacementFieldImageFilter()
{
  m_OutputSpacing.Fill(1.0);
  m_OutputOrigin.Fill(0.0);
  for( unsigned int i = 0; i < ImageDimension; i++ )
    {
    m_Size[i] = 0;
    }

  m_OutputDirection.SetIdentity();

  using DefaultTransformType = ThinPlateSplineKernelTransform<
      double,
      Self::ImageDimension >;

  m_KernelTransform = DefaultTransformType::New();

  m_SubsamplingFactor = 16;
}

/**
 * Print out a description of self
 *
 * \todo Add details about this class
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Size:              " << m_Size << std::endl;
  os << indent << "OutputSpacing:     " << m_OutputSpacing << std::endl;
  os << indent << "OutputOrigin:      " << m_OutputOrigin << std::endl;
  os << indent << "KernelTransform:   " << m_KernelTransform.GetPointer() << std::endl;
  os << indent << "SubsamplingFactor: " << m_SubsamplingFactor << std::endl;

  return;
}

/**
 * Set the output image spacing.
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::SetOutputSpacing(const double *spacing)
{
  SpacingType s(spacing);

  this->SetOutputSpacing( s );
}

/**
 * Set the output image origin.
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::SetOutputOrigin(const double *origin)
{
  OriginPointType p(origin);

  this->SetOutputOrigin( p );
}

/**
 * Sub-sample the input deformation field and prepare the KernelBase
 * BSpline
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::PrepareKernelBaseSpline()
{
  using LandmarkContainer = typename KernelTransformType::PointsContainer;
  using LandmarkContainerPointer = typename LandmarkContainer::Pointer;

  // Source contains points with physical coordinates of the
  // destination displacement fields (the inverse field)
  LandmarkContainerPointer source = LandmarkContainer::New();

  // Target contains vectors (stored as points) indicating
  // displacement in the inverse direction.
  LandmarkContainerPointer target = LandmarkContainer::New();

  using ResamplerType = itk::ResampleImageFilter<
      InputImageType,
      InputImageType>;

  typename ResamplerType::Pointer resampler = ResamplerType::New();

  const InputImageType *inputImage = this->GetInput();

  resampler->SetInput( inputImage );
  resampler->SetOutputOrigin( inputImage->GetOrigin() );

  typename InputImageType::SpacingType spacing = inputImage->GetSpacing();

  using InputRegionType = typename InputImageType::RegionType;
  using InputSizeType = typename InputImageType::SizeType;
  using InputIndexType = typename InputImageType::IndexType;

  InputRegionType region;

  region = inputImage->GetLargestPossibleRegion();

  InputSizeType size = region.GetSize();
  for( unsigned int i = 0; i < ImageDimension; i++ )
    {
    size[i]    =  static_cast<typename InputSizeType::SizeValueType>( size[i] / m_SubsamplingFactor );
    spacing[i] *= m_SubsamplingFactor;
    }

  InputRegionType subsampledRegion;
  subsampledRegion.SetSize( size );
  subsampledRegion.SetIndex( region.GetIndex() );

  resampler->SetSize( size );
  resampler->SetOutputStartIndex( subsampledRegion.GetIndex() );
  resampler->SetOutputSpacing( spacing );

  resampler->Update();

  // allocate a landmark pair for each
  // pixel in the subsampled field
  const unsigned long numberOfLandmarks = subsampledRegion.GetNumberOfPixels();
  source->Reserve( numberOfLandmarks );
  target->Reserve( numberOfLandmarks );

  const InputImageType *sampledInput = resampler->GetOutput();

  using IteratorType = ImageRegionConstIteratorWithIndex<InputImageType>;

  unsigned int landmarkId = 0;

  IteratorType ot( sampledInput, subsampledRegion );

  ot.GoToBegin();

  OutputPixelType               value;
  Point<double, ImageDimension> sourcePoint;
  Point<double, ImageDimension> targetPoint;

  while( !ot.IsAtEnd() )
    {
    value = ot.Get();
    sampledInput->TransformIndexToPhysicalPoint( ot.GetIndex(), sourcePoint );

    source->InsertElement( landmarkId,  sourcePoint );
    for( unsigned int i = 0; i < ImageDimension; i++ )
      {
      targetPoint[i] = -value[i];
      }
    target->InsertElement( landmarkId, targetPoint );  // revert direction of
                                                       // displacement

    ++landmarkId;
    ++ot;
    }

  itkDebugMacro( << "Number of Landmarks created = " <<  numberOfLandmarks );

  m_KernelTransform->GetTargetLandmarks()->SetPoints( target );
  m_KernelTransform->GetSourceLandmarks()->SetPoints( source );

  itkDebugMacro( << "Before ComputeWMatrix() ");

  m_KernelTransform->ComputeWMatrix();

  itkDebugMacro( << "After ComputeWMatrix() ");
}

/**
 * GenerateData
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::GenerateData()
{
  // First subsample the input deformation field in order to create
  // the KernelBased spline.
  this->PrepareKernelBaseSpline();

  itkDebugMacro(<< "Actually executing");

  // Get the output pointers
  OutputImageType *outputPtr = this->GetOutput();

  outputPtr->SetBufferedRegion( outputPtr->GetRequestedRegion() );
  outputPtr->Allocate();

  // Create an iterator that will walk the output region for this thread.
  using OutputIterator = ImageRegionIteratorWithIndex<
      TOutputImage>;

  OutputImageRegionType region = outputPtr->GetRequestedRegion();

  OutputIterator outIt( outputPtr, region );

  // Define a few indices that will be used to translate from an input pixel
  // to an output pixel
  IndexType outputIndex;         // Index to current output pixel

  using InputPointType = typename KernelTransformType::InputPointType;
  using OutputPointType = typename KernelTransformType::OutputPointType;

  InputPointType outputPoint;    // Coordinates of current output pixel

  // Support for progress methods/callbacks
  ProgressReporter progress(this, 0, region.GetNumberOfPixels(), 10);

  outIt.GoToBegin();

  // Walk the output region
  while( !outIt.IsAtEnd() )
    {
    // Determine the index of the current output pixel
    outputIndex = outIt.GetIndex();
    outputPtr->TransformIndexToPhysicalPoint( outputIndex, outputPoint );

    // Compute corresponding inverse displacement vector
    OutputPointType interpolation
      = m_KernelTransform->TransformPoint( outputPoint );

    OutputPixelType inverseDisplacement;
    for( unsigned int i = 0; i < ImageDimension; i++ )
      {
      inverseDisplacement[i] = interpolation[i];
      }

    outIt.Set( inverseDisplacement ); // set inverse displacement.
    ++outIt;
    progress.CompletedPixel();
    }

  return;
}

/**
 * Inform pipeline of necessary input image region
 *
 * Determining the actual input region is non-trivial, especially
 * when we cannot assume anything about the transform being used.
 * So we do the easy thing and request the entire input image.
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::GenerateInputRequestedRegion()
{
  // call the superclass's implementation of this method
  Superclass::GenerateInputRequestedRegion();

  if( !this->GetInput() )
    {
    return;
    }

  // get pointers to the input and output
  InputImagePointer inputPtr
    = const_cast<InputImageType *>( this->GetInput() );

  // Request the entire input image
  InputImageRegionType inputRegion;
  inputRegion = inputPtr->GetLargestPossibleRegion();
  inputPtr->SetRequestedRegion(inputRegion);

  return;
}

/**
 * Inform pipeline of required output region
 */
template <typename TInputImage, typename TOutputImage>
void
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::GenerateOutputInformation()
{
  // call the superclass' implementation of this method
  Superclass::GenerateOutputInformation();

  // get pointers to the input and output
  OutputImagePointer outputPtr = this->GetOutput();

  if( !outputPtr )
    {
    return;
    }

  // Set the size of the output region
  typename TOutputImage::RegionType outputLargestPossibleRegion;
  outputLargestPossibleRegion.SetSize( m_Size );
  outputPtr->SetLargestPossibleRegion( outputLargestPossibleRegion );

  // Set spacing, origin, and direction
  outputPtr->SetSpacing( m_OutputSpacing );
  outputPtr->SetOrigin( m_OutputOrigin );
  outputPtr->SetDirection( m_OutputDirection );

  return;
}

/**
 * Verify if any of the components has been modified.
 */
template <typename TInputImage, typename TOutputImage>
unsigned long
GtractInverseDisplacementFieldImageFilter<TInputImage, TOutputImage>
::GetMTime( void ) const
{
  unsigned long latestTime = Object::GetMTime();

  if( m_KernelTransform )
    {
    if( latestTime < m_KernelTransform->GetMTime() )
      {
      latestTime = m_KernelTransform->GetMTime();
      }
    }

  return latestTime;
}
} // end namespace itk

#endif
