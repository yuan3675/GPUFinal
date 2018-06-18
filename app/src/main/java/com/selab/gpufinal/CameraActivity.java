package com.selab.gpufinal;

import android.app.Activity;
import android.graphics.Bitmap;
import android.hardware.Camera;
import android.hardware.camera2.*;
import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.Nullable;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.TextView;

import org.jcodec.api.android.AndroidSequenceEncoder;
import org.opencv.android.BaseLoaderCallback;
import org.opencv.android.CameraBridgeViewBase;
import org.opencv.android.LoaderCallbackInterface;
import org.opencv.android.OpenCVLoader;
import org.opencv.android.Utils;
import org.opencv.core.Core;
import org.opencv.core.CvType;
import org.opencv.core.Mat;
import org.opencv.core.Size;
import org.opencv.imgproc.Imgproc;
import org.opencv.videoio.VideoCapture;
import org.opencv.videoio.VideoWriter;

import java.io.File;
import java.io.IOException;

public class CameraActivity extends Activity implements CameraBridgeViewBase.CvCameraViewListener2, View.OnClickListener{

    private static final String  TAG = "CameraFrameAccess";
    private VideoWriter videoWriter;
    private AndroidSequenceEncoder sequenceEncoder = null;

    protected CameraBridgeViewBase cameraPreview;
    protected Mat mRgba;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_camera);

        cameraPreview = (CameraBridgeViewBase) findViewById(R.id.sample_test_camera_view);
        cameraPreview.setMaxFrameSize(1920, 1080);
        cameraPreview.setCvCameraViewListener(this);
        cameraPreview.setOnClickListener(this);

        videoWriter = new VideoWriter();
        Size size = new Size(1080, 1080);
        videoWriter.open(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS) + "/Test.avi", VideoWriter.fourcc('M', 'J', 'P', 'G'), 16.9, size);
        if (!videoWriter.isOpened()) {
            videoWriter = null;
            Log.e("Fail", "not open");
        }
    }

    public native String tracking();

    protected BaseLoaderCallback mLoaderCallback = new BaseLoaderCallback(this) {
        @Override
        public void onManagerConnected(int status) {
            switch (status) {
                case LoaderCallbackInterface.SUCCESS:
                {
                    Log.i(TAG, "OpenCV loaded successfully");
//                    mOpenCvCameraView.enableView();
//                    mOpenCvCameraView.setOnTouchListener(ColorRegionDetectionActivity.this);
                    cameraPreview.enableView();
                } break;
                default:
                {
                    super.onManagerConnected(status);
                } break;
            }
        }
    };

    @Override
    public void onCameraViewStarted(int width, int height) {
        // TODO Auto-generated method stub
        mRgba =  new Mat(height, width, CvType.CV_8UC4);
    }

    @Override
    public void onCameraViewStopped() {
        // TODO Auto-generated method stub
        mRgba.release();
        videoWriter.release();
    }

    @Override
    public Mat onCameraFrame(CameraBridgeViewBase.CvCameraViewFrame inputFrame) {
        // TODO Auto-generated method stub
        mRgba = inputFrame.rgba();
        Core.rotate(mRgba, mRgba, Core.ROTATE_90_CLOCKWISE);
        Mat mRgb = new Mat();
        Imgproc.cvtColor(mRgba, mRgb, Imgproc.COLOR_BGRA2BGR);
        videoWriter.write(mRgb);
        return mRgba;
    }

    @Override
    protected void onDestroy() {
        // TODO Auto-generated method stub
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        // TODO Auto-generated method stub
        super.onPause();
        if(cameraPreview != null){
            cameraPreview.disableView();
        }
    }

    @Override
    protected void onResume() {
        // TODO Auto-generated method stub
        super.onResume();
        OpenCVLoader.initAsync(OpenCVLoader.OPENCV_VERSION_2_4_3, this, mLoaderCallback);
    }

    @Override
    public void onClick(View view) {
        this.finish();
    }
}
