#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_TARGET_OPENCL_VERSION 220
#include <iostream>
#include <jni.h>
#include <string>
#include <CL/cl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include "sift.hpp"



extern VL::Sift *sift_ptr;
VL::Sift sift;

using namespace cv;
using namespace std;

std::string jstring2string(JNIEnv *env, jstring jStr);

extern "C" {

Mat pre_mat;
Mat target_mat, target_descriptor;

JNIEXPORT jstring
JNICALL
Java_com_selab_gpufinal_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */)
{
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

#define DATA_SIZE 1024
const char *KernelSource = "\n" \
"__kernel void square(                                                       \n"
"   __global float* input,                                              \n"
"   __global float* output,                                             \n"
"   const unsigned int count)                                           \n"
"{                                                                      \n"
"   int i = get_global_id(0);                                           \n"
"   if(i < count)                                                       \n"
"       output[i] = input[i] * input[i];                                \n"
"}                                                                      \n"
"\n";

JNIEXPORT jint
JNICALL
Java_com_selab_gpufinal_MainActivity_foo(
        JNIEnv *env,
        jobject /* this */)
{


    int err;                            // error code returned from api calls

    float data[DATA_SIZE];              // original data set given to device
    float results[DATA_SIZE];           // results returned from device
    unsigned int correct;               // number of correct results returned

    size_t global;                      // global domain size for our calculation
    size_t local;                       // local domain size for our calculation

    cl_device_id device_id;             // compute device id
    cl_context context;                 // compute context
    cl_command_queue commands;          // compute command queue
    cl_program program;                 // compute program
    cl_kernel kernel;                   // compute kernel
    cl_platform_id  platform_id;
    cl_uint ret_num_platforms;
    cl_uint ret_num_devices;

    cl_mem input;                       // device memory used for the input array
    cl_mem output;                      // device memory used for the output array

    // Fill our data set with random float values
    int i = 0;
    unsigned int count = DATA_SIZE;
    for(i = 0; i < count; i++)
        data[i] = rand() / (float)RAND_MAX;

    // Connect to a compute device
    //

    err = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
    err = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1, &device_id, &ret_num_devices);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to create a device group!\n");
        return 0;
    }

    // Create a compute context
    //
    context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
    if (!context)
    {
        printf("Error: Failed to create a compute context!\n");
        return 0;
    }

    // Create a command commands
    //
    commands = clCreateCommandQueue(context, device_id, 0, &err);
    if (!commands)
    {
        printf("Error: Failed to create a command commands!\n");
        return 0;
    }

    // Create the compute program from the source buffer
    //
    program = clCreateProgramWithSource(context, 1,  &KernelSource, NULL, &err);
    if (!program)
    {
        printf("Error: Failed to create compute program!\n");
        return 0;
    }

    // Build the program executable
    //
    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        size_t len;
        char buffer[2048];

        printf("Error: Failed to build program executable!\n");
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        printf("%s\n", buffer);
        return 0;
    }

    // Create the compute kernel in the program we wish to run
    //
    kernel = clCreateKernel(program, "square", &err);
    if (!kernel || err != CL_SUCCESS)
    {
        printf("Error: Failed to create compute kernel!\n");
        return 0;
    }

    // Create the input and output arrays in device memory for our calculation
    //
    input = clCreateBuffer(context,  CL_MEM_READ_ONLY,  sizeof(float) * count, NULL, NULL);
    output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float) * count, NULL, NULL);
    if (!input || !output)
    {
        printf("Error: Failed to allocate device memory!\n");
        return 0;
    }

    // Write our data set into the input array in device memory
    //
    err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float) * count, data, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to write to source array!\n");
        return 0;
    }

    // Set the arguments to our compute kernel
    //
    err = 0;
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(kernel, 2, sizeof(unsigned int), &count);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to set kernel arguments! %d\n", err);
        return 0;
    }

    // Get the maximum work group size for executing the kernel on the device
    //
    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to retrieve kernel work group info! %d\n", err);
        return 0;
    }

    // Execute the kernel over the entire range of our 1d input data set
    // using the maximum number of work group items for this device
    //
    global = count;
    err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
    if (err)
    {
        printf("Error: Failed to execute kernel!\n");
        return 0;
    }

    // Wait for the command commands to get serviced before reading back results
    //
    clFinish(commands);

    // Read back the results from the device to verify the output
    //
    err = clEnqueueReadBuffer( commands, output, CL_TRUE, 0, sizeof(float) * count, results, 0, NULL, NULL );
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to read output array! %d\n", err);
        return 0;
    }

    // Validate our results
    //
    correct = 0;
    for(i = 0; i < count; i++)
    {
        if(results[i] == data[i] * data[i])
            correct++;
    }


    // Shutdown and cleanup
    //
    clReleaseMemObject(input);
    clReleaseMemObject(output);
    clReleaseProgram(program);
    clReleaseKernel(kernel);
    clReleaseCommandQueue(commands);
    clReleaseContext(context);
    return correct == count;
}

JNIEXPORT jstring JNICALL
Java_com_selab_gpufinal_MainActivity_tracking(
        JNIEnv *env,
        jobject /* this */) {

    VideoCapture video;
    video.open(0);
    if (!video.isOpened()) {
        std::string fail = "Failed to open camera ";
        return env->NewStringUTF(fail.c_str());
    }
/*
    string::size_type pAt = filePath.find_last_of('.');                  // Find extension point
    string NAME = "/storage/emulated/0/DCIM/Camera/produceOutput.mp4";   // Form the new name with container
    int ex = CV_FOURCC('H', '2', '6', '4');     // Get Codec Type- Int form
    // Transform from int to char via Bitwise operators
    Size S = Size((int) video.get(CAP_PROP_FRAME_WIDTH),    // Acquire input size
                  (int) video.get(CAP_PROP_FRAME_HEIGHT));
    VideoWriter outputVideo;                                        // Open the output
    outputVideo.open(NAME, ex, video.get(CAP_PROP_FPS), S, true);

    if (!outputVideo.isOpened())
    {
        string fail = "Could not open the output video for write ";
        return env->NewStringUTF(fail.c_str());
    }


    Mat src, res;
    vector<Mat> spl;
    for(;;) //Show the image captured in the window and repeat
    {
        video >> src;              // read
        if (src.empty()) break;         // check if at end
        // detect target in each frame

        outputVideo.write(res); //save
    }
*/
    string success = "Done";
    return env->NewStringUTF(success.c_str());
}

}

std::string jstring2string(JNIEnv *env, jstring jStr) {
    if (!jStr)
        return "";

    const jclass stringClass = env->GetObjectClass(jStr);
    const jmethodID getBytes = env->GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
    const jbyteArray stringJbytes = (jbyteArray) env->CallObjectMethod(jStr, getBytes, env->NewStringUTF("UTF-8"));

    size_t length = (size_t) env->GetArrayLength(stringJbytes);
    jbyte* pBytes = env->GetByteArrayElements(stringJbytes, NULL);

    std::string ret = std::string((char *)pBytes, length);
    env->ReleaseByteArrayElements(stringJbytes, pBytes, JNI_ABORT);

    env->DeleteLocalRef(stringJbytes);
    env->DeleteLocalRef(stringClass);
    return ret;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_selab_gpufinal_MainActivity_videoTracking(JNIEnv *env, jobject instance,
                                                   jlongArray frame_addresses)
{
    std::vector<cv::Mat> mat_vec;

    int frame_num = env->GetArrayLength(frame_addresses);

    jlong *frames = env->GetLongArrayElements(frame_addresses, NULL);
    for (int i = 0; i < frame_num; ++i) {
        mat_vec.insert(mat_vec.end(), *(cv::Mat *)frames[i]);
    }

    std::string success_str = "success";
    std::string fail_str = "fail";




    env->ReleaseLongArrayElements(frame_addresses, frames, 0);
    return frame_num;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_selab_gpufinal_MainActivity_tracking__J(JNIEnv *env, jobject instance,
                                                 jlong frameAddresses)
{
    Mat current_mat = *(Mat *)frameAddresses;
    pre_mat = current_mat.clone();
}extern "C"
JNIEXPORT jstring JNICALL
Java_com_selab_gpufinal_CameraActivity_tracking(JNIEnv *env, jobject instance) {

    // TODO
    VideoCapture video;
    video.open(0);
    if (!video.isOpened()) {
        std::string fail = "Failed to open camera ";
        return env->NewStringUTF(fail.c_str());
    }
    string success = "Done";
    return env->NewStringUTF(success.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_selab_gpufinal_MainActivity_initVideoTracking(JNIEnv *env, jobject instance,
                                                       jlong address) {
    // TODO
    target_mat = *(Mat *) address;
}
#define SIZE 1166440
struct point {
    int x;
    int y;
};

struct points {
    struct point p[SIZE];
    int size;
} path;
extern void detectAndCompute(Mat* address, int flag);
float dist(float *a, float *b);

typedef struct coor {
    int x;
    int y;
    int neigh_num;
    struct coor **neigh_ptr;
} coor;
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_selab_gpufinal_CameraActivity_videoTracking(JNIEnv *env, jobject instance, jlong address, jlong color_addr) {

    Mat *mat_ptr = (Mat *)address;
    Mat *color_ptr = (Mat *)color_addr;
    //TODO: Detect and compute the frame
    detectAndCompute(mat_ptr, 2);

    const float threshold = 0.3f;
    //TODO: Find matches between target and the frame
    int count = 0;
    vector<coor> coors;
    for (int i = 0; i < sift.keypoints.size(); ++i) {
        float min_dist = 100000000.0f;
        int min_j = 0;
        for (int j = 0; j < sift_ptr->keypoints.size(); ++j) {
            float temp  = dist(sift.descriptors[i], sift_ptr->descriptors[j]);
            if (temp < min_dist) {
                min_dist = temp;
                min_j = j;
            }
        }
        if (min_dist < threshold) {
            ++count;
            coors.push_back((coor){sift_ptr->keypoints[min_j].ix, sift_ptr->keypoints[min_j].iy});
            for (int k = -5; k <= 5; ++k) {
                color_ptr->at<Vec4b>((sift_ptr->keypoints[min_j].iy + 1) * 2 - 1 + k,
                                     (sift_ptr->keypoints[min_j].ix + 1) * 2 - 1) = Vec4b(0, 255, 0, 255);
                color_ptr->at<Vec4b>((sift_ptr->keypoints[min_j].iy + 1) * 2 - 1,
                                     (sift_ptr->keypoints[min_j].ix + 1) * 2 - 1 + k) = Vec4b(0, 255, 0, 255);
            }
        }
    }
    if (count > 5) {
        float *dists = new float[count - 1];
        for (int i = 1; i < count; ++i) {
            dists[i - 1] = pow(coors[0].x - coors[i].x, 2) + pow(coors[0].y - coors[i].y, 2);
        }
        sort(dists, dists + count - 1);
        float dist_threshold;
        float acc = 0.0f;
        for (int i = 0; i < count / 5; ++i) {
            acc += dists[i];
        }
        dist_threshold = acc / (count / 5);

        for (int i = 0; i < count; ++i) {
            coors[i].neigh_num = 0;
            coors[i].neigh_ptr = (coor **)malloc(sizeof(*coors[i].neigh_ptr) * (count - 1));
            for (int j = 0; j < count; ++j) {
                if (i == j) {
                    continue;
                }
                if (pow(coors[i].x - coors[j].x, 2) + pow(coors[i].y - coors[j].y, 2) < dist_threshold) {
                    coors[i].neigh_ptr[coors[i].neigh_num++] = &coors[j];
                }
            }
        }
        int max_neigh_index = 0;
        int max_neigh_num = 0;
        for (int i = 0; i < count; ++i) {
            if (coors[i].neigh_num > max_neigh_num) {
                max_neigh_num = coors[i].neigh_num;
                max_neigh_index = i;
            }
        }
        int min_x, max_x, min_y, max_y;
        min_x = max_x = coors[max_neigh_index].x;
        min_y = max_y = coors[max_neigh_index].y;
        for (int i = 0; i < max_neigh_num; ++i) {
            if (coors[max_neigh_index].neigh_ptr[i]->x < min_x) {
                min_x = coors[max_neigh_index].neigh_ptr[i]->x;
            }
            if (coors[max_neigh_index].neigh_ptr[i]->x > max_x) {
                max_x = coors[max_neigh_index].neigh_ptr[i]->x;
            }
            if (coors[max_neigh_index].neigh_ptr[i]->y < min_y) {
                min_y = coors[max_neigh_index].neigh_ptr[i]->y;
            }
            if (coors[max_neigh_index].neigh_ptr[i]->y > max_y) {
                max_y = coors[max_neigh_index].neigh_ptr[i]->y;
            }
        }
        for (int i = (min_x + 1) * 2 - 1; i <= (max_x + 1) * 2 - 1; ++i) {
            color_ptr->at<Vec4b>((min_y + 1) * 2 - 1, i) = Vec4b(0, 255, 0, 255);
            color_ptr->at<Vec4b>((max_y + 1) * 2 - 1, i) = Vec4b(0, 255, 0, 255);
        }
        for (int i = (min_y + 1) * 2 - 1; i <= (max_y + 1) * 2 - 1; ++i) {
            color_ptr->at<Vec4b>(i, (min_x + 1) * 2 - 1) = Vec4b(0, 255, 0, 255);
            color_ptr->at<Vec4b>(i, (max_x + 1) * 2 - 1) = Vec4b(0, 255, 0, 255);
        }
    }
    pre_mat = *(Mat *)address;
    delete sift_ptr;
    return true;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_selab_gpufinal_CameraActivity_detectAndCompute(JNIEnv *env, jobject instance,
                                                        jlong address) {
    //TODO: Detect and compute the frame
    detectAndCompute((Mat *)address, 1);
    memcpy(&sift, sift_ptr, sizeof(sift));
    sift.descriptors = (float(*)[128])malloc(sizeof(*sift.descriptors) * sift_ptr->keypoints.size());
    for (int i = 0; i < sift_ptr->keypoints.size(); ++i) {
            for (int j = 0; j < 128; ++j) {
                sift.descriptors[i][j] = sift_ptr->descriptors[i][j];
            }
    }
    delete sift_ptr;
}


float dist(float *a, float *b)
{
    float acc = 0.0f;
    for (int k = 0; k < 128; ++k) {
        acc += pow(a[k] - b[k], 2);
    }
    return acc;
}