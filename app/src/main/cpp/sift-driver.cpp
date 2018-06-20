// file:        sift-driver.cpp
// author:      Andrea Vedaldi
// description: SIFT command line utility implementation

// AUTORIGTHS

#include "sift.hpp"

#include<string>
#include<iostream>
#include<fstream>
#include<sstream>
#include<algorithm>
#include<limits>
#include <opencv2/opencv.hpp>
#include <CL/cl.h>

using namespace cv;

extern "C" {
#include<getopt.h>
}

using namespace std ;
VL::Sift *sift_ptr;
void detectAndCompute(Mat* address, int flag)
{
  int first    = -1 ;
  int octaves  = -1 ; // Number of octaves cannot be smaller than one.
  int levels   = 3 ; // Number of levels cannot be smaller than one.
  int nodescr  = 0 ;
  int noorient = 0 ;
  int savegss  = 0 ;
  int verbose  = 0 ;
  //std::string prefix("") ;

  static struct option longopts[] = {
    { "verbose",         no_argument,            NULL,              'v' },
    { "help",            no_argument,            NULL,              'h' },
    { "output",          required_argument,      NULL,              'o' },
    { "first-octave",    required_argument,      NULL,              'f' },
    { "octaves",         required_argument,      NULL,              'O' },
    { "levels",          required_argument,      NULL,              'S' },
    { "no-descriptors",  no_argument,            &nodescr,          1   },
    { "no-orientations", no_argument,            &noorient,         1   },
    { "save-gss",        no_argument,            &savegss,          1   },
    { NULL,              0,                      NULL,              0   }
  };


  // Resize the frame
    Mat resizeFrame;
    Size new_size(address->cols / flag, address->rows / flag);
    resize(*address, resizeFrame, new_size);

    VL::PgmBuffer buffer ;
    //TODO: Need to implement a function converts gray scale Mat to Pgmbuffer
    buffer.width = (int) resizeFrame.cols;
    buffer.height = (int) resizeFrame.rows;
    buffer.data = new float[buffer.width * buffer.height];
    for (int i = 0; i < buffer.height; ++i) {
        for (int j = 0; j < buffer.width; ++j) {
            buffer.data[i * buffer.width + j] = (float)resizeFrame.ptr<unsigned char>(i)[j] / 255.0f;
        }
    }
/*
    FILE *fp = fopen("/storage/emulated/0/Download/mat.txt", "w");
    fprintf(fp, "%d\n", buffer.height);
    fprintf(fp, "%d\n", buffer.width);
    for (int i = 0; i < buffer.height; ++i) {
        for (int j = 0; j < buffer.width; ++j) {
            fprintf(fp, "%.2f ", buffer.data[i * buffer.width + j]);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
*/
  //std::string name(argv[0]) ;
    
  // get prefix
  // ---------------------------------------------------------------
  //                                                  Load PGM image
  // ---------------------------------------------------------------

  // ---------------------------------------------------------------
  //                                            Gaussian scale space
  // ---------------------------------------------------------------

  int         O      = octaves ;
  int const   S      = levels ;
  int const   omin   = first ;
  float const sigman = .5 ;
  float const sigma0 = 1.6 * powf(2.0f, 1.0f / S) ;

  // autoselect of number of octaves?
  if( O < 1 ) {
    O = std::max( int(floor(log2(std::min(buffer.width,buffer.height))) - omin -4), 1 ) ;
  }
    
  sift_ptr = new VL::Sift( buffer.data, buffer.width, buffer.height,
                 sigman, sigma0,
                 O, S,
                 omin, -1, S+1 ) ;

  // ---------------------------------------------------------------
  //                                       Save Gaussian scale space (Not needed)
  // ---------------------------------------------------------------
  // ---------------------------------------------------------------
  //                                                   SIFT detector
  // ---------------------------------------------------------------

  sift_ptr->detectKeypoints();
  // Don't know how to get all keypoints

  // ---------------------------------------------------------------
  //                               SIFT orientations and descriptors
  // ---------------------------------------------------------------


    int keypoints_size = sift_ptr->keypoints.size();
    sift_ptr->descriptors = (float(*)[128])malloc(sizeof(*sift_ptr->descriptors) * keypoints_size);
    // Can be parallel


    for(int i = 0; i < keypoints_size; ++i) {
        sift_ptr->computeKeypointDescriptor(sift_ptr->descriptors[i], sift_ptr->keypoints[i], 0.0f);
    }
}


