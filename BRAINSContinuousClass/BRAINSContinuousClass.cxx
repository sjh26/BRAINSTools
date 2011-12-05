#include <iostream>
#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkListSample.h"
#include "itkVector.h"
#include "itkMaskImageFilter.h"
#include "itkBinaryThresholdImageFilter.h"
#include "itkCastImageFilter.h"
#include "itkFixedArray.h"
#include "itkThresholdImageFilter.h"
#include "itkImageDuplicator.h"
#include "itkMaximumRatioDecisionRule.h"
#include "itkVariableSizeMatrix.h"
#include "itkConstrainedValueDifferenceImageFilter.h"
#include "itkImageRegionConstIterator.h"
#include "itkLabelStatisticsImageFilter.h"
#include "linear.h"
#include "omp.h"
#include "BRAINSContinuousClassCLP.h"

#define Malloc(type, n) (type *)malloc( (n) * sizeof(type) )

int main(int argc, char * argv [])
{
  PARSE_ARGS;

  bool violated = false;
  if( inputT1Volume.size() == 0 )
    {
    violated = true; std::cout << "  --inputT1Volume Required! "  << std::endl;
    }
  if( inputT2Volume.size() == 0 )
    {
    violated = true; std::cout << "  --inputT2Volume Required! "  << std::endl;
    }
  if( inputDiscreteVolume.size() == 0 )
    {
    violated = true; std::cout << "  --inputDiscreteVolume Required! "  << std::endl;
    }
  if( outputVolume.size() == 0 )
    {
    violated = true; std::cout << "  --outputVolume Required! "  << std::endl;
    }
  if( violated )
    {
    exit(1);
    }

  typedef float PixelType;
  const unsigned int Dimension = 3;

  typedef itk::Image<PixelType,  Dimension>    ImageType;
  typedef itk::Image<unsigned int, Dimension>  ShortImageType;
  typedef itk::ImageFileReader<ImageType>      ReaderType;
  typedef itk::ImageFileReader<ShortImageType> ShortReaderType;
  typedef itk::ImageFileWriter<ImageType>      WriterType;

  static const ShortImageType::PixelType grayMatterDiscreteValue = 2;
  static const ShortImageType::PixelType basalGrayMatterDiscreteValue = 3;
  static const ShortImageType::PixelType whiteMatterDiscreteValue = 1;
  static const ShortImageType::PixelType csfDiscreteValue = 4;
  static const ShortImageType::PixelType airDiscreteValue = 0;
  static const ShortImageType::PixelType veinousBloodDiscreteValue = 5;
  static const ShortImageType::PixelType allStandInDiscreteValue = 99;

  ReaderType::Pointer      t1Reader = ReaderType::New();
  ReaderType::Pointer      t2Reader = ReaderType::New();
  ShortReaderType::Pointer discreteReader = ShortReaderType::New();

  WriterType::Pointer ouptutWriter = WriterType::New();

  ImageType::Pointer      t1Volume;
  ImageType::Pointer      t2Volume;
  ShortImageType::Pointer discreteVolume;

  try
    {
    t1Reader->SetFileName( inputT1Volume.c_str() );
    t1Reader->Update();
    t1Volume = t1Reader->GetOutput();

    t2Reader->SetFileName( inputT2Volume.c_str() );
    t2Reader->Update();
    t2Volume = t2Reader->GetOutput();

    discreteReader->SetFileName( inputDiscreteVolume.c_str() );
    discreteReader->Update();
    discreteVolume = discreteReader->GetOutput();
    }
  catch( itk::ExceptionObject & exe )
    {
    std::cout << exe << std::endl;
    exit(1);
    }

  // Use the labelStatistics filter to count the number of voxels for each tissue type.
  // Need this for the logistic regression problem later.

  typedef itk::LabelStatisticsImageFilter<ShortImageType, ShortImageType> LabelStatisticsFilterType;
  LabelStatisticsFilterType::Pointer labelStatisticsFilter = LabelStatisticsFilterType::New();
  labelStatisticsFilter->SetInput(discreteVolume);
  labelStatisticsFilter->SetLabelInput(discreteVolume);
  labelStatisticsFilter->Update();
  const unsigned int grayMatterSampleCount = labelStatisticsFilter->GetCount(grayMatterDiscreteValue)
    + labelStatisticsFilter->GetCount(basalGrayMatterDiscreteValue);
  const unsigned int whiteMatterSampleCount = labelStatisticsFilter->GetCount(whiteMatterDiscreteValue);
  const unsigned int csfSampleCount = labelStatisticsFilter->GetCount(csfDiscreteValue);
  const unsigned int veinousBloodSampleCount = labelStatisticsFilter->GetCount(veinousBloodDiscreteValue);
  const unsigned int airSampleCount = labelStatisticsFilter->GetCount(airDiscreteValue);

  // Create training set for each tissue type.
  // The training set is all voxels labeled that class in the discrete classified image.
  // For each set of training data train a logistic classifier.
  struct parameter logisticRegressionParameter;

  logisticRegressionParameter.solver_type = L1R_LR;
  logisticRegressionParameter.C = 1;
  logisticRegressionParameter.eps = 0.01;
  logisticRegressionParameter.nr_weight = 0;
  logisticRegressionParameter.weight_label = NULL;
  logisticRegressionParameter.weight = NULL;

  struct problem logisticRegressionProblemWhiteVsCSF;
  struct problem logisticRegressionProblemWhiteVsGray;
  struct problem logisticRegressionProblemGrayVsCSF;
  struct problem logisticRegressionProblemAirVsAll;
  struct problem logisticRegressionProblemVeinousBloodVsAll;

  const unsigned int bias = 1;
  const unsigned int featuresCount = 2; // T1 and T2, excludes bias term

  // air vs all
  logisticRegressionProblemAirVsAll.bias = bias;
  logisticRegressionProblemAirVsAll.n = bias + featuresCount; // Number of features with bias term
  logisticRegressionProblemAirVsAll.l = veinousBloodSampleCount + grayMatterSampleCount + whiteMatterSampleCount;
  logisticRegressionProblemAirVsAll.l += airSampleCount + csfSampleCount; // Number of training samples
  logisticRegressionProblemAirVsAll.y = Malloc(int, logisticRegressionProblemAirVsAll.l);
  logisticRegressionProblemAirVsAll.x = Malloc(struct feature_node *, logisticRegressionProblemAirVsAll.l);
  struct feature_node *airVsAllFeatureNodes;
  airVsAllFeatureNodes = Malloc(struct feature_node, (featuresCount + bias) * logisticRegressionProblemAirVsAll.l);

  // Veinous blood vs all
  logisticRegressionProblemVeinousBloodVsAll.bias = bias;
  logisticRegressionProblemVeinousBloodVsAll.n = bias + featuresCount;                // Number of features with bias
                                                                                      // term
  logisticRegressionProblemVeinousBloodVsAll.l = logisticRegressionProblemAirVsAll.l; // Number of training samples
  logisticRegressionProblemVeinousBloodVsAll.y = Malloc(int, logisticRegressionProblemVeinousBloodVsAll.l);
  logisticRegressionProblemVeinousBloodVsAll.x = Malloc(struct feature_node *,
                                                        logisticRegressionProblemVeinousBloodVsAll.l);
  struct feature_node *veinousBloodVsAllFeatureNodes;
  veinousBloodVsAllFeatureNodes = Malloc(struct feature_node,
                                         (featuresCount + bias) * logisticRegressionProblemVeinousBloodVsAll.l);

  // White Vs CSF
  logisticRegressionProblemWhiteVsCSF.bias = bias;
  logisticRegressionProblemWhiteVsCSF.n = bias + featuresCount;                    // Number of features with bias term
  logisticRegressionProblemWhiteVsCSF.l = whiteMatterSampleCount + csfSampleCount; // Number of training samples
  logisticRegressionProblemWhiteVsCSF.y = Malloc(int, logisticRegressionProblemWhiteVsCSF.l);
  logisticRegressionProblemWhiteVsCSF.x = Malloc(struct feature_node *, logisticRegressionProblemWhiteVsCSF.l);
  struct feature_node *whiteVsCSFFeatureNodes;
  whiteVsCSFFeatureNodes = Malloc(struct feature_node, (featuresCount + bias) * logisticRegressionProblemWhiteVsCSF.l);

  // White Vs GRAY
  logisticRegressionProblemWhiteVsGray.bias = bias;
  logisticRegressionProblemWhiteVsGray.n = bias + featuresCount;                           // Number of features with
                                                                                           // bias term
  logisticRegressionProblemWhiteVsGray.l = whiteMatterSampleCount + grayMatterSampleCount; // Number of training samples
  logisticRegressionProblemWhiteVsGray.y = Malloc(int, logisticRegressionProblemWhiteVsGray.l);
  logisticRegressionProblemWhiteVsGray.x = Malloc(struct feature_node *, logisticRegressionProblemWhiteVsGray.l);
  struct feature_node *whiteVsGrayFeatureNodes;
  whiteVsGrayFeatureNodes =
    Malloc(struct feature_node, (featuresCount + bias) * logisticRegressionProblemWhiteVsGray.l);

  // CSF Vs Gray
  logisticRegressionProblemGrayVsCSF.bias = bias;
  logisticRegressionProblemGrayVsCSF.n = bias + featuresCount;                   // Number of features with bias term
  logisticRegressionProblemGrayVsCSF.l = csfSampleCount + grayMatterSampleCount; // Number of training samples
  logisticRegressionProblemGrayVsCSF.y = Malloc(int, logisticRegressionProblemGrayVsCSF.l);
  logisticRegressionProblemGrayVsCSF.x = Malloc(struct feature_node *, logisticRegressionProblemGrayVsCSF.l);
  struct feature_node *GrayVsCSFFeatureNodes;
  GrayVsCSFFeatureNodes = Malloc(struct feature_node, (featuresCount + bias) * logisticRegressionProblemGrayVsCSF.l);

  unsigned int whiteVsGraySampleCount = 0;
  unsigned int csfVsGraySampleCount = 0;
  unsigned int whiteVsCSFSampleCount = 0;
  unsigned int airVsAllSampleCount = 0;
  unsigned int veinousBloodVsAllSampleCount = 0;

  typedef itk::ImageRegionConstIterator<ImageType> ImageRegionConstIteratorType;
  ImageRegionConstIteratorType imgItr( t1Volume, t1Volume->GetRequestedRegion() );
  for( imgItr.GoToBegin(); !imgItr.IsAtEnd(); ++imgItr )
    {
    const ImageType::IndexType      idx = imgItr.GetIndex();
    const ImageType::PixelType      t1PixelValue = t1Volume->GetPixel(idx);
    const ImageType::PixelType      t2PixelValue = t2Volume->GetPixel(idx);
    const ShortImageType::PixelType discretePixelValue = discreteVolume->GetPixel(idx);

    if( discretePixelValue == grayMatterDiscreteValue || discretePixelValue == basalGrayMatterDiscreteValue )
      {
      logisticRegressionProblemWhiteVsGray.x[whiteVsGraySampleCount] =
        &whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)];
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)].index = 1;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)].value = t1PixelValue;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 1].index = 2;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemWhiteVsGray.y[whiteVsGraySampleCount] = grayMatterDiscreteValue;

      logisticRegressionProblemGrayVsCSF.x[csfVsGraySampleCount] =
        &GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)];
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)].index = 1;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)].value = t1PixelValue;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 1].index = 2;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemGrayVsCSF.y[csfVsGraySampleCount] = grayMatterDiscreteValue;

      logisticRegressionProblemAirVsAll.x[airVsAllSampleCount] =
        &airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)];
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].index = 1;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemAirVsAll.y[airVsAllSampleCount] = allStandInDiscreteValue;

      logisticRegressionProblemVeinousBloodVsAll.x[veinousBloodVsAllSampleCount] =
        &veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)];
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].index = 1;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemVeinousBloodVsAll.y[veinousBloodVsAllSampleCount] = allStandInDiscreteValue;

      veinousBloodVsAllSampleCount++;
      airVsAllSampleCount++;
      whiteVsGraySampleCount++;
      csfVsGraySampleCount++;
      }
    else if( discretePixelValue == whiteMatterDiscreteValue )
      {
      logisticRegressionProblemWhiteVsGray.x[whiteVsGraySampleCount] =
        &whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)];
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)].index = 1;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias)].value = t1PixelValue;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 1].index = 2;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      whiteVsGrayFeatureNodes[whiteVsGraySampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemWhiteVsGray.y[whiteVsGraySampleCount] = whiteMatterDiscreteValue;

      logisticRegressionProblemWhiteVsCSF.x[whiteVsCSFSampleCount] =
        &whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)];
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)].index = 1;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)].value = t1PixelValue;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 1].index = 2;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemWhiteVsCSF.y[whiteVsCSFSampleCount] = whiteMatterDiscreteValue;

      logisticRegressionProblemAirVsAll.x[airVsAllSampleCount] =
        &airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)];
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].index = 1;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemAirVsAll.y[airVsAllSampleCount] = allStandInDiscreteValue;

      logisticRegressionProblemVeinousBloodVsAll.x[veinousBloodVsAllSampleCount] =
        &veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)];
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].index = 1;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemVeinousBloodVsAll.y[veinousBloodVsAllSampleCount] = allStandInDiscreteValue;

      veinousBloodVsAllSampleCount++;
      airVsAllSampleCount++;
      whiteVsGraySampleCount++;
      whiteVsCSFSampleCount++;
      }
    else if( discretePixelValue == csfDiscreteValue )
      {
      logisticRegressionProblemGrayVsCSF.x[csfVsGraySampleCount] =
        &GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)];
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)].index = 1;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias)].value = t1PixelValue;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 1].index = 2;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      GrayVsCSFFeatureNodes[csfVsGraySampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemGrayVsCSF.y[csfVsGraySampleCount] = csfDiscreteValue;

      logisticRegressionProblemWhiteVsCSF.x[whiteVsCSFSampleCount] =
        &whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)];
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)].index = 1;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias)].value = t1PixelValue;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 1].index = 2;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      whiteVsCSFFeatureNodes[whiteVsCSFSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemWhiteVsCSF.y[whiteVsCSFSampleCount] = csfDiscreteValue;

      logisticRegressionProblemAirVsAll.x[airVsAllSampleCount] =
        &airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)];
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].index = 1;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemAirVsAll.y[airVsAllSampleCount] = allStandInDiscreteValue;

      logisticRegressionProblemVeinousBloodVsAll.x[veinousBloodVsAllSampleCount] =
        &veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)];
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].index = 1;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemVeinousBloodVsAll.y[veinousBloodVsAllSampleCount] = allStandInDiscreteValue;

      veinousBloodVsAllSampleCount++;
      airVsAllSampleCount++;
      csfVsGraySampleCount++;
      whiteVsCSFSampleCount++;
      }
    else if( discretePixelValue == airDiscreteValue )
      {
      logisticRegressionProblemAirVsAll.x[airVsAllSampleCount] =
        &airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)];
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].index = 1;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemAirVsAll.y[airVsAllSampleCount] = airDiscreteValue;

      logisticRegressionProblemVeinousBloodVsAll.x[veinousBloodVsAllSampleCount] =
        &veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)];
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].index = 1;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemVeinousBloodVsAll.y[veinousBloodVsAllSampleCount] = allStandInDiscreteValue;

      veinousBloodVsAllSampleCount++;
      airVsAllSampleCount++;
      }
    else if( discretePixelValue == veinousBloodDiscreteValue )
      {
      logisticRegressionProblemVeinousBloodVsAll.x[veinousBloodVsAllSampleCount] =
        &veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)];
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].index = 1;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      veinousBloodVsAllFeatureNodes[veinousBloodVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemVeinousBloodVsAll.y[veinousBloodVsAllSampleCount] = veinousBloodDiscreteValue;

      logisticRegressionProblemAirVsAll.x[airVsAllSampleCount] =
        &airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)];
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].index = 1;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias)].value = t1PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].index = 2;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 1].value = t2PixelValue;
      airVsAllFeatureNodes[airVsAllSampleCount * (featuresCount + bias) + 2].index = -1;
      logisticRegressionProblemAirVsAll.y[airVsAllSampleCount] = allStandInDiscreteValue;

      veinousBloodVsAllSampleCount++;
      airVsAllSampleCount++;
      }
    }

  struct model* logisticRegressionModelWhiteVsCSF;
  struct model* logisticRegressionModelGrayVsCSF;
  struct model* logisticRegressionModelWhiteVsGray;
  struct model* logisticRegressionModelVeinousBloodVsAll;
  struct model* logisticRegressionModelAirVsAll;

  logisticRegressionModelWhiteVsCSF = train(&logisticRegressionProblemWhiteVsCSF, &logisticRegressionParameter);
  logisticRegressionModelGrayVsCSF = train(&logisticRegressionProblemGrayVsCSF, &logisticRegressionParameter);
  logisticRegressionModelWhiteVsGray = train(&logisticRegressionProblemWhiteVsGray, &logisticRegressionParameter);
  logisticRegressionModelVeinousBloodVsAll = train(&logisticRegressionProblemVeinousBloodVsAll,
                                                   &logisticRegressionParameter);
  logisticRegressionModelAirVsAll = train(&logisticRegressionProblemAirVsAll, &logisticRegressionParameter);

  typedef itk::ImageDuplicator<ImageType> ImageDuplicatorType;
  ImageDuplicatorType::Pointer t1DuplicateImageFilter = ImageDuplicatorType::New();
  t1DuplicateImageFilter->SetInputImage(t1Reader->GetOutput() );
  t1DuplicateImageFilter->Update();
  ImageType::Pointer outputImage = t1DuplicateImageFilter->GetOutput();

  struct feature_node *sampleToPredict =
    (struct feature_node *) malloc( (featuresCount + bias) * sizeof(struct feature_node) );
  sampleToPredict[0].index = 1;
  sampleToPredict[1].index = 2;
  sampleToPredict[2].index = -1;

  double predictedProbabilityEstimatesWhiteVsGray[2];
  double predictedProbabilityEstimatesWhiteVsCSF[2];
  double predictedProbabilityEstimatesGrayVsCSF[2];
  double predictedProbabilityEstimatesVeinousBloodVsAll[2];
  double predictedProbabilityEstimatesAirVsAll[2];
  for( imgItr.GoToBegin(); !imgItr.IsAtEnd(); ++imgItr )
    {
    const ImageType::IndexType idx = imgItr.GetIndex();
    const ImageType::PixelType t1PixelValue = t1Reader->GetOutput()->GetPixel(idx);
    const ImageType::PixelType t2PixelValue = t2Reader->GetOutput()->GetPixel(idx);
    sampleToPredict[0].value = t1PixelValue;
    sampleToPredict[1].value = t2PixelValue;

    unsigned int predictionLabel = predict_probability(logisticRegressionModelWhiteVsCSF,
                                                       sampleToPredict,
                                                       predictedProbabilityEstimatesWhiteVsCSF);
    predictionLabel = predict_probability(logisticRegressionModelWhiteVsGray,
                                          sampleToPredict,
                                          predictedProbabilityEstimatesWhiteVsGray);
    predictionLabel = predict_probability(logisticRegressionModelGrayVsCSF,
                                          sampleToPredict,
                                          predictedProbabilityEstimatesGrayVsCSF);
    predictionLabel = predict_probability(logisticRegressionModelVeinousBloodVsAll,
                                          sampleToPredict,
                                          predictedProbabilityEstimatesVeinousBloodVsAll);
    predictionLabel = predict_probability(logisticRegressionModelAirVsAll,
                                          sampleToPredict,
                                          predictedProbabilityEstimatesAirVsAll);

    ImageType::PixelType outputAirPixelValue = 0;
    ImageType::PixelType outputOtherPixelValue = 9;
    ImageType::PixelType predictedOutputPixelValue = outputAirPixelValue;

    // TODO: Add classifiers for air and blood.

    // The ordering of the predicted probabilities is based on the order of the labels.  The
    // zero index probability corresponds to the probability that the sample is the label with the
    // lowest integer value.  i.e. if Gray is label 1 and White is label 2, the zero index probability
    // would correspond to Gray.
//    if( predictedProbabilityEstimatesAirVsAll[1] < predictedProbabilityEstimatesAirVsAll[0] )
//      {
//      predictedOutputPixelValue = outputAirPixelValue;
//      }
    if( predictedProbabilityEstimatesVeinousBloodVsAll[0] < predictedProbabilityEstimatesVeinousBloodVsAll[1] )
      {
      predictedOutputPixelValue = outputOtherPixelValue;
      }
    else if( predictedProbabilityEstimatesWhiteVsCSF[0] > predictedProbabilityEstimatesWhiteVsCSF[1] )
      {
      if( predictedProbabilityEstimatesWhiteVsGray[0] < predictedProbabilityEstimatesWhiteVsGray[1] )
        {
        if( predictedProbabilityEstimatesGrayVsCSF[0] < predictedProbabilityEstimatesGrayVsCSF[1] )
          {
          // Output voxel is other
          predictedOutputPixelValue = outputOtherPixelValue;
          }
        else
          {
          // white vs gray
          predictedOutputPixelValue =
            static_cast<ImageType::PixelType>(130 + (120 * predictedProbabilityEstimatesWhiteVsGray[0]) );
          }
        }
      else
        {
        // white vs gray
        predictedOutputPixelValue =
          static_cast<ImageType::PixelType>(130 + 120 * predictedProbabilityEstimatesWhiteVsGray[0]);
        }
      }
    else
      {
      if( predictedProbabilityEstimatesGrayVsCSF[0] > predictedProbabilityEstimatesGrayVsCSF[1] )
        {
        if( predictedProbabilityEstimatesWhiteVsGray[0] > predictedProbabilityEstimatesWhiteVsGray[1] )
          {
          // Output voxel is other
          predictedOutputPixelValue = outputOtherPixelValue;
          }
        else
          {
          // CSF Vs Gray
          predictedOutputPixelValue =
            static_cast<ImageType::PixelType>(10 + 120 * predictedProbabilityEstimatesGrayVsCSF[0]);
          }
        }
      else
        {
        // CSF Vs Gray
        predictedOutputPixelValue =
          static_cast<ImageType::PixelType>(10 + 120 * predictedProbabilityEstimatesGrayVsCSF[0]);
        }
      }

    outputImage->SetPixel(idx, predictedOutputPixelValue);
    }

  destroy_param(&logisticRegressionParameter);
  free(logisticRegressionProblemGrayVsCSF.y);
  free(logisticRegressionProblemGrayVsCSF.x);
  free(GrayVsCSFFeatureNodes);
  free(logisticRegressionProblemWhiteVsGray.y);
  free(logisticRegressionProblemWhiteVsGray.x);
  free(whiteVsGrayFeatureNodes);
  free(logisticRegressionProblemWhiteVsCSF.y);
  free(logisticRegressionProblemWhiteVsCSF.x);
  free(whiteVsCSFFeatureNodes);
  free(logisticRegressionProblemAirVsAll.y);
  free(logisticRegressionProblemAirVsAll.x);
  free(airVsAllFeatureNodes);
  free(logisticRegressionProblemVeinousBloodVsAll.y);
  free(logisticRegressionProblemVeinousBloodVsAll.x);
  free(veinousBloodVsAllFeatureNodes);
  free(sampleToPredict);
  free(logisticRegressionModelWhiteVsCSF->w);
  free(logisticRegressionModelWhiteVsCSF->label);
  free(logisticRegressionModelWhiteVsCSF);
  free(logisticRegressionModelWhiteVsGray->w);
  free(logisticRegressionModelWhiteVsGray->label);
  free(logisticRegressionModelWhiteVsGray);
  free(logisticRegressionModelGrayVsCSF->w);
  free(logisticRegressionModelGrayVsCSF->label);
  free(logisticRegressionModelGrayVsCSF);
  free(logisticRegressionModelAirVsAll->w);
  free(logisticRegressionModelAirVsAll->label);
  free(logisticRegressionModelAirVsAll);
  free(logisticRegressionModelVeinousBloodVsAll->w);
  free(logisticRegressionModelVeinousBloodVsAll->label);
  free(logisticRegressionModelVeinousBloodVsAll);

  ouptutWriter->SetInput(outputImage);
  ouptutWriter->SetFileName(outputVolume);
  ouptutWriter->Modified();
  ouptutWriter->Update();

  return EXIT_SUCCESS;
}
