/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

// https://www.tnt.uni-hannover.de/papers/data/883/paper_EMMCVPR_11.pdf

#include "precomp.hpp"
#include "gcgraph.hpp"
#include <limits>
#include <time.h>

using namespace cv;

/*
This is implementation of image segmentation algorithm GrabCut described in
"GrabCut — Interactive Foreground Extraction using Iterated Graph Cuts".
Carsten Rother, Vladimir Kolmogorov, Andrew Blake.
 */

/*
 GMM - Gaussian Mixture Model
*/
class GMM
{
public:
    static const int componentsCount = 5;

    GMM( Mat& _model );
    double operator()( const Vec3d color ) const;
    double operator()( int ci, const Vec3d color ) const;
    int whichComponent( const Vec3d color ) const;

    void initLearning();
    void addSample( int ci, const Vec3d color );
    void endLearning();

private:
    void calcInverseCovAndDeterm( int ci );
    Mat model;
    double* coefs;
    double* mean;
    double* cov;

    double inverseCovs[componentsCount][3][3];
    double covDeterms[componentsCount];

    double sums[componentsCount][3];
    double prods[componentsCount][3][3];
    int sampleCounts[componentsCount];
    int totalSampleCount;
};

GMM::GMM( Mat& _model )
{
    const int modelSize = 3/*mean*/ + 9/*covariance*/ + 1/*component weight*/;
    if( _model.empty() )
    {
        _model.create( 1, modelSize*componentsCount, CV_64FC1 );
        _model.setTo(Scalar(0));
    }
    else if( (_model.type() != CV_64FC1) || (_model.rows != 1) || (_model.cols != modelSize*componentsCount) )
        CV_Error( CV_StsBadArg, "_model must have CV_64FC1 type, rows == 1 and cols == 13*componentsCount" );

    model = _model;

    coefs = model.ptr<double>(0);
    mean = coefs + componentsCount;
    cov = mean + 3*componentsCount;

    for( int ci = 0; ci < componentsCount; ci++ )
        if( coefs[ci] > 0 )
             calcInverseCovAndDeterm( ci );
}

double GMM::operator()( const Vec3d color ) const
{
    double res = 0;
    for( int ci = 0; ci < componentsCount; ci++ )
        res += coefs[ci] * (*this)(ci, color );
    return res;
}

double GMM::operator()( int ci, const Vec3d color ) const
{
    double res = 0;
    if( coefs[ci] > 0 )
    {
        CV_Assert( covDeterms[ci] > std::numeric_limits<double>::epsilon() );
        Vec3d diff = color;
        double* m = mean + 3*ci;
        diff[0] -= m[0]; diff[1] -= m[1]; diff[2] -= m[2];
        double mult = diff[0]*(diff[0]*inverseCovs[ci][0][0] + diff[1]*inverseCovs[ci][1][0] + diff[2]*inverseCovs[ci][2][0])
                   + diff[1]*(diff[0]*inverseCovs[ci][0][1] + diff[1]*inverseCovs[ci][1][1] + diff[2]*inverseCovs[ci][2][1])
                   + diff[2]*(diff[0]*inverseCovs[ci][0][2] + diff[1]*inverseCovs[ci][1][2] + diff[2]*inverseCovs[ci][2][2]);
        res = 1.0f/sqrt(covDeterms[ci]) * exp(-0.5f*mult);
    }
    return res;
}

int GMM::whichComponent( const Vec3d color ) const
{
    int k = 0;
    double max = 0;

    for( int ci = 0; ci < componentsCount; ci++ )
    {
        double p = (*this)( ci, color );
        if( p > max )
        {
            k = ci;
            max = p;
        }
    }
    return k;
}

void GMM::initLearning()
{
    for( int ci = 0; ci < componentsCount; ci++)
    {
        sums[ci][0] = sums[ci][1] = sums[ci][2] = 0;
        prods[ci][0][0] = prods[ci][0][1] = prods[ci][0][2] = 0;
        prods[ci][1][0] = prods[ci][1][1] = prods[ci][1][2] = 0;
        prods[ci][2][0] = prods[ci][2][1] = prods[ci][2][2] = 0;
        sampleCounts[ci] = 0;
    }
    totalSampleCount = 0;
}

void GMM::addSample( int ci, const Vec3d color )
{
    sums[ci][0] += color[0]; sums[ci][1] += color[1]; sums[ci][2] += color[2];
    prods[ci][0][0] += color[0]*color[0]; prods[ci][0][1] += color[0]*color[1]; prods[ci][0][2] += color[0]*color[2];
    prods[ci][1][0] += color[1]*color[0]; prods[ci][1][1] += color[1]*color[1]; prods[ci][1][2] += color[1]*color[2];
    prods[ci][2][0] += color[2]*color[0]; prods[ci][2][1] += color[2]*color[1]; prods[ci][2][2] += color[2]*color[2];
    sampleCounts[ci]++;
    totalSampleCount++;
}

void GMM::endLearning()
{
    const double variance = 0.01;
    for( int ci = 0; ci < componentsCount; ci++ )
    {
        int n = sampleCounts[ci];
        if( n == 0 )
            coefs[ci] = 0;
        else
        {
            coefs[ci] = (double)n/totalSampleCount;

            double* m = mean + 3*ci;
            m[0] = sums[ci][0]/n; m[1] = sums[ci][1]/n; m[2] = sums[ci][2]/n;

            double* c = cov + 9*ci;
            c[0] = prods[ci][0][0]/n - m[0]*m[0]; c[1] = prods[ci][0][1]/n - m[0]*m[1]; c[2] = prods[ci][0][2]/n - m[0]*m[2];
            c[3] = prods[ci][1][0]/n - m[1]*m[0]; c[4] = prods[ci][1][1]/n - m[1]*m[1]; c[5] = prods[ci][1][2]/n - m[1]*m[2];
            c[6] = prods[ci][2][0]/n - m[2]*m[0]; c[7] = prods[ci][2][1]/n - m[2]*m[1]; c[8] = prods[ci][2][2]/n - m[2]*m[2];

            double dtrm = c[0]*(c[4]*c[8]-c[5]*c[7]) - c[1]*(c[3]*c[8]-c[5]*c[6]) + c[2]*(c[3]*c[7]-c[4]*c[6]);
            if( dtrm <= std::numeric_limits<double>::epsilon() )
            {
                // Adds the white noise to avoid singular covariance matrix.
                c[0] += variance;
                c[4] += variance;
                c[8] += variance;
            }

            calcInverseCovAndDeterm(ci);
        }
    }
}

void GMM::calcInverseCovAndDeterm( int ci )
{
    if( coefs[ci] > 0 )
    {
        double *c = cov + 9*ci;
        double dtrm =
              covDeterms[ci] = c[0]*(c[4]*c[8]-c[5]*c[7]) - c[1]*(c[3]*c[8]-c[5]*c[6]) + c[2]*(c[3]*c[7]-c[4]*c[6]);

        CV_Assert( dtrm > std::numeric_limits<double>::epsilon() );
        inverseCovs[ci][0][0] =  (c[4]*c[8] - c[5]*c[7]) / dtrm;
        inverseCovs[ci][1][0] = -(c[3]*c[8] - c[5]*c[6]) / dtrm;
        inverseCovs[ci][2][0] =  (c[3]*c[7] - c[4]*c[6]) / dtrm;
        inverseCovs[ci][0][1] = -(c[1]*c[8] - c[2]*c[7]) / dtrm;
        inverseCovs[ci][1][1] =  (c[0]*c[8] - c[2]*c[6]) / dtrm;
        inverseCovs[ci][2][1] = -(c[0]*c[7] - c[1]*c[6]) / dtrm;
        inverseCovs[ci][0][2] =  (c[1]*c[5] - c[2]*c[4]) / dtrm;
        inverseCovs[ci][1][2] = -(c[0]*c[5] - c[2]*c[3]) / dtrm;
        inverseCovs[ci][2][2] =  (c[0]*c[4] - c[1]*c[3]) / dtrm;
    }
}

/*
  Calculate beta - parameter of GrabCut algorithm.
  beta = 1/(2*avg(sqr(||color[i] - color[j]||)))
*/
static double calcBeta( const Mat& img )
{
    double beta = 0;
    for( int y = 0; y < img.rows; y++ )
    {
        for( int x = 0; x < img.cols; x++ )
        {
            Vec3d color = img.at<Vec3b>(y,x);
            if( x>0 ) // left
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y,x-1);
                beta += diff.dot(diff);
            }
            if( y>0 && x>0 ) // upleft
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x-1);
                beta += diff.dot(diff);
            }
            if( y>0 ) // up
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x);
                beta += diff.dot(diff);
            }
            if( y>0 && x<img.cols-1) // upright
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x+1);
                beta += diff.dot(diff);
            }
        }
    }
    if( beta <= std::numeric_limits<double>::epsilon() )
        beta = 0;
    else
        beta = 1.f / (2 * beta/(4*img.cols*img.rows - 3*img.cols - 3*img.rows + 2) );

    return beta;
}

/*
  Calculate weights of noterminal vertices of graph.
  beta and gamma - parameters of GrabCut algorithm.
 */
static void calcNWeights( const Mat& img, Mat& leftW, Mat& upleftW, Mat& upW, Mat& uprightW, double beta, double gamma )
{
    const double gammaDivSqrt2 = gamma / std::sqrt(2.0f);
    leftW.create( img.rows, img.cols, CV_64FC1 );
    upleftW.create( img.rows, img.cols, CV_64FC1 );
    upW.create( img.rows, img.cols, CV_64FC1 );
    uprightW.create( img.rows, img.cols, CV_64FC1 );
    for( int y = 0; y < img.rows; y++ )
    {
        for( int x = 0; x < img.cols; x++ )
        {
            Vec3d color = img.at<Vec3b>(y,x);
            if( x-1>=0 ) // left
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y,x-1);
                leftW.at<double>(y,x) = gamma * exp(-beta*diff.dot(diff));
            }
            else
                leftW.at<double>(y,x) = 0;
            if( x-1>=0 && y-1>=0 ) // upleft
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x-1);
                upleftW.at<double>(y,x) = gammaDivSqrt2 * exp(-beta*diff.dot(diff));
            }
            else
                upleftW.at<double>(y,x) = 0;
            if( y-1>=0 ) // up
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x);
                upW.at<double>(y,x) = gamma * exp(-beta*diff.dot(diff));
            }
            else
                upW.at<double>(y,x) = 0;
            if( x+1<img.cols && y-1>=0 ) // upright
            {
                Vec3d diff = color - (Vec3d)img.at<Vec3b>(y-1,x+1);
                uprightW.at<double>(y,x) = gammaDivSqrt2 * exp(-beta*diff.dot(diff));
            }
            else
                uprightW.at<double>(y,x) = 0;
        }
    }
}

/*
  Check size, type and element values of mask matrix.
 */
static void checkMask( const Mat& img, const Mat& mask )
{
    if( mask.empty() )
        CV_Error( CV_StsBadArg, "mask is empty" );
    if( mask.type() != CV_8UC1 )
        CV_Error( CV_StsBadArg, "mask must have CV_8UC1 type" );
    if( mask.cols != img.cols || mask.rows != img.rows )
        CV_Error( CV_StsBadArg, "mask must have as many rows and cols as img" );
    for( int y = 0; y < mask.rows; y++ )
    {
        for( int x = 0; x < mask.cols; x++ )
        {
            uchar val = mask.at<uchar>(y,x);
            if( val!=GC_BGD && val!=GC_FGD && val!=GC_PR_BGD && val!=GC_PR_FGD )
                CV_Error( CV_StsBadArg, "mask element value must be equel"
                    "GC_BGD or GC_FGD or GC_PR_BGD or GC_PR_FGD" );
        }
    }
}

/*
  Initialize mask using rectangular.
*/
static void initMaskWithRect( Mat& mask, Size imgSize, Rect rect )
{
    mask.create( imgSize, CV_8UC1 );
    mask.setTo( GC_BGD );

    rect.x = std::max(0, rect.x);
    rect.y = std::max(0, rect.y);
    rect.width = std::min(rect.width, imgSize.width-rect.x);
    rect.height = std::min(rect.height, imgSize.height-rect.y);

    (mask(rect)).setTo( Scalar(GC_PR_FGD) );
}

/*
  Initialize GMM background and foreground models using kmeans algorithm.
*/
static void initGMMs( const Mat& img, const Mat& mask, GMM& bgdGMM, GMM& fgdGMM )
{
    const int kMeansItCount = 10;
    const int kMeansType = KMEANS_PP_CENTERS;

    Mat bgdLabels, fgdLabels;
    std::vector<Vec3f> bgdSamples, fgdSamples;
    Point p;
    for( p.y = 0; p.y < img.rows; p.y++ )
    {
        for( p.x = 0; p.x < img.cols; p.x++ )
        {
            if( mask.at<uchar>(p) == GC_BGD || mask.at<uchar>(p) == GC_PR_BGD )
                bgdSamples.push_back( (Vec3f)img.at<Vec3b>(p) );
            else // GC_FGD | GC_PR_FGD
                fgdSamples.push_back( (Vec3f)img.at<Vec3b>(p) );
        }
    }
    CV_Assert( !bgdSamples.empty() && !fgdSamples.empty() );
    Mat _bgdSamples( (int)bgdSamples.size(), 3, CV_32FC1, &bgdSamples[0][0] );
    kmeans( _bgdSamples, GMM::componentsCount, bgdLabels,
            TermCriteria( CV_TERMCRIT_ITER, kMeansItCount, 0.0), 0, kMeansType );
    Mat _fgdSamples( (int)fgdSamples.size(), 3, CV_32FC1, &fgdSamples[0][0] );
    kmeans( _fgdSamples, GMM::componentsCount, fgdLabels,
            TermCriteria( CV_TERMCRIT_ITER, kMeansItCount, 0.0), 0, kMeansType );

    bgdGMM.initLearning();
    for( int i = 0; i < (int)bgdSamples.size(); i++ )
        bgdGMM.addSample( bgdLabels.at<int>(i,0), bgdSamples[i] );
    bgdGMM.endLearning();

    fgdGMM.initLearning();
    for( int i = 0; i < (int)fgdSamples.size(); i++ )
        fgdGMM.addSample( fgdLabels.at<int>(i,0), fgdSamples[i] );
    fgdGMM.endLearning();
}

/*
  Assign GMMs components for each pixel.
*/
static void assignGMMsComponents( const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, Mat& compIdxs )
{
    Point p;
    for( p.y = 0; p.y < img.rows; p.y++ )
    {
        for( p.x = 0; p.x < img.cols; p.x++ )
        {
            Vec3d color = img.at<Vec3b>(p);
            compIdxs.at<int>(p) = mask.at<uchar>(p) == GC_BGD || mask.at<uchar>(p) == GC_PR_BGD ?
                bgdGMM.whichComponent(color) : fgdGMM.whichComponent(color);
        }
    }
}

/*
  Learn GMMs parameters.
*/
static void learnGMMs( const Mat& img, const Mat& mask, const Mat& compIdxs, GMM& bgdGMM, GMM& fgdGMM )
{
    bgdGMM.initLearning();
    fgdGMM.initLearning();
    Point p;
    for( int ci = 0; ci < GMM::componentsCount; ci++ )
    {
        for( p.y = 0; p.y < img.rows; p.y++ )
        {
            for( p.x = 0; p.x < img.cols; p.x++ )
            {
                if( compIdxs.at<int>(p) == ci )
                {
                    if( mask.at<uchar>(p) == GC_BGD || mask.at<uchar>(p) == GC_PR_BGD )
                        bgdGMM.addSample( ci, img.at<Vec3b>(p) );
                    else
                        fgdGMM.addSample( ci, img.at<Vec3b>(p) );
                }
            }
        }
    }
    bgdGMM.endLearning();
    fgdGMM.endLearning();
}

// Sink (BGD) weight for pixel p in the non reduced (virtual) graph
static inline double getSinkW(Point p, const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, double lambda)
{
	double toSink = 0;
	Vec3b color = img.at<Vec3b>(p.y, p.x);

	if (mask.at<uchar>(p) == GC_BGD)

		toSink = lambda;

	else if (mask.at<uchar>(p.y, p.x) == GC_PR_BGD || mask.at<uchar>(p.y, p.x) == GC_PR_FGD)

		toSink = -log(fgdGMM(color)); // bien fgd (see original constructGCGraph)

	return toSink;
}

// Source (FGD) weight for pixel p in the non reduced (virtual) graph
static inline double getSourceW(Point p, const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, double lambda)
{
	double fromSource = 0;
	Vec3b color = img.at<Vec3b>(p.y, p.x);

	if (mask.at<uchar>(p) == GC_FGD) 

		fromSource = lambda;

	else if (mask.at<uchar>(p.y, p.x) == GC_PR_BGD || mask.at<uchar>(p.y, p.x) == GC_PR_FGD)

		fromSource = -log(bgdGMM(color));  // bien bgd
	
	return fromSource;
}

//sum of weights of pending edges for pxl (joined or not joined) in current graph(pixel p not simplified)
static inline double pendingSumW(const Point p, const Point pxl, const Mat& img, const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW)
{
	double s = 0;

	// border pxl
	if (((pxl.y == p.y) && (pxl.x < p.x)) || ((pxl.y == p.y - 1) && (pxl.x >= p.x)))  
	{
		if (pxl.x == p.x - 1) 
		    s += leftW.at<double>(Point(pxl.x + 1, pxl.y));

		if (pxl.y < img.rows - 1)
		{
			s += upW.at<double>(pxl.y + 1, pxl.x);

			if ((pxl.x > 0) && (pxl.x != p.x))

				s += uprightW.at<double>(pxl.y + 1, pxl.x - 1);

			if (pxl.x < img.cols - 1)

				s += upleftW.at<double>(pxl.y + 1, pxl.x + 1);

		}
	}

	// 
	if ((pxl.y == p.y - 1) && (pxl.x == p.x - 1))

		s += upleftW.at<double>(p);

	return s;
}

// sum of weights of pending edges for sink in current graph ( pixel p not simplified)
static inline double getSinkPendingSumW(const Point p, const Mat& img, const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW, const std::vector<Point>& sinkToPxl)
{
	double s = 0;

	for (size_t i = sinkToPxl.size() - 1; i-- > 0; )  //(size_t i = sinkToPxl.size() - 1; i >= 0; i--) i=0 included; Caution size_t is unsigned type
	{
		Point pxl = sinkToPxl[i];

		s += pendingSumW(p, pxl, img, leftW, upleftW, upW, uprightW);

		if ((pxl.y <= p.y - 1) && (pxl.x < p.x - 1))  // pixels are added to sourceToPxl in increasing col/row order 

			break;
	}

	return s;
}

// sum of weights of pending edges for source in current graph (pixel p not simplified)
static inline double getSourcePendingSumW(const Point p, const Mat& img, const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW, const std::vector<Point>& sourceToPxl)
{
	double s = 0;

	//for (int i = sourceToPxl.size() - 1; i >= 0; i--)
	for (size_t i = sourceToPxl.size() - 1; i-- > 0; ) // i=0 included; Caution size_t is unsigned type
	{
		Point pxl = sourceToPxl[i];

		s += pendingSumW(p, pxl, img, leftW, upleftW, upW, uprightW);

		if ((pxl.y <= p.y - 1) && (pxl.x < p.x - 1))  // pixels are added to sourceToPxl in increasing col/row order 

			break;
	}

	return s;
}



// Init matrix sigmaW of total weight for pixels in non reduced (virtual) graph. Terminal weights are included.
static void initSigmaW(const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, Mat& sigmaW, 
	                       const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW, double lambda)
{
	Point p;

	//sigmaW.create(img.rows, img.cols, CV_64FC1);  // double done by constructGCGrapg_slim
	
	for (p.y = 0; p.y < img.rows; p.y++)
	{
		for (p.x = 0; p.x < img.cols; p.x++)
		{   
			double s = 0;
			//sigmaW.at<double>(p.y, p.x) = 0;
			s += leftW.at<double>(p.y, p.x);
			s += upleftW.at<double>(p.y, p.x);
			s += upW.at<double>(p.y, p.x);
			s += uprightW.at<double>(p.y, p.x);

			if (p.x < img.cols-1)
				s += leftW.at<double>(p.y, p.x+1);
			if (p.x < img.cols - 1 && p.y < img.rows-1)
				s += upleftW.at<double>(p.y+1, p.x+1);
			if (p.y < img.rows - 1)
				s += upW.at<double>(p.y + 1, p.x );
			if (p.x > 0 && p.y < img.rows - 1)
				s += uprightW.at<double>(p.y + 1, p.x-1);


			double fromSource, toSink;
			Vec3b color = img.at<Vec3b>(p.y, p.x);

			if (mask.at<uchar>(p.y, p.x) == GC_PR_BGD || mask.at<uchar>(p.y, p.x) == GC_PR_FGD)
			{
				fromSource = -log(bgdGMM(color));
				toSink = -log(fgdGMM(color));
				//printf("from source: %.2f color %d %d %d", fromSource, color[0], color[1], color[2]);
			}
			else if (mask.at<uchar>(p) == GC_BGD)
			{
				fromSource = 0;
				toSink = lambda;
			}
			else // GC_FGD
			{
				fromSource = lambda;
				toSink = 0;
			}

			sigmaW.at<double>(p.y, p.x) = s+fromSource+toSink;  
		}

	}
}

// compute sum of weights for all edges adjacent to node vtx[i], including
// pending (after p) edges.
static inline double slimSumW(const Mat& img, const int i, const Point p, GCGraph<double>& graph, 
	                            const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW,
								const Mat_<Point>& Vtx2pxl)
{
	double s = graph.sumW(i); // sum of weights for edges adjacent to vtx[i], including source and sink

	// add weights of pending edges
	Point pxl = graph.getFirstP(i);  
	for (Point pxl = graph.getFirstP(i); pxl != Point(-1, -1); pxl = Vtx2pxl.at<Point>(p))

		s += pendingSumW(p, pxl, img, leftW, upleftW, upW, uprightW);

	return s;
	//{
	//	if ((pxl.x == p.x - 1) && (pxl.y == p.y-1))  // up-left neighbor pf p 
	//		s += upleftW.at<double>(p);

	//	if (((pxl.y == p.y) && (pxl.x < p.x)) || ((pxl.y == p.y - 1) && (pxl.x>=p.x)))  // border pixel
	//	{
	//		if (pxl.x == p.x - 1) 
	//			s += leftW.at<double>(Point(pxl.x + 1, pxl.y));
	//		
	//		//if (pxl.y == p.y -1)
	//		//	s += upW.at<double>(Point(pxl.x, pxl.y+1));

	//		if (pxl.y < img.rows - 1)
	//		{
	//			s += upW.at<double>(Point(pxl.x, pxl.y + 1));  

	//			if ((pxl.x > 0) && (pxl.x != p.x))
	//			//if (pxl.x > 0) 
	//				s += uprightW.at<double>(Point(pxl.x - 1, pxl.y + 1));

	//			if (pxl.x < img.cols - 1)
	//				s += upleftW.at<double>(Point(pxl.x + 1, pxl.y + 1));
	//		}
	//	}
	//}
	//return s;
}

#define BV_NO_VTX_FOUND -10

// search for first  node  to which pixel p can be joined
// return node index (negative GC_JNT_BGD or GC_JNT_FGD for terminal node) or BV_NO_VTX_FOUND
static inline int searchJoin(const Point p, const Mat& img, const Mat& sigmaW, const Mat& pxl2Vtx, 
	                  const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW, 
					  GCGraph<double>& graph, const Mat_<Point>& Vtx2pxl, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM,
					  const double lambda, const std::vector<Point>& sinkToPxl, const std::vector<Point>& sourceToPxl)
{   
	int nghbrVtx[4];  // indices of neighbor vertices (order left, upleft, up, upright)
	double w[4];
	double s[4];
	for (int i = 0; i < 4; i++)
	{
		nghbrVtx[i] = -10; // no neighbor
		w[i] = 0;
		s[i] = 0;
	}

	if (p.x > 0)
	{
		nghbrVtx[0] = pxl2Vtx.at<int>(p.y, p.x - 1);
		w[0] = leftW.at<double>(p);
	}
	if ((p.x > 0) && (p.y > 0))
	{
		nghbrVtx[1] = pxl2Vtx.at<int>(p.y - 1, p.x - 1);
		w[1] = upleftW.at<double>(p);
	}
	if (p.y > 0)
	{
		nghbrVtx[2] = pxl2Vtx.at<int>(p.y - 1, p.x);
		w[2] = upW.at<double>(p);
	}
	if ((p.y > 0) && (p.x < img.cols - 1))
	{
		nghbrVtx[3] = pxl2Vtx.at<int>(p.y - 1, p.x + 1);
		w[3] = uprightW.at<double>(p);
	}

	double ws = getSinkW(p, img, mask, bgdGMM, fgdGMM, lambda);
	double wt = getSourceW(p, img, mask, bgdGMM, fgdGMM, lambda);
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			if (nghbrVtx[i] == nghbrVtx[j])
				s[i] += w[j];
		if (nghbrVtx[i] == GC_JNT_BGD)
			s[i] += ws; // getSinkW(p, img, mask, bgdGMM, fgdGMM, lambda);
		if (nghbrVtx[i] == GC_JNT_FGD)
			s[i] += wt; // getSourceW(p, img, mask, bgdGMM, fgdGMM, lambda);
	}

	//if (getSinkW(p, img, mask, bgdGMM, fgdGMM, lambda) > 0.5 * sigmaW.at<double>(p))
	if (ws > 0.5 * sigmaW.at<double>(p))
		return GC_JNT_BGD;
	//if (getSinkW(p, img, mask, bgdGMM, fgdGMM, lambda) > 0.5 * (graph.sink_sigmaW + getSinkPendingSumW(p, img, leftW, upleftW, upW, uprightW, sinkToPxl)))
	//	return GC_JNT_BGD; unsafe as sink sum of weights should include terminal weights for all graph, and useless !!!
	if (wt > 0.5 * sigmaW.at<double>(p))
		return GC_JNT_FGD;
	//if (getSourceW(p, img, mask, bgdGMM, fgdGMM, lambda) > 0.5 * (graph.source_sigmaW + getSourcePendingSumW(p, img, leftW, upleftW, upW, uprightW, sinkToPxl)))
	//	return GC_JNT_FGD; unsafe and useless


	// search for first joinable neighbor
	for (int i = 0; i < 4; i++)
	{
		int cn = nghbrVtx[i];
		if (cn == -10)
			continue;

		// first condition for simple edge
		if (s[i] > 0.5 * sigmaW.at<double>(p))
			 return cn;

		// dual condition
		if (cn >= 0)
		{
			if (s[i] > 0.5 * slimSumW(img, cn, p, graph, leftW, upleftW, upW, uprightW, Vtx2pxl))
				return cn;
		}
		else // neighbor is joined to terminal
			if (cn == GC_JNT_BGD)
			{
				if (ws > 0.5*(graph.sink_sigmaW + getSinkPendingSumW(p, img, leftW, upleftW, upW, uprightW, sinkToPxl)))
					return cn;
			}
			else
			{
				if (wt > 0.5*(graph.source_sigmaW + getSourcePendingSumW(p, img, leftW, upleftW, upW, uprightW, sourceToPxl)))
					return cn;
			}
	}
	return BV_NO_VTX_FOUND;
}
	
	
// optimized version of ConstructGCGraph. 
// Author BV
// 
static void constructGCGraph_slim( const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, double lambda,
                       const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW,
					   GCGraph<double>& graph, Mat& pxl2Vtx)
{
    int vtxCount = img.cols*img.rows,
        edgeCount = 2*(4*img.cols*img.rows - 3*(img.cols + img.rows) + 2);
	
    graph.create(vtxCount, edgeCount);
    Point p;
	int vtxIdx;
	double s2tw=0.0;  // source to sink weight
	int count = 0;

	Mat_<Point> Vtx2pxl(img.rows, img.cols); // lists of pixels joined to vertices
	std::vector<Point> sinkToPxl, sourceToPxl; // lists of pixels joined to terminal nodes
	Mat_<double> sigmaW(img.rows, img.cols, CV_64FC1);  //double
	Vtx2pxl = Point(-1, -1);
	initSigmaW(img, mask, bgdGMM, fgdGMM, sigmaW, leftW, upleftW, upW, uprightW, lambda);

    for( p.y = 0; p.y < img.rows; p.y++ )
    {
        for( p.x = 0; p.x < img.cols; p.x++)
        {     
            Vec3b color = img.at<Vec3b>(p);
			
            // set t-weights
            double fromSource, toSink;

			// add node and set its t-weights
            if( mask.at<uchar>(p) == GC_PR_BGD || mask.at<uchar>(p) == GC_PR_FGD )
            {
				int i = searchJoin(p, img, sigmaW, pxl2Vtx, leftW, upleftW, upW, uprightW, graph, Vtx2pxl, mask, bgdGMM, fgdGMM, lambda, sinkToPxl, sourceToPxl);
				//i = BV_NO_VTX_FOUND; // BLOCK JOIN REMOVE*************************************************************************
				if (i != BV_NO_VTX_FOUND)
				{
					count++;
					if (i >= 0)
					{
						vtxIdx = i;  // node to join
						pxl2Vtx.at<int>(p) = vtxIdx;
						Vtx2pxl.at<Point>(p) = graph.getFirstP(vtxIdx);
						graph.setFirstP(vtxIdx, p);
					}
					else // terminal node
					{
						vtxIdx = i;  // node to join
						pxl2Vtx.at<int>(p) = vtxIdx;
						if (i == GC_JNT_BGD)
							sinkToPxl.push_back(p);
						else
							sourceToPxl.push_back(p);
					}
				}
				else// NO_VTX_FOUND
				{
					vtxIdx = graph.addVtx();
					pxl2Vtx.at<int>(p) = vtxIdx;
					graph.setFirstP(vtxIdx, p);  // first and last pixel

				}
				if (vtxIdx >= 0)
				{
					fromSource = -log(bgdGMM(color));
					toSink = -log(fgdGMM(color));
					graph.addTermWeights(vtxIdx, fromSource, toSink);
				}
				else if (vtxIdx == GC_JNT_BGD)
				    s2tw += getSourceW(p, img, mask, bgdGMM, fgdGMM, lambda);  // -log(bgdGMM(color))??
				else
					s2tw += getSinkW(p, img, mask, bgdGMM, fgdGMM, lambda);
				//printf("slim from source: %.2f color %d %d %d", fromSource, color[0], color[1], color[2]);
            }
            else if( mask.at<uchar>(p) == GC_BGD )
			// join to sink
            {
				pxl2Vtx.at<int>(p) = GC_JNT_BGD; // join node to sink (BG) -1 = GC_JNT_BGD
                fromSource = 0;  // as fromSource=0, weight of edge(source, sink) is unmodified
                toSink = lambda; // edge deleted by the join operation
				sinkToPxl.push_back(p);
				//continue;
            }
            else // GC_FGD
			// join to source
            {
				pxl2Vtx.at<int>(p) = GC_JNT_FGD; // join node to source (FG) -2=GC_JNT_FGD
                fromSource = lambda; // edge deleted by the join operation
                toSink = 0; // as toSink=0, weight of edge(source, sink) is unmodified
				sourceToPxl.push_back(p);
				//continue;
            }
	
            
			// Set n-weights and t-weights for non terminal neighbors
			// and update t-weights for terminal neighbors.
			int vtx = pxl2Vtx.at<int>(p); 
			if (p.x > 0)
			{
				double w = leftW.at<double>(p);
				int n = pxl2Vtx.at<int>(Point(p.x - 1, p.y)); // equiv to at<int>(p.y, p.x-1)
				if (n >= 0)  // no terminal W-neighbor
					if (vtx >= 0) // no terminal node
					{
						if (vtx != n)
							//graph.addEdges(vtx, n, w, w);
							graph.addWeight(vtx, n, w);
					}
					else
						graph.addTermWeights(n, (jfg(vtx) ? w : 0), (jbg(vtx) ? w : 0));
				else
					if (vtx >= 0)
						graph.addTermWeights(vtx, (jfg(n) ? w : 0), (jbg(n) ? w : 0));
					else
						if (jbg(vtx) != jbg(n))
							s2tw += w;
            }
            if( p.x>0 && p.y>0 )
            {
                double w = upleftW.at<double>(p);
				int n = pxl2Vtx.at<int>(Point(p.x - 1, p.y - 1));
				if (n >= 0) // not terminal NW-neighbor
					if (vtx >= 0) // not terminal node
					{//
						if (vtx != n)
							//graph.addEdges(vtx, n, w, w);
							graph.addWeight(vtx, n, w);
					}//
					else
						graph.addTermWeights(n, (jfg(vtx) ? w : 0), (jbg(vtx) ? w : 0));
				else // neighbor is terminal
					if (vtx >= 0)
						graph.addTermWeights(vtx, (jfg(n) ? w : 0), (jbg(n) ? w : 0));
					else
						if (jbg(vtx) != jbg(n))
							s2tw += w;
            }
            if( p.y>0 )
            {
                double w = upW.at<double>(p);
				int n = pxl2Vtx.at<int>(Point(p.x, p.y - 1));
				if (n >= 0)
					if (vtx >= 0)
					{//
						if (vtx != n)
							//graph.addEdges(vtx, n, w, w);
							graph.addWeight(vtx, n, w);
					}//
					else
						graph.addTermWeights(n, (jfg(vtx) ? w : 0), (jbg(vtx) ? w : 0));
				else
					if (vtx >= 0)
					    graph.addTermWeights(vtx, (jfg(n) ? w : 0), (jbg(n) ? w : 0));
					else
						if (jbg(vtx) != jbg(n))
							s2tw += w;
            }
            if( p.x<img.cols-1 && p.y>0 )
            {
                double w = uprightW.at<double>(p);
				int n = pxl2Vtx.at<int>(Point(p.x + 1, p.y - 1));
				if (n >= 0)
					if (vtx >= 0)
					{ //
						if (vtx != n)
							//graph.addEdges(vtx, n, w, w);
							graph.addWeight(vtx, n, w);
					}//
					else
						graph.addTermWeights(n, (jfg(vtx) ? w : 0), (jbg(vtx) ? w : 0));
				else
					if (vtx >= 0)
					    graph.addTermWeights(vtx, (jfg(n) ? w : 0), (jbg(n) ? w : 0));
					else
						if (jbg(vtx) != jbg(n))
							s2tw += w;
            }
			
			//double flow = graph.maxFlow();
			//printf("count=%d ", count++);
			//printf("Slim flow: %.2f\n", flow);
			
        }
    }
	//printf("s- t weight: %.2f\n", s2tw);
	printf("joinable vtx found %d\n", count);

	int simple = graph.searchSimpleEdges();  // TODO : to be removed
	printf("simple edges %d\n", simple);
	printf("s2tw %.2f\n", s2tw);
}


//  Slim version of Estimate segmentation using MaxFlow algorithm
static void estimateSegmentation_slim( GCGraph<double>& graph, Mat& mask, Mat& ptx2Vtx )
{
    double flow=graph.maxFlow();
	printf("Slim flow: %.2f\n", flow);
    Point p;
    for( p.y = 0; p.y < mask.rows; p.y++ )
    {
        for( p.x = 0; p.x < mask.cols; p.x++ )
        {
            if( mask.at<uchar>(p) == GC_PR_BGD || mask.at<uchar>(p) == GC_PR_FGD )
            {
				int v = ptx2Vtx.at<int>(p);
				if (v == GC_JNT_BGD )
				{
					mask.at<uchar>(p) = GC_PR_BGD; //GC_PR_BGD;
					//continue;
				}
				else if (v == GC_JNT_FGD )
				{
					mask.at<uchar>(p) = GC_PR_FGD; // GC_PR_BGD;
					//continue;
				}
				else if (graph.inSourceSegment(v)) // p.y*mask.cols+p.x /*vertex index*/ ) )
                    mask.at<uchar>(p) = GC_PR_FGD;
                else
                    mask.at<uchar>(p) = GC_PR_BGD;
            }
        }
    }
}

static void constructGCGraph(const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, double lambda,
	const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW,
	GCGraph<double>& graph);

// Slim version of Grabcut algorithm
void cv::grabCut_slim( InputArray _img, InputOutputArray _mask, Rect rect,
                  InputOutputArray _bgdModel, InputOutputArray _fgdModel,
                  int iterCount, int mode )
{
    Mat img = _img.getMat();
    Mat& mask = _mask.getMatRef();
    Mat& bgdModel = _bgdModel.getMatRef();
    Mat& fgdModel = _fgdModel.getMatRef();

    if( img.empty() )
        CV_Error( CV_StsBadArg, "image is empty" );
    if( img.type() != CV_8UC3 )
        CV_Error( CV_StsBadArg, "image must have CV_8UC3 type" );

    GMM bgdGMM( bgdModel ), fgdGMM( fgdModel );
    Mat compIdxs( img.size(), CV_32SC1 );
	Mat pxl2Vtx(img.size(), CV_32SC1);   // pixel vertices

	clock_t tStart;
    if( mode == GC_INIT_WITH_RECT || mode == GC_INIT_WITH_MASK )
    {
        if( mode == GC_INIT_WITH_RECT )
            initMaskWithRect( mask, img.size(), rect );
        else // flag == GC_INIT_WITH_MASK
            checkMask( img, mask );
		tStart = clock();
        initGMMs( img, mask, bgdGMM, fgdGMM );
		printf("initGMM: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);
    }

    if( iterCount <= 0)
        return;

    if( mode == GC_EVAL )
        checkMask( img, mask );

    const double gamma = 50;
    const double lambda = 9*gamma;
	tStart = clock();
    const double beta = calcBeta( img );
	printf("calcBeta: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);

    Mat leftW, upleftW, upW, uprightW, sigmaNW;

	tStart = clock();
    calcNWeights( img, leftW, upleftW, upW, uprightW, beta, gamma );
	printf("calcNWeights: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);
	
    for( int i = 0; i < iterCount; i++ )
    {
        GCGraph<double> graph;
		tStart = clock();
        assignGMMsComponents( img, mask, bgdGMM, fgdGMM, compIdxs );
		printf("assignGMMsComponents: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);

		tStart = clock();
        learnGMMs( img, mask, compIdxs, bgdGMM, fgdGMM );
		printf("learnGMMs: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);

		tStart = clock();
        constructGCGraph_slim(img, mask, bgdGMM, fgdGMM, lambda, leftW, upleftW, upW, uprightW, graph, pxl2Vtx);
		printf("construcGCGraph slim: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);

		GCGraph<double> graph2;
		constructGCGraph(img, mask, bgdGMM, fgdGMM, lambda, leftW, upleftW, upW, uprightW, graph2);
		double flow = graph2.maxFlow();
		printf("test flow: %.2f\n", flow);

		tStart = clock();
        estimateSegmentation_slim( graph, mask, pxl2Vtx );
		printf("estimateSegmentation slim: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);
    }
}
// End of modification. BV

/*#else  */
/*
Construct GCGraph
*/
static void constructGCGraph(const Mat& img, const Mat& mask, const GMM& bgdGMM, const GMM& fgdGMM, double lambda,
	const Mat& leftW, const Mat& upleftW, const Mat& upW, const Mat& uprightW,
	GCGraph<double>& graph)
{
	int vtxCount = img.cols*img.rows,
		edgeCount = 2 * (4 * img.cols*img.rows - 3 * (img.cols + img.rows) + 2);
	graph.create(vtxCount, edgeCount);
	Point p;
	//int count = 0;
	for (p.y = 0; p.y < img.rows; p.y++)
	{
		for (p.x = 0; p.x < img.cols; p.x++)
		{
			// add node
			int vtxIdx = graph.addVtx();
			Vec3b color = img.at<Vec3b>(p);

			// set t-weights
			double fromSource, toSink;
			if (mask.at<uchar>(p) == GC_PR_BGD || mask.at<uchar>(p) == GC_PR_FGD)
			{
				fromSource = -log(bgdGMM(color));
				toSink = -log(fgdGMM(color));
				//printf("from source: %.2f color %d %d %d", fromSource, color[0], color[1], color[2]);
			}
			else if (mask.at<uchar>(p) == GC_BGD)
			{
				fromSource = 0;
				toSink = lambda;
			}
			else // GC_FGD
			{
				fromSource = lambda;
				toSink = 0;
			}
			graph.addTermWeights(vtxIdx, fromSource, toSink);

			// set n-weights
			if (p.x>0)
			{
				double w = leftW.at<double>(p);
				graph.addEdges(vtxIdx, vtxIdx - 1, w, w);
			}
			if (p.x>0 && p.y>0)
			{
				double w = upleftW.at<double>(p);
				graph.addEdges(vtxIdx, vtxIdx - img.cols - 1, w, w);
			}
			if (p.y>0)
			{
				double w = upW.at<double>(p);
				graph.addEdges(vtxIdx, vtxIdx - img.cols, w, w);
			}
			if (p.x<img.cols - 1 && p.y>0)
			{
				double w = uprightW.at<double>(p);
				graph.addEdges(vtxIdx, vtxIdx - img.cols + 1, w, w);
			}
			//double flow = graph.maxFlow();
			//printf("count=%d ", count++);
			//printf("flow: %.2f\n", flow);
		}
	}
}

/*
Estimate segmentation using MaxFlow algorithm
*/
static void estimateSegmentation(GCGraph<double>& graph, Mat& mask)
{
	double flow=graph.maxFlow();
	printf("Flow: %.2fs\n", flow);
	Point p;
	for (p.y = 0; p.y < mask.rows; p.y++)
	{
		for (p.x = 0; p.x < mask.cols; p.x++)
		{
			if (mask.at<uchar>(p) == GC_PR_BGD || mask.at<uchar>(p) == GC_PR_FGD)
			{
				if (graph.inSourceSegment(p.y*mask.cols + p.x /*vertex index*/))
					mask.at<uchar>(p) = GC_PR_FGD;
				else
					mask.at<uchar>(p) = GC_PR_BGD;
			}
		}
	}
}

void cv::grabCut(InputArray _img, InputOutputArray _mask, Rect rect,
	InputOutputArray _bgdModel, InputOutputArray _fgdModel,
	int iterCount, int mode)
{
	Mat img = _img.getMat();
	Mat& mask = _mask.getMatRef();
	Mat& bgdModel = _bgdModel.getMatRef();
	Mat& fgdModel = _fgdModel.getMatRef();
	clock_t tStart;

	if (img.empty())
		CV_Error(CV_StsBadArg, "image is empty");
	if (img.type() != CV_8UC3)
		CV_Error(CV_StsBadArg, "image must have CV_8UC3 type");

	GMM bgdGMM(bgdModel), fgdGMM(fgdModel);
	Mat compIdxs(img.size(), CV_32SC1);

	if (mode == GC_INIT_WITH_RECT || mode == GC_INIT_WITH_MASK)
	{
		if (mode == GC_INIT_WITH_RECT)
			initMaskWithRect(mask, img.size(), rect);
		else // flag == GC_INIT_WITH_MASK
			checkMask(img, mask);
		initGMMs(img, mask, bgdGMM, fgdGMM);
	}

	if (iterCount <= 0)
		return;

	if (mode == GC_EVAL)
		checkMask(img, mask);

	const double gamma = 50;
	const double lambda = 9 * gamma;
	const double beta = calcBeta(img);

	Mat leftW, upleftW, upW, uprightW;
	calcNWeights(img, leftW, upleftW, upW, uprightW, beta, gamma);

	for (int i = 0; i < iterCount; i++)
	{
		GCGraph<double> graph;
		assignGMMsComponents(img, mask, bgdGMM, fgdGMM, compIdxs);
		learnGMMs(img, mask, compIdxs, bgdGMM, fgdGMM);

		tStart = clock();
		constructGCGraph(img, mask, bgdGMM, fgdGMM, lambda, leftW, upleftW, upW, uprightW, graph);
		printf("construcGCGraph: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);

		tStart = clock();
		estimateSegmentation(graph, mask);
		printf("estimateSegmentation: %.2fs\n", (double)(clock() - tStart) / CLOCKS_PER_SEC);
	}
}


/*#endif */
