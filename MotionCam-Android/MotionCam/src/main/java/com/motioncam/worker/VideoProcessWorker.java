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
import java.io.IOException;
import java.util.Locale;

public class VideoProcessWorker extends Worker implements NativeDngConverterListener {
    public static final String TAG = "MotionVideoCamWorker";

    public static final String INPUT_URI_KEY = "input_uri";
    public static final String INPUT_NUM_FRAMES_TO_MERGE = "num_frames_to_merge";

    public static final String OUTPUT_URI_KEY = "output_uri";

    public static final int NOTIFICATION_ID = 1;
    public static final String VIDEOS_PATH = "videos";

    private NativeProcessor mNativeProcessor;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;
    private Uri mInputUri;
    private DocumentFile mOutputDocument;

    public VideoProcessWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
    }

    private static String getNameWithoutExtension(String filename) {
        int idx = filename.lastIndexOf('.');
        return (idx == -1) ? filename : filename.substring(0, idx);
    }

    private boolean deleteVideo(Uri uri) {
        if(uri.getScheme().equalsIgnoreCase("file")) {
            return new File(uri.getPath()).delete();
        }
        else {
            return DocumentFile.fromSingleUri(getApplicationContext(), uri).delete();
        }
    }

    private String getName(Uri uri) {
        if(uri.getScheme().equalsIgnoreCase("file")) {
            return new File(uri.getPath()).getName();
        }
        else {
            return DocumentFile.fromSingleUri(getApplicationContext(), uri).getName();
        }
    }

    private void processVideo(Uri inputUri, DocumentFile output, int numFramesToMerge) throws IOException {
        mInputUri = inputUri;
        mOutputDocument = output.createDirectory(getNameWithoutExtension(getName(inputUri)));

        ContentResolver resolver = getApplicationContext().getContentResolver();

        try(ParcelFileDescriptor pfd = resolver.openFileDescriptor(inputUri, "r", null)) {
            if (pfd == null) {
                throw new NullPointerException("Invalid input");
            }

            mNativeProcessor.processVideo(pfd.detachFd(), numFramesToMerge, this);
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

        int numFramesToMerge = inputData.getInt(INPUT_NUM_FRAMES_TO_MERGE, 0);

        String inputUriString = inputData.getString(INPUT_URI_KEY);
        if(inputUriString == null) {
            Log.e(TAG, "Invalid input URI");
            return Result.failure();
        }

        String outputUriString = inputData.getString(OUTPUT_URI_KEY);
        if(outputUriString == null) {
            Log.e(TAG, "Invalid output URI");
            return Result.failure();
        }

        if(!init())
            return Result.failure();

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        Uri inputUri = Uri.parse(inputUriString);
        Uri outputUri = Uri.parse(outputUriString);

        DocumentFile output = DocumentFile.fromTreeUri(getApplicationContext(), outputUri);
        if(output == null) {
            Log.e(TAG, "Invalid output");
            return Result.failure();
        }

        try {
            Log.d(TAG, "Processing video " + inputUriString);
            processVideo(inputUri, output, numFramesToMerge);
        }
        catch (Exception e) {
            Log.e(TAG, "Failed to process " + inputUriString, e);
        }

        if(!deleteVideo(inputUri)) {
            Log.e(TAG, "Failed to delete " + inputUriString);
        }

        mNotifyManager.cancel(NOTIFICATION_ID);

        Data result = new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY, State.STATE_COMPLETED)
                .putString(State.PROGRESS_INPUT_URI_KEY, inputUriString)
                .build();

        return Result.success(result);
    }

    @Override
    public int onNeedFd(int frameNumber) {
        if(mInputUri == null)
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
                .putString(State.PROGRESS_INPUT_URI_KEY, mInputUri.toString())
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
