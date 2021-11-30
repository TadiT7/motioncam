package com.motioncam.worker;

import android.app.NotificationManager;
import android.content.ContentResolver;
import android.content.Context;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.core.app.NotificationCompat;
import androidx.documentfile.provider.DocumentFile;
import androidx.work.Data;
import androidx.work.ForegroundInfo;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

import com.motioncam.R;
import com.motioncam.processor.NativeDngConverterListener;
import com.motioncam.processor.NativeProcessor;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Locale;

public class VideoProcessWorker extends Worker implements NativeDngConverterListener {
    public static final String TAG = "MotionVideoCamWorker";

    public static final String INPUT_PATH_KEY = "input_path";
    public static final String OUTPUT_URI_KEY = "output_uri";

    public static final int NOTIFICATION_ID = 1;
    public static final String VIDEOS_PATH = "videos";

    private NativeProcessor mNativeProcessor;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;
    private File mActiveFile;
    private DocumentFile mOutputDocument;

    public VideoProcessWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
    }

    private static String getNameWithoutExtension(String filename) {
        int idx = filename.lastIndexOf('.');
        return (idx == -1) ? filename : filename.substring(0, idx);
    }

    private void processVideo(File inputFile, DocumentFile documentFile, boolean rawVideoToDng) {
        if(rawVideoToDng) {
            mActiveFile = inputFile;
            mOutputDocument = documentFile.createDirectory(getNameWithoutExtension(inputFile.getName()));

            mNativeProcessor.processVideo(inputFile.getPath(), this);
        }

        if(!inputFile.delete()) {
            Log.w(TAG, "Failed to delete " + inputFile.toString());
        }
    }

    private boolean init() {
        Context context = getApplicationContext();
        mNotifyManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ImageProcessWorker.createChannel(mNotifyManager);
        }

        mNotificationBuilder = new NotificationCompat.Builder(context, ImageProcessWorker.NOTIFICATION_CHANNEL_ID)
                .setContentTitle("Please Wait")
                .setContentText("Processing Video")
                .setTicker("MotionCam")
                .setLargeIcon(BitmapFactory.decodeResource(context.getResources(), R.mipmap.icon))
                .setSmallIcon(R.drawable.ic_processing_notification)
                .setOngoing(true);

        mNativeProcessor = new NativeProcessor();

        return true;
    }
    @NonNull
    @Override
    public Result doWork() {
        Data inputData = getInputData();

        String inputPath = inputData.getString(INPUT_PATH_KEY);
        if(inputPath == null) {
            Log.e(TAG, "Invalid input file");
            return Result.failure();
        }

        String outputUriString = inputData.getString(OUTPUT_URI_KEY);
        if(outputUriString == null) {
            Log.e(TAG, "Invalid output URI");
            return Result.failure();
        }

        File inputFile = new File(inputPath);
        if(!inputFile.exists()) {
            Log.e(TAG, "Input file " + inputPath + " does not exist");
            return Result.failure();
        }

        if(!init())
            return Result.failure();

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        Uri outputUri = Uri.parse(outputUriString);
        DocumentFile documentFile = DocumentFile.fromTreeUri(getApplicationContext(), outputUri);

        if(documentFile == null) {
            Log.e(TAG, "Invalid output URI");
            return Result.failure();
        }

        try {
            Log.d(TAG, "Processing video " + inputFile.getPath());
            processVideo(inputFile, documentFile, true);
        }
        catch (Exception e) {
            Log.e(TAG, "Failed to process " + inputFile.getPath(), e);
            if(!inputFile.delete()) {
                Log.e(TAG, "Failed to delete " + inputFile.toString());
            }
        }

        mNotifyManager.cancel(NOTIFICATION_ID);

        Data result = new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY,           State.STATE_COMPLETED)
                .putString(State.PROGRESS_INPUT_PATH_KEY,   inputPath)
                .build();

        return Result.success(result);
    }

    @Override
    public int onNeedFd(int frameNumber) {
        if(mActiveFile == null)
            return -1;

        String dngOutputName = String.format(Locale.US, "frame-%06d.dng", frameNumber);
        ContentResolver resolver = getApplicationContext().getContentResolver();
        DocumentFile outputFile = mOutputDocument.createFile("image/x-adobe-dng", dngOutputName);

        if(outputFile == null)
            return -1;

        try {
            ParcelFileDescriptor pfd = resolver.openFileDescriptor(outputFile.getUri(), "w", null);

            if(pfd != null) {
                return pfd.detachFd();
            }
        }
        catch (FileNotFoundException e) {
            e.printStackTrace();
            return -1;
        }

        return -1;
    }

    @Override
    public boolean onProgressUpdate(int progress) {
        setProgressAsync(new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY, State.STATE_PROCESSING)
                .putInt(State.PROGRESS_PROGRESS_KEY, progress)
                .build());

        mNotificationBuilder.setProgress(100, progress, false);
        mNotifyManager.notify(NOTIFICATION_ID, mNotificationBuilder.build());

        return true;
    }

    @Override
    public void onCompleted(int fd) {
    }

    @Override
    public void onCompleted() {
    }

    @Override
    public void onError(String error) {
    }
}
