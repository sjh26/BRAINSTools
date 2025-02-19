#ifndef __itkLevelTracingImageFilter_hxx_
#define __itkLevelTracingImageFilter_hxx_

#include "itkLevelTracingImageFilter.h"
#include "itkFloodFilledImageFunctionConditionalIterator.h"
#include "itkProgressReporter.h"
#include "itkConstNeighborhoodIterator.h"
#include "itkConstantBoundaryCondition.h"
#include "itkImageFunction.h"
#include "itkNumericTraits.h"

namespace itk
{

template <typename TInputImage, typename TCoordRep = float>
class LevelTracingImageFunction :
    public ImageFunction<TInputImage,bool,TCoordRep>
{
public:
  /** Standard class type alias. */
  using Self = LevelTracingImageFunction;
  using Superclass = ImageFunction<TInputImage,bool,TCoordRep>;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  /** Run-time type information (and related methods). */
  itkTypeMacro(LevelTracingImageFunction, ImageFunction);

  /** Method for creation through the object factory. */
  itkNewMacro(Self);

  /** InputImageType type alias support. */
  using InputImageType = typename Superclass::InputImageType;

  /** Typedef to describe the type of pixel. */
  using PixelType = typename TInputImage::PixelType;

  /** SizeType of the input image */
  using InputSizeType = typename InputImageType::SizeType;

  /** Dimension underlying input image. */
  static constexpr unsigned int ImageDimension = Superclass::ImageDimension;

  /** Point type alias support. */
  using PointType = typename Superclass::PointType;

  /** Index type alias support. */
  using IndexType = typename Superclass::IndexType;

  /** ContinuousIndex type alias support. */
  using ContinuousIndexType = typename Superclass::ContinuousIndexType;



  /** BinaryThreshold the image at a point position
   *
   * Returns true if the image intensity at the specified point position
   * satisfies the threshold criteria.  The point is assumed to lie within
   * the image buffer.
   *
   * ImageFunction::IsInsideBuffer() can be used to check bounds before
   * calling the method. */
  virtual bool Evaluate( const PointType& point ) const override
    {
      IndexType index;
      this->ConvertPointToNearestIndex( point, index );
      return ( this->EvaluateAtIndex( index ) );
    }

  /** BinaryThreshold the image at a continuous index position
   *
   * Returns true if the image intensity at the specified point position
   * satisfies the threshold criteria.  The point is assumed to lie within
   * the image buffer.
   *
   * ImageFunction::IsInsideBuffer() can be used to check bounds before
   * calling the method. */
  virtual bool EvaluateAtContinuousIndex(
    const ContinuousIndexType & index ) const override
    {
      IndexType nindex;

      this->ConvertContinuousIndexToNearestIndex (index, nindex);
      return this->EvaluateAtIndex(nindex);
    }

  /** BinaryThreshold the image at an index position.
   *
   * Returns true if the image intensity at the specified point position
   * satisfies the threshold criteria.  The point is assumed to lie within
   * the image buffer.
   *
   * ImageFunction::IsInsideBuffer() can be used to check bounds before
   * calling the method. */
  virtual bool EvaluateAtIndex( const IndexType & index ) const override
    {
      // Create an N-d neighborhood kernel, using a zeroflux boundary condition
      ConstNeighborhoodIterator<InputImageType>
        it(m_Radius, this->GetInputImage(), this->GetInputImage()->GetBufferedRegion());


      ConstantBoundaryCondition<InputImageType> cbc;
      cbc.SetConstant(NumericTraits<PixelType>::min());
      it.OverrideBoundaryCondition( &cbc );

      // Set the iterator at the desired location
      it.SetLocation(index);


      PixelType threshold = this->GetThreshold();

      // check the center pixel first
      if (it.GetCenterPixel() < threshold)
        {
        return false;
        }

      // Walk the neighborhood
      bool allInside = true;

      PixelType value;
      const unsigned int size = it.Size();
      for (unsigned int i = 0; i < size; ++i)
        {
        if (i == (size >> 1))
          {
          continue;
          }

        value = it.GetPixel(i);
        if (value < threshold)
          {
          allInside = false;
          break;
          }
        }

      return ( !allInside );
    }

  /** Values greater than or equal to the value are inside. */
  void SetThreshold(PixelType thresh) {m_Threshold = thresh;};
  const PixelType& GetThreshold() const { return m_Threshold; };

protected:
  LevelTracingImageFunction()
    {
      m_Threshold = NumericTraits<PixelType>::min();
      m_Radius.Fill(1);
    };
  ~LevelTracingImageFunction(){};

private:
  ITK_DISALLOW_COPY_AND_ASSIGN(LevelTracingImageFunction);

  PixelType m_Threshold;
  InputSizeType m_Radius;
};

/**
 * Constructor
 */
template <typename TInputImage, typename TOutputImage>
LevelTracingImageFilter<TInputImage, TOutputImage>
::LevelTracingImageFilter()
{
  m_Seed.Fill(0);
  m_MovedSeed = false;
  m_Min = 0;
  m_Max = 0;

  typename ChainCodePathType::Pointer chainCode = ChainCodePathType::New();
  this->ProcessObject::SetNthOutput(1, chainCode.GetPointer());
  chainCode->Initialize();

}

/** Smart Pointer type to a DataObject. */
using DataObjectPointer = DataObject::Pointer;

template <typename TInputImage, typename TOutputImage>
DataObjectPointer
LevelTracingImageFilter<TInputImage, TOutputImage>
::MakeOutput(unsigned int output)
{


  switch (output)
    {
    case 0:
      return static_cast<DataObject*>(TOutputImage::New().GetPointer());
      break;
    case 1:
      return static_cast<DataObject*>(ChainCodePathType::New().GetPointer());
      break;
    default:
      // might as well make an image
      return static_cast<DataObject*>(TOutputImage::New().GetPointer());
      break;
    }
}


/**
 * Standard PrintSelf method.
 */
template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage, TOutputImage>
::PrintSelf(std::ostream& os, Indent indent) const
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Seed point location: " << m_Seed
     << std::endl;
}

template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage,TOutputImage>
::GenerateInputRequestedRegion()
{
  Superclass::GenerateInputRequestedRegion();
  if ( this->GetInput() )
    {
    InputImagePointer image = const_cast< InputImageType * >( this->GetInput() );
    image->SetRequestedRegionToLargestPossibleRegion();
    }
}

template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage,TOutputImage>
::EnlargeOutputRequestedRegion(DataObject *output)
{
  Superclass::EnlargeOutputRequestedRegion(output);
  output->SetRequestedRegionToLargestPossibleRegion();
}

template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage,TOutputImage>
::GenerateData()
{
  // Delegate to either a version specialized for dimension or a
  // general N-dimensional version
  if (this->GetInput()->GetRequestedRegion().IsInsideInWorldSpace( m_Seed ))
    {
    this->Trace( Dispatch<InputImageDimension>() );
    }
  else
    {
    //itkWarningMacro(<< "Seed point " << m_Seed << " is outside the image.");
    }
}

template <typename TInputImage, typename TOutputImage>
int
LevelTracingImageFilter<TInputImage,TOutputImage>
::GetThreshold()
{
  InputImageConstPointer inputImage = this->GetInput();
  return (int) inputImage->GetPixel(m_Seed);
}


template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage,TOutputImage>
::Trace(const Dispatch<2>&)
{

  InputImageConstPointer inputImage = this->GetInput();
  OutputImagePointer outputImage = this->GetOutput();
  ChainCodePathPointer outputPath = this->GetPathOutput();


  typename InputImageType::RegionType region = inputImage->GetBufferedRegion();

  // We may move the seed point to the boundary if it is off by a pixel
  m_MovedSeed = false;

  // Zero the output
  OutputImageRegionType regionOut =  outputImage->GetRequestedRegion();
  outputImage->SetBufferedRegion( regionOut );
  outputImage->Allocate();
  outputImage->FillBuffer ( NumericTraits<OutputImagePixelType>::ZeroValue() );

  outputPath->Initialize();

  //
  InputImagePixelType threshold = inputImage->GetPixel(m_Seed);
  OffsetType offset;

  IndexType pix;
  IndexType seed;
  InputImagePixelType val;

  seed[0] = m_Seed[0];
  seed[1] = m_Seed[1];
  pix.Fill(0);
  pix[0]  = m_Seed[0];
  pix[1]  = m_Seed[1];


  // 8 connected neighbor offsets
  int neighbors[8][2]= {{-1, -1},
                        {-1,  0},
                        {-1,  1},
                        { 0,  1},
                        { 1,  1},
                        { 1,  0},
                        { 1, -1},
                        { 0, -1}};

  int noOfPixels = 0;

  int offsetX;
  int offsetY;
  int zeroIndex = 0;
  m_Max = NumericTraits<InputImagePixelType>::NonpositiveMin();
  m_Min = threshold;

  // We use the standard convention that the foreground is 8
  // connectected and the background is 4 connected.  This implies
  // that for a seed point to be on the boundary of the foreground, it
  // must have a 4 connected neighbor that is the background.
  //
  // Look for the 4 connected neighbor that is background. Save it as
  // the zeroIndex.
  bool found = false;
  IndexType pixTemp;
  pixTemp.Fill(0);
  for(zeroIndex = 1; zeroIndex<8; zeroIndex+=2)
    {
    offsetX = neighbors[zeroIndex][0];
    offsetY = neighbors[zeroIndex][1];
    pixTemp[0] = pix[0] + offsetX;
    pixTemp[1] = pix[1] + offsetY;
    val = inputImage->GetPixel(pixTemp);
    if(val < threshold)
      {
      found = true;
      break;
      }
    }

  // if no 4 connected neighbor is background, look for an 8 connected
  // neighbor that is background. Then adjust the seed point to be one
  // of the 4 connected neighbors to the original seed point that is 4
  // connected to this 8 connected neighbor ofthe original seed.
  if (!found)
    {
    // only need to check the corners since we already checked the 4
    // connected neighbors
    for(zeroIndex = 0; zeroIndex<8; zeroIndex+=2)
      {
      offsetX = neighbors[zeroIndex][0];
      offsetY = neighbors[zeroIndex][1];
      pixTemp[0] = pix[0] + offsetX;
      pixTemp[1] = pix[1] + offsetY;
      val = inputImage->GetPixel(pixTemp);
      if(val < threshold)
        {
        found = true;
        break;
        }
      }

    if (found)
      {
      // found an 8 connected neighbor that is background. Move seed
      // a 4 connected neighbor of the original seed (at this point,
      // said neighbor is implictly foreground) which is also a 4
      // connected neighbor of the background point found.
      int newSeedIndex = (zeroIndex+1)%8;
      seed[0] = pix[0] + neighbors[newSeedIndex][0];
      seed[1] = pix[1] + neighbors[newSeedIndex][1];
      pix[0] = seed[0];
      pix[1] = seed[1];

      // what is the index of background pixel in the new seed point
      // coordinate frame?
      zeroIndex = (newSeedIndex+6)%8;

      // should be change the threshold if we moved the seed? for now,
      // we don't change the threshold because the user probably
      // wanted that intensity value to define the boundary
      m_MovedSeed = true;
      m_Seed[0] = seed[0];
      m_Seed[1] = seed[1];
      }
    else
      {
      // not near a boundary, no boundary to trace
      return;
      }
    }


  // Now we have the seed and the starting neighbor
  outputPath->SetStart(seed);
  outputImage->SetPixel(pix, NumericTraits<OutputImagePixelType>::OneValue());
  do
    {
    for(int s = 0; s<8; s++)
      {
      offsetX = neighbors[(s + zeroIndex + 1)%8][0];
      offsetY = neighbors[(s + zeroIndex + 1)%8][1];
      pixTemp[0] = pix[0] + offsetX;
      pixTemp[1] = pix[1] + offsetY;
      if(region.IsInsideInWorldSpace(pixTemp))
        {
        val = inputImage->GetPixel(pixTemp);
        if (val >= threshold)
          {
          //condition is satisfied, label the output image and output path
          outputImage->SetPixel(pixTemp,
                                NumericTraits<OutputImagePixelType>::OneValue());
          offset[0]=offsetX;
          offset[1]=offsetY;
          outputPath->InsertStep(noOfPixels, offset);
          noOfPixels++;
          if (val > m_Max)
            {
            m_Max = val;
            }
          if (val < m_Min)
            {
            m_Min = val;
            }
          //new seed
          pix[0] = pixTemp[0];
          pix[1] = pixTemp[1];

          // the current pixel is last neighbor to check in next iteration
          zeroIndex = (s + zeroIndex + 1 + 4)%8;
          break;
          }// if more than thresh
        } // if IsInside is true
      else
        {
        // if the pixel is out of the boundary, then go to next neighbor
        continue;
        }
      } //for loop
    }  while ( ! ((pix[0] == seed[0]) && (pix[1] == seed[1])) );  //end while
}

template <typename TInputImage, typename TOutputImage>
void
LevelTracingImageFilter<TInputImage,TOutputImage>
::Trace(const DispatchBase &)
{
  // N-dimensional version
  InputImageConstPointer inputImage = this->GetInput();
  OutputImagePointer outputImage = this->GetOutput();

  InputImagePixelType threshold = inputImage->GetPixel(m_Seed);

  // Zero the output
  OutputImageRegionType region =  outputImage->GetRequestedRegion();
  outputImage->SetBufferedRegion( region );
  outputImage->Allocate();
  outputImage->FillBuffer ( NumericTraits<OutputImagePixelType>::ZeroValue() );

  using FunctionType = LevelTracingImageFunction<InputImageType, double>;
  using IteratorType = FloodFilledImageFunctionConditionalIterator<OutputImageType, FunctionType>;

  typename FunctionType::Pointer function = FunctionType::New();
  function->SetInputImage ( inputImage );
  function->SetThreshold( threshold );

  ProgressReporter progress(this, 0, region.GetNumberOfPixels());

  IteratorType it ( outputImage, function, m_Seed );
  it.GoToBegin();

  while( !it.IsAtEnd())
    {
    it.Set(NumericTraits<OutputImagePixelType>::OneValue());
    ++it;
    progress.CompletedPixel();  // potential exception thrown here
    }
}

} // end namespace itk

#endif
