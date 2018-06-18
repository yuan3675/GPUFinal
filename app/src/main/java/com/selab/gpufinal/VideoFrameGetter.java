package com.selab.gpufinal;

import android.graphics.Bitmap;
import android.media.MediaMetadataRetriever;
import android.util.Log;

import org.opencv.android.Utils;
import org.opencv.core.Mat;

import java.util.ArrayList;

import wseemann.media.FFmpegMediaMetadataRetriever;

public class VideoFrameGetter implements Runnable {
    private MediaMetadataRetriever retriever;
    private ArrayList<Long> frameAddresses;
    private ArrayList<Mat> matArrayList;
    private int thread_num;
    private int index;
    private  long timeInMicroSec;
    private long interval;

    public VideoFrameGetter(MediaMetadataRetriever retriever, ArrayList<Long> frameAddresses, ArrayList<Mat> matArrayList, int thread_num, int index, long timeInMicroSec, long interval) {
        this.retriever = retriever;
        this.frameAddresses = frameAddresses;
        this.matArrayList = matArrayList;
        this.thread_num = thread_num;
        this.index = index;
        this.timeInMicroSec = timeInMicroSec;
        this.interval = interval;
    }

    @Override
    public void run() {
        long thread_interval = interval * thread_num;
        int count = index;
        for (long i = index * interval; i <= timeInMicroSec; i += thread_interval) {
            Bitmap bitmap = retriever.getFrameAtTime(i);
            Mat mat = new Mat();
            Utils.bitmapToMat(bitmap, mat);
            frameAddresses.set(count, mat.getNativeObjAddr());
            matArrayList.set(count ,mat);
            Log.i("Count", "" + count);
            count += thread_num;
        }
    }
}
