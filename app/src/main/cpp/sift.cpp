// file:        sift.cpp
// author:      Andrea Vedaldi
// description: Sift definition

// AUTORIGHTS
// Copyright (c) 2006 The Regents of the University of California
// All Rights Reserved.
// 
// Created by Andrea Vedaldi (UCLA VisionLab)
// 
// Permission to use, copy, modify, and distribute this software and its
// documentation for educational, research and non-profit purposes,
// without fee, and without a written agreement is hereby granted,
// provided that the above copyright notice, this paragraph and the
// following three paragraphs appear in all copies.
// 
// This software program and documentation are copyrighted by The Regents
// of the University of California. The software program and
// documentation are supplied "as is", without any accompanying services
// from The Regents. The Regents does not warrant that the operation of
// the program will be uninterrupted or error-free. The end-user
// understands that the program was developed for research purposes and
// is advised not to rely exclusively on the program for any reason.
// 
// This software embodies a method for which the following patent has
// been issued: "Method and apparatus for identifying scale invariant
// features in an image and use of same for locating an object in an
// image," David G. Lowe, US Patent 6,711,293 (March 23,
// 2004). Provisional application filed March 8, 1999. Asignee: The
// University of British Columbia.
// 
// IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY
// FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
// INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND
// ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. THE UNIVERSITY OF
// CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
// BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
// MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

// #define USE_LAPACK

#include "sift.hpp"
#include "sift-conv.tpp"

#include<algorithm>
#include<iostream>
#include<sstream>
#include<string.h>

#if defined( USE_LAPACK )
#include<Accelerate/Accelerate.h>
#endif

// clf ; hold on ;h=plotsiftframe(f) ; hh=plotsiftframe(ff(1:4,:)) ; set(h,'Color','r','LineWidth',5) ;

#define DGESV dgesv_
#define SGESV sgesv_

using namespace VL ;

class _cmnt {} cmnt ;

std::istream& operator>>(std::istream& is, _cmnt& manip)
{
  char c ;
  char b [1024] ; 
  is>>c ;
  if( c != '#' ) 
    return is.putback(c) ;
  is.getline(b,1024) ;
  return is ;
}

/** @brief Insert PGM file into stream
 **
 ** The function iserts into the stream @a os a grayscale
 ** image encoded as a PGM file. The image has @c float
 ** storage class and is assumed to be normalized in the
 ** range 0.0 -- 1.0.
 **
 ** @param os output stream.
 ** @param im pointer to image data.
 ** @param width image width.
 ** @param height image height.
 **/
std::ostream& 
VL::insertPgm(std::ostream& os, float const* im, int width, int height)
{
  os<< "P5"   << "\n"
    << width  << " "
    << height << "\n"
    << "255"  << "\n" ;
  for(int y = 0 ; y < height ; ++y) {
    for(int x = 0 ; x < width ; ++x) {
      unsigned char v = 
        (unsigned char)
        (std::max(std::min(*im++, 1.0f),0.f) * 255.0f) ;
      os << v ;
    }
  }
  return os ;
}

/** @brief Extract PGM file from stream.
 **
 ** The function extracts from the stream @a in a grayscale image
 ** encoded as a PGM file. The function fills the structure @a buffer,
 ** containing image dimensions and a pointer to the image data.
 **
 ** The image data is an array of floats and is owned by the
 ** caller. Therefore it should be freed by means of @free []
 ** buffer.data.
 **
 ** When the function cannot complete the operation it throws an
 ** instance of Exception.
 **
 ** @param in input stream.
 ** @param buffer buffer descriptor to be filled.
 **/

std::istream& 
VL::extractPgm(std::istream& in, PgmBuffer& buffer)
{
  float* im_pt ;
  int    width ;
  int    height ;
  int    maxval ;

  char c ;
  in>>c ;
  if( c != 'P') VL_THROW("Parsing error") ;
  
  bool is_ascii ;
  in>>c ;
  switch( c ) {
  case '2' : is_ascii = true ; break ;
  case '5' : is_ascii = false ; break ;
  default  : VL_THROW("File is not in PGM format") ;
  }
  
  in >> cmnt
     >> width
     >> cmnt 
     >> height
     >> cmnt
     >> maxval
     >> cmnt ;
  
  if( ! in.good() ) 
    VL_THROW("Parsing error") ;
  
  im_pt = new float [ width*height ];
  
  try {
    if( is_ascii ) {
      float* start = im_pt ;
      float* end   = start + width*height ; 
      float  norm  = float( maxval ) ;
      
      while( start != end ) {        
        int i ;
        in >> i ;
        if( ! in.good() ) VL_THROW("Parsing error") ;
        *start++ = float( i ) / norm ;        
      }
    } else {
      char* buffer = new char [width*height] ;
      in.read(buffer, width*height) ;
      if( ! in.good() ) VL_THROW("Errror reading PGM file") ;
      
      float* start = im_pt ;
      float* end   = start + width*height ; 
      unsigned char* src = reinterpret_cast<unsigned char*>(buffer) ;      
      while( start != end ) *start++ = *src++ / 255.0f ;
    }       
  } catch(...) {
    delete [] im_pt ; 
    throw ;
  }
  
  buffer.width  = width ;
  buffer.height = height ;
  buffer.data   = im_pt ;

  return in ;
}

// ===================================================================
//                                                 Low level image ops
// -------------------------------------------------------------------

// Smooth an image
void
VL::Sift::smooth
(float* dst, float* temp, 
 float const* src, int width, int height, 
 float s)
{
  // Prepare filter buffer
  int W = int( ceilf( 4.0 * s ) ) ;
  if( ! filter ) {
    filterReserved = 0 ;
  }
  
  if( filterReserved < W ) {
    filterReserved = W ;
    if( filter ) delete [] filter ;
    filter = new float [ 2* filterReserved + 1 ] ;
  }
  
  // Filter shape
  for(int j = 0 ; j < 2*W+1 ; ++j) 
    filter[j] = expf(-0.5 * (j - W)*(j - W)/(s*s)) ;
  
  // Sum to one
  normalize(filter, W) ;
  
  // Convolve
  convolve(temp, src, width, height, filter, W) ;
  convolve(dst, temp, height, width, filter, W) ;
}

// Copy an image
void
copy(float* dst, float const* src, int width, int height)
{
  memcpy(dst, src, sizeof(float)*width*height)  ;
}

// Copy an image upsampling by 2
void 
copyAndUpsampleRows
(float* dst, float const* src, int width, int height)
{
  for(int y = 0 ; y < height ; ++y) {
    float b, a ;
    b = a = *src++ ;
    for(int x = 0 ; x < width-1 ; ++x) {
      b = *src++ ;
      *dst = a ;         dst += height ;
      *dst = 0.5*(a+b) ; dst += height ;
      a = b ;
    }
    *dst = b ; dst += height ;
    *dst = b ; dst += height ;
    dst += 1 - width * 2 * height ;
  }  
}

// Copy an image downsampling by d
void 
copyAndDownsample(float* dst, float const* src, int width, int height, int d)
{
  for(int y = 0 ; y < height ; y+=d) {
    float const * srcrowp = src + y * width ;
    for(int x = 0 ; x < width ; x+=d) {
      *dst++ = *srcrowp ;
      srcrowp += d ;
    }
  }
}

// ===================================================================
//                                                     Sift(), ~Sift()
// -------------------------------------------------------------------
VL::Sift::Sift()
{

};
VL::Sift::
Sift(const float* _im_pt, int _width, int _height,
     float _sigman,
     float _sigma0,
     int _O, int _S,
     int _omin, int _smin, int _smax)
  : sigman( _sigman ), 
    sigma0( _sigma0 ),
    O( _O ),
    S( _S ),
    omin( _omin ),
    smin( _smin ),
    smax( _smax ),
    
    temp( NULL ),
    octaves( NULL ),
    filter( NULL ),
    
    threshold( 0.04f / S / 2 ),
    r( 10.0f )
{
  process(_im_pt, _width, _height) ;
}

VL::Sift::~Sift()
{
  freeBuffers() ;
  free(this->descriptors);
}

// Allocate buffers. Buffer sizes depend on the image size and the
// value of omin.
void
VL::Sift::
prepareBuffers()
{
  // compute buffer size
  int w = (omin >= 0) ? (width  >> omin) : (width  << -omin) ;
  int h = (omin >= 0) ? (height >> omin)  : (height << -omin) ;
  int size = w*h* std::max
    ((smax - smin), 2*(smax - smin - 2 + 1)) ;

  if( temp && tempReserved == size ) return ;
  
  freeBuffers() ;
  
  // allocate
  temp           = new float [ size ] ; 
  tempReserved   = size ;
  tempIsGrad     = false ;
  tempOctave     = 0 ;

  octaves = new float* [ O ] ;
  for(int o = 0 ; o < O ; ++o) {
    octaves[o] = new float [ (smax - smin + 1) * w * h ] ;
    w >>= 1 ;
    h >>= 1 ;
  }
}
  
// Free all allocated buffers
void
VL::Sift::
freeBuffers()
{
  if( filter ) {
    delete [] filter ;
  }
  filter = 0 ;

  if( octaves ) {
    for(int o = 0 ; o < O ; ++o) {
      delete [] octaves[ o ] ;
    }
    delete [] octaves ;
  }
  octaves = 0 ;
  
  if( temp ) {
    delete [] temp ;   
  }
  temp = 0  ; 
}

// ===================================================================
//                                                           process()
// -------------------------------------------------------------------

/** @brief Compute Gaussian Scale Space
 **
 ** The method computes the Gaussian scale space of the specified
 ** image. The scale space data is managed internally and can be
 ** accessed by means of getOctave() and getLevel().
 **
 ** @remark Calling this method will delete the list of keypoints
 ** constructed by detectKeypoints().
 **
 ** @param _im_pt pointer to image data.
 ** @param _width image width.
 ** @param _height image height .
 **/
void
VL::Sift::
process(const float* _im_pt, int _width, int _height)
{
  width  = _width ;
  height = _height ;
  prepareBuffers() ;
  
  float sigmak = powf(2.0f, 1.0 / S) ;
  float dsigma0 = sigma0 * sqrtf(1.0f - 1.0f / (sigmak*sigmak) ) ;
  
  // -----------------------------------------------------------------
  //                                              Prepare pyramid base
  // -----------------------------------------------------------------
  if( omin < 0 ) {
    copyAndUpsampleRows(temp,       _im_pt, width,  height  ) ;
    copyAndUpsampleRows(octaves[0], temp,   height, 2*width ) ;      

    for(int o = omin + 1 ; o < 0 ; ++o) {
      copyAndUpsampleRows(temp,       octaves[0], width  << -o,    height << -o) ;
      copyAndUpsampleRows(octaves[0], temp,       height << -o, 2*(width  << -o)) ;            
    }
  } else if( omin > 0 ) {
    copyAndDownsample(octaves[0], _im_pt, width, height, 1 << omin) ;
  } else {
    copy(octaves[0], _im_pt, width, height) ;
  }

  {
    float sa = sigma0 * powf(sigmak, smin) ; 
    float sb = sigman / powf(2.0f,   omin) ; // review this
    if( sa > sb ) {
      float sd = sqrtf( sa*sa - sb*sb ) ;
      smooth( octaves[0], temp, octaves[0], 
              getOctaveWidth(omin),
              getOctaveHeight(omin), 
              sd ) ;
    }
  }
  
  // -----------------------------------------------------------------
  //                                                      Make octaves
  // -----------------------------------------------------------------
  for(int o = omin ; o < omin+O ; ++o) {
    // Prepare octave base
    if( o > omin ) {
      int sbest = std::min(smin + S, smax) ;
      copyAndDownsample(getLevel(o,   smin ), 
                        getLevel(o-1, sbest),
                        getOctaveWidth(o-1),
                        getOctaveHeight(o-1), 2 ) ;
      float sa = sigma0 * powf(sigmak, smin      ) ;
      float sb = sigma0 * powf(sigmak, sbest - S ) ;
      if(sa > sb ) {
        float sd = sqrtf( sa*sa - sb*sb ) ;
        smooth( getLevel(o,0), temp, getLevel(o,0), 
                getOctaveWidth(o), getOctaveHeight(o),
                sd ) ;
      }
    }

    // Make other levels
    for(int s = smin+1 ; s <= smax ; ++s) {
      float sd = dsigma0 * powf(sigmak, s) ;
      smooth( getLevel(o,s), temp, getLevel(o,s-1),
              getOctaveWidth(o), getOctaveHeight(o),
              sd ) ;
    }
  }
}

/** @brief Sift detector
 **
 ** The function runs the SIFT detector on the stored Gaussian scale
 ** space (see process()). The detector consists in three steps
 **
 ** - local maxima detection;
 ** - subpixel interpolation;
 ** - weak and edge keypoint rejection.
 **
 ** The resulting keypoints are stored internally. The list of
 ** keypoints can be accessed by means of getKeypointsBegin() and
 ** getKeypointsEnd(). The list is ordered by octave, which is useful
 ** to speed-up computeKeypointOrientations() and
 ** computeKeypointDescriptor().
 **/
void
Sift::detectKeypoints()
{
  keypoints.clear() ;

  int nValidatedKeypoints = 0 ;

  // Process one octave per time
  for(int o = omin ; o < omin + O ; ++o) {
        
    int const xo = 1 ;
    int const yo = getOctaveWidth(o) ;
    int const so = getOctaveWidth(o) * getOctaveHeight(o) ;
    int const ow = getOctaveWidth(o) ;
    int const oh = getOctaveHeight(o) ;

    float xperiod = getOctaveSamplingPeriod(o) ;

    // -----------------------------------------------------------------
    //                                            Difference of Gaussian
    // -----------------------------------------------------------------
    float* dog = temp ;
    tempIsGrad = false ;
    {
      float* pt = dog ;
      for(int s = smin ; s <= smax-1 ; ++s) {
        float* srca = getLevel(o, s  ) ;
        float* srcb = getLevel(o, s+1) ;
        float* enda = srcb ;
        while( srca != enda ) {
          *pt++ = *srcb++ - *srca++ ;
        }
      }
    }
    
    // -----------------------------------------------------------------
    //                                           Find points of extremum
    // -----------------------------------------------------------------
    {
      float* pt  = dog + xo + yo + so ;
      for(int s = smin+1 ; s <= smax-2 ; ++s) {
        for(int y = 1 ; y < oh - 1 ; ++y) {
          for(int x = 1 ; x < ow - 1 ; ++x) {          
            float v = *pt ;
            
            // assert( (pt - x*xo - y*yo - (s-smin)*so) - dog == 0 ) ;
            
#define CHECK_NEIGHBORS(CMP,SGN)                    \
            ( v CMP ## = SGN 0.8 * threshold &&     \
              v CMP *(pt + xo) &&                   \
              v CMP *(pt - xo) &&                   \
              v CMP *(pt + so) &&                   \
              v CMP *(pt - so) &&                   \
              v CMP *(pt + yo) &&                   \
              v CMP *(pt - yo) &&                   \
                                                    \
              v CMP *(pt + yo + xo) &&              \
              v CMP *(pt + yo - xo) &&              \
              v CMP *(pt - yo + xo) &&              \
              v CMP *(pt - yo - xo) &&              \
                                                    \
              v CMP *(pt + xo      + so) &&         \
              v CMP *(pt - xo      + so) &&         \
              v CMP *(pt + yo      + so) &&         \
              v CMP *(pt - yo      + so) &&         \
              v CMP *(pt + yo + xo + so) &&         \
              v CMP *(pt + yo - xo + so) &&         \
              v CMP *(pt - yo + xo + so) &&         \
              v CMP *(pt - yo - xo + so) &&         \
                                                    \
              v CMP *(pt + xo      - so) &&         \
              v CMP *(pt - xo      - so) &&         \
              v CMP *(pt + yo      - so) &&         \
              v CMP *(pt - yo      - so) &&         \
              v CMP *(pt + yo + xo - so) &&         \
              v CMP *(pt + yo - xo - so) &&         \
              v CMP *(pt - yo + xo - so) &&         \
              v CMP *(pt - yo - xo - so) )
            
            if( CHECK_NEIGHBORS(>,+) || CHECK_NEIGHBORS(<,-) ) {
              
              Keypoint k ;
              k.ix = x ;
              k.iy = y ;
              k.is = s ;
              keypoints.push_back(k) ;
            }
            pt += 1 ;
          }
          pt += 2 ;
        }
        pt += 2*yo ;
      }
    }

    // -----------------------------------------------------------------
    //                                               Refine local maxima
    // -----------------------------------------------------------------
    { // refine
      KeypointsIter siter ;
      KeypointsIter diter ;
      
      for(diter = siter = keypointsBegin() + nValidatedKeypoints ; 
          siter != keypointsEnd() ; 
          ++siter) {
       
        int x = int( siter->ix ) ;
        int y = int( siter->iy ) ;
        int s = int( siter->is ) ;
                
        float Dx=0,Dy=0,Ds=0,Dxx=0,Dyy=0,Dss=0,Dxy=0,Dxs=0,Dys=0 ;
        float  b [3] ;
        float* pt ;
        int dx = 0 ;
        int dy = 0 ;

        // must be exec. at least once
        for(int iter = 0 ; iter < 5 ; ++iter) {

          float A[3*3] ;          
          long int ipiv[3] ;
          long int n = 3 ;
          long int one = 1 ;
          long int info = 0 ;

          x += dx ;
          y += dy ;

          pt = dog 
            + xo * x
            + yo * y
            + so * (s - smin) ;

#define at(dx,dy,ds) (*( pt + (dx)*xo + (dy)*yo + (ds)*so))
#define Aat(i,j)     (A[(i)+(j)*3])    
          
          /* Compute the gradient. */
          Dx = 0.5 * (at(+1,0,0) - at(-1,0,0)) ;
          Dy = 0.5 * (at(0,+1,0) - at(0,-1,0));
          Ds = 0.5 * (at(0,0,+1) - at(0,0,-1)) ;
          
          /* Compute the Hessian. */
          Dxx = (at(+1,0,0) + at(-1,0,0) - 2.0 * at(0,0,0)) ;
          Dyy = (at(0,+1,0) + at(0,-1,0) - 2.0 * at(0,0,0)) ;
          Dss = (at(0,0,+1) + at(0,0,-1) - 2.0 * at(0,0,0)) ;
          
          Dxy = 0.25 * ( at(+1,+1,0) + at(-1,-1,0) - at(-1,+1,0) - at(+1,-1,0) ) ;
          Dxs = 0.25 * ( at(+1,0,+1) + at(-1,0,-1) - at(-1,0,+1) - at(+1,0,-1) ) ;
          Dys = 0.25 * ( at(0,+1,+1) + at(0,-1,-1) - at(0,-1,+1) - at(0,+1,-1) ) ;
          
          /* Solve linear system. */
          Aat(0,0) = Dxx ;
          Aat(1,1) = Dyy ;
          Aat(2,2) = Dss ;
          Aat(0,1) = Aat(1,0) = Dxy ;
          Aat(0,2) = Aat(2,0) = Dxs ;
          Aat(1,2) = Aat(2,1) = Dys ;
          
          b[0] = - Dx ;
          b[1] = - Dy ;
          b[2] = - Ds ;
          

#ifdef USE_LAPACK
          SGESV (&n, &one, A, &n, ipiv, b, &n, &info) ;
#else
          // Gauss elimination          
          for(int j = 0 ; j < 3 ; ++j) {

            // look for leading pivot
            float maxa = 0 ;
            float maxabsa = 0 ;
            int   maxi = -1 ;
            int i ;
            for(i = j ; i < 3 ; ++i) {
              float a    = Aat(i,j) ;
              float absa = fabsf( a ) ;
              if ( absa > maxabsa ) {
                maxa    = a ;
                maxabsa = absa ;
                maxi    = i ;
              }
            }

            // singular?
            if( maxabsa < 1e-10f ) {
              b[0] = 0 ;
              b[1] = 0 ;
              b[2] = 0 ;
              break ;
            }

            i = maxi ;

            // swap j-th row with i-th row and
            // normalize j-th row
            for(int jj = j ; jj < 3 ; ++jj) {
              std::swap( Aat(j,jj) , Aat(i,jj) ) ;
              Aat(j,jj) /= maxa ;
            }
            std::swap( b[j], b[i] ) ;
            b[j] /= maxa ;

            // elimination
            for(int ii = j+1 ; ii < 3 ; ++ii) {
              float x = Aat(ii,j) ;
              for(int jj = j ; jj < 3 ; ++jj) {
                Aat(ii,jj) -= x * Aat(j,jj) ;                
              }
              b[ii] -= x * b[j] ;
            }
          }

          // backward substitution
          for(int i = 2 ; i > 0 ; --i) {
            float x = b[i] ;
            for(int ii = i-1 ; ii >= 0 ; --ii) {
              b[ii] -= x * Aat(ii,i) ;
            }
          }
#endif
          
          /* If the translation of the keypoint is big, move the keypoint
           * and re-iterate the computation. Otherwise we are all set.
           */
          dx= ((b[0] >  0.6 && x < ow-2) ?  1 : 0 )
            + ((b[0] < -0.6 && x > 1   ) ? -1 : 0 ) ;
          
          dy= ((b[1] >  0.6 && y < oh-2) ?  1 : 0 )
            + ((b[1] < -0.6 && y > 1   ) ? -1 : 0 ) ;

          /*          
          std::cout<<x<<","<<y<<"="<<at(0,0,0)
                   <<"("
                   <<at(0,0,0)+0.5 * (Dx * b[0] + Dy * b[1] + Ds * b[2])<<")"
                   <<" "<<std::flush ; 
          */

          if( dx == 0 && dy == 0 ) break ;
        }
        
        /* std::cout<<std::endl ; */

        {
          float val = at(0,0,0) + 0.5 * (Dx * b[0] + Dy * b[1] + Ds * b[2]) ; 
          float score = (Dxx+Dyy)*(Dxx+Dyy) / (Dxx*Dyy - Dxy*Dxy) ; 
          float xn = x + b[0] ;
          float yn = y + b[1] ;
          float sn = s + b[2] ;
          
          if(fabs(val) > threshold &&
             score < (r+1)*(r+1)/r && 
             score >= 0 &&
             fabs(b[0]) < 1.5 &&
             fabs(b[1]) < 1.5 &&
             fabs(b[2]) < 1.5 &&
             xn >= 0    &&
             xn <= ow-1 &&
             yn >= 0    &&
             yn <= oh-1 &&
             sn >= smin &&
             sn <= smax ) {
            
            diter->o  = o ;
       
            diter->ix = x ;
            diter->iy = y ;
            diter->is = s ;

            diter->x = xn * xperiod ; 
            diter->y = yn * xperiod ; 
            diter->s = sn ;

            diter->sigma = getScaleFromIndex(o,sn) ;

            ++diter ;
          }
        }
      } // next candidate keypoint

      // prepare for next octave
      keypoints.resize( diter - keypoints.begin() ) ;
      nValidatedKeypoints = keypoints.size() ;
    } // refine block

  } // next octave
}

// ===================================================================
//                                       computeKeypointOrientations()
// -------------------------------------------------------------------

// Compute the gradient modulus and orientation for the specified
// octave
void
Sift::prepareGrad(int o)
{ 
  int const ow = getOctaveWidth(o) ;
  int const oh = getOctaveHeight(o) ;
  int const xo = 1 ;
  int const yo = ow ;
  int const so = oh*ow ;

  if( ! tempIsGrad || tempOctave != o ) {

    // compute dx/dy
    for(int s = smin+1 ; s <= smax-1 ; ++s) {
      for(int y = 1 ; y < oh-1 ; ++y ) {
        float* src  = getLevel(o, s) + xo + yo*y ;        
        float* end  = src + ow - 1 ;
        float* grad = 2 * (xo + yo*y + (s - smin -1)*so) + temp ;
        while(src != end) {
          float Gx = 0.5 * ( *(src+xo) - *(src-xo) ) ;
          float Gy = 0.5 * ( *(src+yo) - *(src-yo) ) ;
          float m = sqrtf( Gx*Gx + Gy*Gy ) ;
          float t = fmodf( atan2( Gy, Gx ) + 2*M_PI, 2*M_PI ) ;
          *grad++ = m ;
          *grad++ = t ;
          ++src ;
        }
      }
    }
  }
  tempIsGrad = true ;
  tempOctave = o ;
}


/** @brief Compute orientation(s) of a keypoint
 **
 ** The function computes the orientation of the specified keypoint.
 ** The function returns up to four different orientations, obtained
 ** as strong peaks of the histogram of gradient orientations (a
 ** keypoint can theoretically generate more than four orientations,
 ** but this is very unlikely).
 **
 ** @remark The function needs to compute the gradient modululs and
 ** orientation of the Gaussian scale space octave to which the
 ** keypoint belongs. The result is cached, but discarded if different
 ** octaves are visited. Thereofre it is much quicker to evaluate the
 ** keypoints in their natural octave order.
 **
 ** @param angles buffers to store the resulting angles.
 ** @param keypoint keypoint to process.
 ** @return number of orientations found.
 **/
int
Sift::computeKeypointOrientations(float angles [4], 
                                  Keypoint keypoint)
{
  int p ;

  float const winFactor = 1.5 ;
  int const   nbins = 36 ;
  float hist [nbins] ;

  // octave
  int o = keypoint.o ;
  prepareGrad(o) ;
  float xperiod = getOctaveSamplingPeriod(o) ;

  // offsets to move in Gaussian scale space octave
  const int ow = getOctaveWidth(o) ;
  const int oh = getOctaveHeight(o) ;
  const int xo = 2 ;
  const int yo = xo * ow ;
  const int so = yo * oh ;

  // keypoint fractional geometry
  float x     = keypoint.x / xperiod ;
  float y     = keypoint.y / xperiod ;
  float s     = keypoint.s ;
  float sigma = keypoint.sigma / xperiod ;
  
  // shall we use keypoints.ix,iy,is here?
  int xi = ((int) (x+0.5)) ; 
  int yi = ((int) (y+0.5)) ;
  int si = keypoint.is ;
  
  float const sigmaw = winFactor * sigma ;
  int W = (int) ceil(3.0 * sigmaw) ;
  
  // make sure within bounds
  if(xi < 0      || 
     xi > ow-1   || 
     yi < 0      || 
     yi > oh-1   || 
     si < smin+1 || 
     si > smax-1 ) {
    std::cerr<<"."<<std::endl ;
    return 0 ;
  }

  // clear histogram
  std::fill(hist, hist + nbins, 0) ;

  // fill histogram
  float* pt = temp + xi * xo + yi * yo + si * (so - smin -1) ;

#undef at
#define at(dx,dy) (*(pt + (dx)*xo + (dy)*yo))

  for(int ys = std::max(-W, 1-yi) ; ys <= std::min(+W, oh -2 -yi) ; ++ys) {
    for(int xs = std::max(-W, 1-xi) ; xs <= std::min(+W, ow -2 -xi) ; ++xs) {
      
      float dx = xi + xs - x;
      float dy = yi + ys - y;
      float r2 = dx*dx + dy*dy ;

      // limit to a circular window
      if(r2 > W*W+0.5) continue ;

      float wgt = expf( - r2 / (2*sigmaw*sigmaw) ) ;
      float mod = *(pt + xs*xo + ys*yo) ;
      float ang = *(pt + xs*xo + ys*yo + 1) ;

      int bin = (int) floor( nbins * ang / (2*M_PI) ) ;
      hist[bin] += mod * wgt ;        
    }
  }
  
  // smooth histogram
  for (int iter = 0; iter < 6; iter++) {
    double prev;
    prev = hist[nbins-1];
    for (int i = 0; i < nbins; i++) {
      float newh = (prev + hist[i] + hist[(i+1) % nbins]) / 3.0;
      prev = hist[i] ;
      hist[i] = newh ;
    }
  }
  
  // find histogram maximum
  float maxh = * std::max_element(hist, hist + nbins) ;

  // find peaks within 80% from max
  int nangles = 0 ;
  for(int i = 0 ; i < nbins ; ++i) {
    float h0 = hist [i] ;
    float hm = hist [(i-1+nbins) % nbins] ;
    float hp = hist [(i+1+nbins) % nbins] ;
    
    // is peak?
    if( h0 > 0.8*maxh && h0 > hm && h0 > hp ) {
      
      // quadratic interpolation
      float di = -0.5 * (hp-hm) / (hp+hm-2*h0) ; 
      float th = 2*M_PI*(i+di+0.5)/nbins ;      
      angles [ nangles++ ] = th ;
    }
  }
  return nangles ;
}

// ===================================================================
//                                         computeKeypointDescriptor()
// -------------------------------------------------------------------

/** Fast fmodf for 2*PI
 **/
/*inline*/
float fast_mod(float th)
{
  while(th < 0) th += 2*M_PI ;
  while(th > 2*M_PI) th -= 2*M_PI ;
  return th ;
}

/** Fast floor. Equivalent to (int) floor(x)
 **/
/*inline*/
int fast_floor(float x)
{
  return (int)( x - ((x>=0)?0:1) ) ; 
}

/** Normalizes in norm L_2 a descriptor.
 **/
void
normalize_histogram(float* L_begin, float* L_end)
{
  float* L_iter ;
  float norm=0.0 ;

  for(L_iter = L_begin; L_iter != L_end ; ++L_iter)
    norm += (*L_iter) * (*L_iter) ;

  norm = sqrtf(norm) ;
  /*  mexPrintf("%f\n",norm) ;*/

  for(L_iter = L_begin; L_iter != L_end ; ++L_iter)
    *L_iter /= norm ;
}

/** @brief SIFT descriptor
 **
 ** The function computes the descriptor of the keypoint @a keypoint.
 ** The function fills the buffer @a descr_pt which must be large
 ** enough. The funciton uses @a angle0 as rotation of the keypoint.
 ** By calling the function multiple times, different orientations can
 ** be evaluated.
 **
 ** @remark The function needs to compute the gradient modululs and
 ** orientation of the Gaussian scale space octave to which the
 ** keypoint belongs. The result is cached, but discarded if different
 ** octaves are visited. Thereofre it is much quicker to evaluate the
 ** keypoints in their natural octave order.
 **/
void
Sift::computeKeypointDescriptor(float* descr_pt,
Keypoint keypoint, 
float angle0)
{

  /* The SIFT descriptor is a  three dimensional histogram of the position
   * and orientation of the gradient.  There are NBP bins for each spatial
   * dimesions and NBO  bins for the orientation dimesion,  for a total of
   * NBP x NBP x NBO bins.
   *
   * The support  of each  spatial bin  has an extension  of SBP  = 3sigma
   * pixels, where sigma is the scale  of the keypoint.  Thus all the bins
   * together have a  support SBP x NBP pixels wide  . Since weighting and
   * interpolation of  pixel is used, another  half bin is  needed at both
   * ends of  the extension. Therefore, we  need a square window  of SBP x
   * (NBP + 1) pixels. Finally, since the patch can be arbitrarly rotated,
   * we need to consider  a window 2W += sqrt(2) x SBP  x (NBP + 1) pixels
   * wide.
   */      

  // octave
  int o = keypoint.o ;
  prepareGrad(o) ;
  float xperiod = getOctaveSamplingPeriod(o) ;

  // offsets to move in Gaussian scale space octave
  const int ow = getOctaveWidth(o) ;
  const int oh = getOctaveHeight(o) ;
  const int xo = 2 ;
  const int yo = xo * ow ;
  const int so = yo * oh ;

  // keypoint fractional geometry
  float x     = keypoint.x / xperiod;
  float y     = keypoint.y / xperiod ;
  float s     = keypoint.s ;
  float sigma = keypoint.sigma / xperiod ;

  float st0   = sinf( angle0 ) ;
  float ct0   = cosf( angle0 ) ;
  
  // shall we use keypoints.ix,iy,is here?
  int xi = ((int) (x+0.5)) ; 
  int yi = ((int) (y+0.5)) ;
  int si = keypoint.is ;

  const float magnif = 3.0f ;
  const int   NBO = 8 ;
  const int   NBP = 4 ;
  const float SBP = magnif * sigma ;
  const int   W = (int) floorf(sqrtf(2.0f) * SBP * (NBP + 1) / 2.0 + 0.5) ;
  
  /* Offsets to move in the descriptor. */
  /* Use Lowe's convention. */
  const int binto = 1 ;
  const int binyo = NBO * NBP ;
  const int binxo = NBO;
  const int bino  = NBO * NBP * NBP ;
  
  int bin ;
  int dxi ;
  int dyi ;
  
  // check bounds
  if(xi < 0      || 
     xi > ow-1   || 
     yi < 0      || 
     yi > oh-1   ||
     si < smin+1 ||
     si > smax-1 )
        return ;

  std::fill( descr_pt, descr_pt + bino, 0 ) ;

  /* Center the scale space and the descriptor on the current keypoint. 
   * Note that dpt is pointing to the bin of center (SBP/2,SBP/2,0).
   */
  float const * pt = temp + xi*xo + yi*yo + (si - smin - 1)*so ;
  float *      dpt = descr_pt + (NBP/2) * binyo + (NBP/2) * binxo ;
     
#define atd(dbinx,dbiny,dbint) (*(dpt + (dbint)*binto + (dbiny)*binyo + (dbinx)*binxo))
      
  /*
   * Process each pixel in the window and in the (1,1)-(M-1,N-1)
   * rectangle.
   */
  for(int dyi = std::max(-W, 1-yi) ; dyi <= std::min(+W, oh-2-yi) ; ++dyi) {
    for(int dxi = std::max(-W, 1-xi) ; dxi <= std::min(+W, ow-2-xi) ; ++dxi) {
      
      // retireve 
      float mod   = *(pt + dxi*xo + dyi*yo + 0 ) ;
      float angle = *(pt + dxi*xo + dyi*yo + 1 ) ;
      float theta = fast_mod(-angle + angle0) ; // lowe compatible ?
      
      // fractional displacement
      float dx = xi + dxi - x;
      float dy = yi + dyi - y;
      
      // get the displacement normalized w.r.t. the keypoint
      // orientation and extension.
      float nx = ( ct0 * dx + st0 * dy) / SBP ;
      float ny = (-st0 * dx + ct0 * dy) / SBP ; 
      float nt = NBO * theta / (2*M_PI) ;
      
      // Get the gaussian weight of the sample. The gaussian window
      // has a standard deviation of NBP/2. Note that dx and dy are in
      // the normalized frame, so that -NBP/2 <= dx <= NBP/2.
      float const wsigma = NBP/2 ;
      float win =  expf(-(nx*nx + ny*ny)/(2.0 * wsigma * wsigma)) ;
      
      // The sample will be distributed in 8 adijacient bins. 
      // We start from the ``lower-left'' bin.
      int binx = fast_floor( nx - 0.5 ) ;
      int biny = fast_floor( ny - 0.5 ) ;
      int bint = fast_floor( nt ) ;
      float rbinx = nx - (binx+0.5) ;
      float rbiny = ny - (biny+0.5) ;
      float rbint = nt - bint ;
      int dbinx ;
      int dbiny ;
      int dbint ;

      // Distribute the current sample into the 8 adijacient bins. */
      for(dbinx = 0 ; dbinx < 2 ; ++dbinx) {
        for(dbiny = 0 ; dbiny < 2 ; ++dbiny) {
          for(dbint = 0 ; dbint < 2 ; ++dbint) {
            
            if( binx+dbinx >= -(NBP/2) &&
                binx+dbinx <   (NBP/2) &&
                biny+dbiny >= -(NBP/2) &&
                biny+dbiny <   (NBP/2) ) {
              float weight = win 
                * mod 
                * fabsf(1 - dbinx - rbinx)
                * fabsf(1 - dbiny - rbiny)
                * fabsf(1 - dbint - rbint) ;
              
              atd(binx+dbinx, biny+dbiny, (bint+dbint) % NBO) += weight ;
            }
          }            
        }
      }
    }  
  }
  
  /* Normalize the histogram to L2 unit length. */        
  normalize_histogram(descr_pt, descr_pt + bino) ;
  
  /* Truncate at 0.2. */
  for(bin = 0; bin < bino ; ++bin) {
    if (descr_pt[bin] > 0.2) descr_pt[bin] = 0.2;
  }
  
  /* Normalize again. */
  normalize_histogram(descr_pt, descr_pt + bino) ;
}
