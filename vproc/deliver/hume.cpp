/*
    (JW): Face Rect

    This file is mainly a concatenation of code from: https://github.com/trishume/eyeLike

    Some modifcations include changing the static consts to enums,
    as clang appeared to give warnings to may partners about some of them being unused.

    Everything here is internal, except I have added one extern function
    at the end of the file "detectPupilHume" that takes a cv mat of
    the whole image (not the face, but I don't think matters) and the ROI for an eye.
    It passes a gray scale version of that mat to Hume's "findEyeCenter",
    and my function converts the return value relative to the whole image,
    not the passed eye ROI.
*/

#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

//#include <mgl2/mgl.h>

//#include <iostream>
#include <queue>
#include <stdio.h>

//#include "constants.h"
#ifndef CONSTANTS_H
#define CONSTANTS_H

#if 0
// Debugging
const bool kPlotVectorField = false;

// Size constants
const int kEyePercentTop = 25;
const int kEyePercentSide = 13;
const int kEyePercentHeight = 30;
const int kEyePercentWidth = 35;

// Preprocessing
const bool kSmoothFaceImage = false;
const float kSmoothFaceFactor = 0.005;

// Algorithm Parameters
const int kFastEyeWidth = 50;//higher is more accurate, but slower
const int kWeightBlurSize = 5;
const bool kEnableWeight = true;
const float kWeightDivisor = 1.0;
const double kGradientThreshold = 50.0;

// Postprocessing
const bool kEnablePostProcess = true;
const float kPostProcessThreshold = 0.97;

// Eye Corner
const bool kEnableEyeCorner = false;
#endif

enum : int
{
    // Size constants
    kEyePercentTop = 25,
    kEyePercentSide = 13,
    kEyePercentHeight = 30,
    kEyePercentWidth = 35,

    kFastEyeWidth = 50,//higher is more accurate, but slower
    kWeightBlurSize = 5
};

enum : bool
{
    kEnableWeight = true,
    kEnablePostProcess = true,
    kEnableEyeCorner = false,
    kPlotVectorField = false,
};


const float kPostProcessThreshold = 0.97f;
const float kSmoothFaceFactor = 0.005f;
const float kWeightDivisor = 1.0f;

const double kGradientThreshold = 50.0;

#endif
//#include "helpers.h"
#ifndef HELPERS_H
#define HELPERS_H

//static bool rectInImage(cv::Rect rect, cv::Mat image);
static bool inMat(cv::Point p,int rows,int cols);
static cv::Mat matrixMagnitude(const cv::Mat &matX, const cv::Mat &matY);
static double computeDynamicThreshold(const cv::Mat &mat, double stdDevFactor);

#endif

// Pre-declarations
static
cv::Mat floodKillEdges(cv::Mat &mat);

/*
template<typename T> mglData *matToData(const cv::Mat &mat) {
  mglData *data = new mglData(mat.cols,mat.rows);
  for (int y = 0; y < mat.rows; ++y) {
    const T *Mr = mat.ptr<T>(y);
    for (int x = 0; x < mat.cols; ++x) {
      data->Put(((mreal)Mr[x]),x,y);
    }
  }
  return data;
}
void plotVecField(const cv::Mat &gradientX, const cv::Mat &gradientY, const cv::Mat &img) {
  mglData *xData = matToData<double>(gradientX);
  mglData *yData = matToData<double>(gradientY);
  mglData *imgData = matToData<float>(img);

  mglGraph gr(0,gradientX.cols * 20, gradientY.rows * 20);
  gr.Vect(*xData, *yData);
  gr.Mesh(*imgData);
  gr.WriteFrame("vecField.png");

  delete xData;
  delete yData;
  delete imgData;
}*/

//#pragma mark Helpers

static
cv::Point unscalePoint(cv::Point p, cv::Rect origSize) {
  float ratio = (((float)kFastEyeWidth)/origSize.width);
  int x = round(p.x / ratio);
  int y = round(p.y / ratio);
  return cv::Point(x,y);
}

static
void scaleToFastSize(const cv::Mat &src,cv::Mat &dst) {
  cv::resize(src, dst, cv::Size(kFastEyeWidth,(((float)kFastEyeWidth)/src.cols) * src.rows));
}

static
cv::Mat computeMatXGradient(const cv::Mat &mat) {
  cv::Mat out(mat.rows,mat.cols,CV_64F);

  for (int y = 0; y < mat.rows; ++y) {
    const uchar *Mr = mat.ptr<uchar>(y);
    double *Or = out.ptr<double>(y);

    Or[0] = Mr[1] - Mr[0];
    for (int x = 1; x < mat.cols - 1; ++x) {
      Or[x] = (Mr[x+1] - Mr[x-1])/2.0;
    }
    Or[mat.cols-1] = Mr[mat.cols-1] - Mr[mat.cols-2];
  }

  return out;
}

//#pragma mark Main Algorithm

static
void testPossibleCentersFormula(int x, int y, const cv::Mat &weight,double gx, double gy, cv::Mat &out) {
  // for all possible centers
  for (int cy = 0; cy < out.rows; ++cy) {
    double *Or = out.ptr<double>(cy);
    const unsigned char *Wr = weight.ptr<unsigned char>(cy);
    for (int cx = 0; cx < out.cols; ++cx) {
      if (x == cx && y == cy) {
        continue;
      }
      // create a vector from the possible center to the gradient origin
      double dx = x - cx;
      double dy = y - cy;
      // normalize d
      double magnitude = sqrt((dx * dx) + (dy * dy));
      dx = dx / magnitude;
      dy = dy / magnitude;
      double dotProduct = dx*gx + dy*gy;
      dotProduct = std::max(0.0,dotProduct);
      // square and multiply by the weight
      if (kEnableWeight) {
        Or[cx] += dotProduct * dotProduct * (Wr[cx]/kWeightDivisor);
      } else {
        Or[cx] += dotProduct * dotProduct;
      }
    }
  }
}

//extern//only one
static
cv::Point findEyeCenter(const cv::Mat& wgim/*face*/, cv::Rect eye/*, std::string debugWindow*/) {
  cv::Mat eyeROIUnscaled = wgim(eye);//whole image gray scale
  cv::Mat eyeROI;
  scaleToFastSize(eyeROIUnscaled, eyeROI);
  // draw eye region
  //rectangle(face,eye,1234);//(Jonathan): NB
  //-- Find the gradient
  cv::Mat gradientX = computeMatXGradient(eyeROI);
  cv::Mat gradientY = computeMatXGradient(eyeROI.t()).t();
  //-- Normalize and threshold the gradient
  // compute all the magnitudes
  cv::Mat mags = matrixMagnitude(gradientX, gradientY);
  //compute the threshold
  double gradientThresh = computeDynamicThreshold(mags, kGradientThreshold);
  //double gradientThresh = kGradientThreshold;
  //double gradientThresh = 0;
  //normalize
  for (int y = 0; y < eyeROI.rows; ++y) {
    double *Xr = gradientX.ptr<double>(y), *Yr = gradientY.ptr<double>(y);
    const double *Mr = mags.ptr<double>(y);
    for (int x = 0; x < eyeROI.cols; ++x) {
      double gX = Xr[x], gY = Yr[x];
      double magnitude = Mr[x];
      if (magnitude > gradientThresh) {
        Xr[x] = gX/magnitude;
        Yr[x] = gY/magnitude;
      } else {
        Xr[x] = 0.0;
        Yr[x] = 0.0;
      }
    }
  }
  //imshow(debugWindow,gradientX);
  //-- Create a blurred and inverted image for weighting
  cv::Mat weight;
  GaussianBlur( eyeROI, weight, cv::Size( kWeightBlurSize, kWeightBlurSize ), 0, 0 );
  for (int y = 0; y < weight.rows; ++y) {
    unsigned char *row = weight.ptr<unsigned char>(y);
    for (int x = 0; x < weight.cols; ++x) {
      row[x] = (255 - row[x]);
    }
  }
  //imshow(debugWindow,weight);
  //-- Run the algorithm!
  cv::Mat outSum = cv::Mat::zeros(eyeROI.rows,eyeROI.cols,CV_64F);
  // for each possible gradient location
  // Note: these loops are reversed from the way the paper does them
  // it evaluates every possible center for each gradient location instead of
  // every possible gradient location for every center.
  //printf("Eye Size: %ix%i\n",outSum.cols,outSum.rows);


  for (int y = 0; y < weight.rows; ++y) {
    const double *Xr = gradientX.ptr<double>(y), *Yr = gradientY.ptr<double>(y);
    for (int x = 0; x < weight.cols; ++x) {
      double gX = Xr[x], gY = Yr[x];
      if (gX == 0.0 && gY == 0.0) {
        continue;
      }
      testPossibleCentersFormula(x, y, weight, gX, gY, outSum);
    }
  }
  // scale all the values down, basically averaging them
  double numGradients = (weight.rows*weight.cols);
  cv::Mat out;
  outSum.convertTo(out, CV_32F,1.0/numGradients);
  //imshow(debugWindow,out);
  //-- Find the maximum point
  cv::Point maxP;
  double maxVal;
  cv::minMaxLoc(out, NULL,&maxVal,NULL,&maxP);
  //-- Flood fill the edges
  if(kEnablePostProcess) {
    cv::Mat floodClone;
    //double floodThresh = computeDynamicThreshold(out, 1.5);
    double floodThresh = maxVal * kPostProcessThreshold;
    cv::threshold(out, floodClone, floodThresh, 0.0f, cv::THRESH_TOZERO);
    if(kPlotVectorField) {
      //plotVecField(gradientX, gradientY, floodClone);
      imwrite("eyeFrame.png",eyeROIUnscaled);
    }
    cv::Mat mask = floodKillEdges(floodClone);
    //imshow(debugWindow + " Mask",mask);
    //imshow(debugWindow,out);
    // redo max
    cv::minMaxLoc(out, NULL,&maxVal,NULL,&maxP,mask);
  }
  return unscalePoint(maxP,eye);
}

//#pragma mark Postprocessing

static
bool floodShouldPushPoint(const cv::Point &np, const cv::Mat &mat) {
  return inMat(np, mat.rows, mat.cols);
}

// returns a mask
static
cv::Mat floodKillEdges(cv::Mat &mat) {
  rectangle(mat,cv::Rect(0,0,mat.cols,mat.rows),255);

  cv::Mat mask(mat.rows, mat.cols, CV_8U, 255);
  std::queue<cv::Point> toDo;
  toDo.push(cv::Point(0,0));
  while (!toDo.empty()) {
    cv::Point p = toDo.front();
    toDo.pop();
    if (mat.at<float>(p) == 0.0f) {
      continue;
    }
    // add in every direction
    cv::Point np(p.x + 1, p.y); // right
    if (floodShouldPushPoint(np, mat)) toDo.push(np);
    np.x = p.x - 1; np.y = p.y; // left
    if (floodShouldPushPoint(np, mat)) toDo.push(np);
    np.x = p.x; np.y = p.y + 1; // down
    if (floodShouldPushPoint(np, mat)) toDo.push(np);
    np.x = p.x; np.y = p.y - 1; // up
    if (floodShouldPushPoint(np, mat)) toDo.push(np);
    // kill it
    mat.at<float>(p) = 0.0f;
    mask.at<uchar>(p) = 0;
  }
  return mask;
}


//helpers.cpp:

/*
static
bool rectInImage(cv::Rect rect, cv::Mat image) {
  return rect.x > 0 && rect.y > 0 && rect.x+rect.width < image.cols &&
  rect.y+rect.height < image.rows;
}
*/

static
bool inMat(cv::Point p,int rows,int cols) {
  return p.x >= 0 && p.x < cols && p.y >= 0 && p.y < rows;
}

static
cv::Mat matrixMagnitude(const cv::Mat &matX, const cv::Mat &matY) {
  cv::Mat mags(matX.rows,matX.cols,CV_64F);
  for (int y = 0; y < matX.rows; ++y) {
    const double *Xr = matX.ptr<double>(y), *Yr = matY.ptr<double>(y);
    double *Mr = mags.ptr<double>(y);
    for (int x = 0; x < matX.cols; ++x) {
      double gX = Xr[x], gY = Yr[x];
      double magnitude = sqrt((gX * gX) + (gY * gY));
      Mr[x] = magnitude;
    }
  }
  return mags;
}

static
double computeDynamicThreshold(const cv::Mat &mat, double stdDevFactor) {
  cv::Scalar stdMagnGrad, meanMagnGrad;
  cv::meanStdDev(mat, meanMagnGrad, stdMagnGrad);
  double stdDev = stdMagnGrad[0] / sqrt(mat.rows*mat.cols);
  return stdDevFactor * stdDev + meanMagnGrad[0];
}

extern//added
cv::Point detectPupilHume(cv::Mat& im, cv::Rect eye)
{
    using namespace cv;

    Mat wholeGray;//he was doing some weird split channels thing that didn't work for me?
    cvtColor(static_cast<const Mat&>(im), wholeGray, CV_BGR2GRAY);

    cv::Point const pnt = findEyeCenter(static_cast<const Mat&>(wholeGray), eye);//rel to eye rect

    cv::Point const icare = cv::Point(pnt.x + eye.x, pnt.y + eye.y);
    return icare;
}
