package com.motioncam.worker;

import android.app.NotificationManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.core.app.NotificationCompat;
import androidx.work.Data;
import androidx.work.ForegroundInfo;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

import com.motioncam.R;
import com.motioncam.processor.NativeDngConverterListener;
import com.motioncam.processor.NativeProcessor;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class VideoProcessWorker extends Worker implements NativeDngConverterListener {
    public static final String TAG = "MotionVideoCamWorker";

    public static final int NOTIFICATION_ID = 1;
    public static final String VIDEOS_PATH = "videos";

    private NativeProcessor mNativeProcessor;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;
    private Map<Integer, Uri> mFileMap;
    private File mActiveFile;

    public VideoProcessWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
    }

    private void processVideo(File file) {
        mActiveFile = file;
        mNativeProcessor.processVideo(file.getPath(), this);

        mNotifyManager.cancel(NOTIFICATION_ID);

        if(!file.delete()) {
            Log.w(TAG, "Failed to delete " + file.toString());
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
        mFileMap = new HashMap<>();

        return true;
    }
    @NonNull
    @Override
    public Result doWork() {
        if(!init())
            return Result.failure();

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        // Find all pending files and process them
        File root = new File(getApplicationContext().getFilesDir(), VIDEOS_PATH);
        File[] pendingFiles = root.listFiles((dir, name) -> name.toLowerCase().endsWith("zip"));

        if(pendingFiles == null)
            return Result.success();

        // Process all files
        Arrays.sort(pendingFiles);

        for(File file : pendingFiles) {
            try {
                Log.d(TAG, "Processing video " + file.getPath());
                processVideo(file);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process " + file.getPath(), e);
                file.delete();
            }
        }

        return Result.success();
    }

    @Override
    public int onNeedFd(int threadNumber) {
        if(mActiveFile == null)
            return -1;

        String zipOutputName = String.format(
                Locale.US,
                "%s%02d.zip",
                ImageProcessWorker.fileNoExtension(mActiveFile.getName()), threadNumber);

        ContentResolver resolver = getApplicationContext().getContentResolver();
        Uri collection = MediaStore.Files.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
        ContentValues details = new ContentValues();

        details.put(MediaStore.Files.FileColumns.DISPLAY_NAME,  zipOutputName);
        details.put(MediaStore.Files.FileColumns.MIME_TYPE,     "application/zip");
        details.put(MediaStore.Files.FileColumns.DATE_ADDED,    System.currentTimeMillis());
        details.put(MediaStore.Files.FileColumns.DATE_TAKEN,    System.currentTimeMillis());

        String outputDirectory = Environment.DIRECTORY_DOCUMENTS
                + File.separator
                + "MotionCam"
                + File.separator
                + ImageProcessWorker.fileNoExtension(mActiveFile.getName());

        details.put(MediaStore.Files.FileColumns.RELATIVE_PATH, outputDirectory);

        Uri imageContentUri = resolver.insert(collection, details);
        if(imageContentUri == null)
            return -1;

        ParcelFileDescriptor pfd;

        try {
            pfd = resolver.openFileDescriptor(imageContentUri, "w", null);
        }
        catch (FileNotFoundException e) {
            e.printStackTrace();
            return -1;
        }

        if(pfd != null) {
            int fd = pfd.detachFd();
            mFileMap.put(fd, imageContentUri);

            return fd;
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
        mFileMap.remove(fd);
    }

    @Override
    public void onCompleted() {
    }

    @Override
    public void onError(String error) {
    }
}
