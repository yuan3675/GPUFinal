package com.selab.gpufinal;

import android.Manifest;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.support.v4.content.ContextCompat;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.view.Menu;
import android.view.MenuItem;
import android.content.Intent;

import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;

import wseemann.media.FFmpegMediaMetadataRetriever;
import org.opencv.android.Utils;
import org.opencv.core.Mat;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
        System.loadLibrary("opencv_java3");
    }
    public String fileManagerString;
    public String selectedImagePath;
    private TextView tv;
    private Button button;
    private ArrayList<Mat> matArrayList = new ArrayList<>(); // just for avoiding GC


    private static final int REQUEST_TAKE_GALLERY_VIDEO = 1;
    private static final int MY_PERMISSIONS_REQUEST = 1;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Toolbar toolbar = (Toolbar) findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        // Example of a call to a native method
        tv = findViewById(R.id.sample_text);
        button = findViewById(R.id.choose_videos_button);
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            // Permission is not granted
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, MY_PERMISSIONS_REQUEST);
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            // Permission is not granted
            requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, MY_PERMISSIONS_REQUEST);
        }
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                button.setEnabled(false);
                Intent intent = new Intent(Intent.ACTION_PICK, android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
                intent.setType("video/*");
                startActivityForResult(intent, REQUEST_TAKE_GALLERY_VIDEO);
            }
        });
    }

    public long[] getVideoFrameAddresses(String path) {
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        ArrayList<Long> frameAddresses = new ArrayList<>();
        matArrayList.clear();
        final int fps = 20;
        long interval = 1000000 / fps;




        final int thread_num = 16;
        VideoFrameGetter[] videoFrameGetters = new VideoFrameGetter[thread_num];
        Thread[] threads = new Thread[thread_num];

        try {
            /*
            retriever.setDataSource(path);
            String time = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            long timeInMilliSec = Long.parseLong(time);
            long timeInMicroSec = timeInMilliSec * 1000;
            int count = 0;
            for (int i = 0; i <= timeInMicroSec; i += interval) {
                Bitmap bitmap = retriever.getFrameAtTime(i);
                Mat mat = new Mat();
                Utils.bitmapToMat(bitmap, mat);
                frameAddresses.add(mat.getNativeObjAddr());
                matArrayList.add(mat);
                Log.i("Count", "" + ++count);
            }
            */


            retriever.setDataSource(path);
            String time = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            long timeInMilliSec = Long.parseLong(time);
            long timeInMicroSec = timeInMilliSec * 1000;
            int frame_num = (int)(timeInMicroSec / interval + 1);
            frameAddresses.ensureCapacity(frame_num);
            for (int i = 0; i < frame_num; ++i) {
                matArrayList.add(null);
                frameAddresses.add(null);
            }

            for (int i = 0; i < thread_num; ++i) {
                videoFrameGetters[i] = new VideoFrameGetter(retriever, frameAddresses, matArrayList, thread_num, i, timeInMicroSec, interval);
            }
            for (int i = 0; i < thread_num; ++i) {
                threads[i] = new Thread(videoFrameGetters[i]);
                threads[i].start();
            }
            for (int i = 0; i < thread_num; ++i) {
                threads[i].join();
            }



            retriever.release();
        } catch (IllegalArgumentException ex) {
            Log.e("ERROR!!!", "Path " + path + " illegal");
            ex.printStackTrace();
        } catch (RuntimeException ex) {
            Log.e("ERROR!!!", "RuntimeException");
            ex.printStackTrace();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        return toPrimitives(frameAddresses);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_settings) {
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK) {
            if (requestCode == REQUEST_TAKE_GALLERY_VIDEO) {
                Uri selectedImageUri = data.getData();

                // OI FILE Manager
                fileManagerString = selectedImageUri.getPath();

                // MEDIA GALLERY
                selectedImagePath = getPath(selectedImageUri);

                if (selectedImagePath != null) {
                    long[] videoFrames = getVideoFrameAddresses(selectedImagePath);
                    int result = videoTracking(videoFrames);
                    if (videoFrames.length == result) {
                        String text = "" + result;
                        tv.setText(text);
                    } else {
                        String text = "" + -1;
                        tv.setText(text);
                    }
                }
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.i("INFO", "onPause");
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.i("INFO", "onResume");
        button.setEnabled(true);
    }

    @Override
    protected void onStop() {
        super.onStop();
        Log.i("INFO", "onStop");
    }

    // UPDATED!
    public String getPath(Uri uri) {
        String[] projection = { MediaStore.Video.Media.DATA };
        Cursor cursor = getContentResolver().query(uri, projection, null, null, null);
        if (cursor != null) {
            // HERE YOU WILL GET A NULLPOINTER IF CURSOR IS NULL
            // THIS CAN BE, IF YOU USED OI FILE MANAGER FOR PICKING THE MEDIA
            int column_index = cursor
                    .getColumnIndexOrThrow(MediaStore.Video.Media.DATA);
            cursor.moveToFirst();
            return cursor.getString(column_index);
        } else
            return null;
    }

    public static long[] toPrimitives(ArrayList<Long> objects) {
        int objects_len = objects.size();

        for (int i = objects_len - 1; i >= 0; --i) {
            if (objects.get(i) != null)
                break;
            --objects_len;
        }

        long[] primitives = new long[objects_len];
        for (int i = 0; i < objects_len; i++)
            primitives[i] = objects.get(i);
        return primitives;
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native int foo();
    public native String tracking(String path);
    public native int videoTracking(long[] frameAddresses);
}
