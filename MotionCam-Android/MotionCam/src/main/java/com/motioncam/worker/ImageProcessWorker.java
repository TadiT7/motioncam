package com.motioncam.worker;

import android.app.NotificationChannel;
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
import androidx.annotation.RequiresApi;
import androidx.core.app.NotificationCompat;
import androidx.work.Data;
import androidx.work.ForegroundInfo;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

import com.motioncam.BuildConfig;
import com.motioncam.R;
import com.motioncam.model.CameraProfile;
import com.motioncam.processor.NativeProcessor;
import com.motioncam.processor.NativeProcessorProgressListener;

import org.apache.commons.io.IOUtil;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Arrays;

public class ImageProcessWorker extends Worker implements NativeProcessorProgressListener {
    public static final String TAG = "MotionCamImageWorker";

    public static final int NOTIFICATION_ID             = 0;
    public static final String NOTIFICATION_CHANNEL_ID  = "MotionCamNotification";

    public static final String PREVIEW_PATH             = "preview";

    private NativeProcessor mNativeProcessor;
    private File mPreviewDirectory;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;

    public ImageProcessWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
    }

    static public String fileNoExtension(String filename) {
        int pos = filename.lastIndexOf(".");
        if (pos > 0) {
            return  filename.substring(0, pos);
        }

        return filename;
    }

    private void saveToFiles(File inputFile, String mimeType, String relativePath) throws IOException {
        ContentResolver resolver = getApplicationContext().getContentResolver();

        Uri collection;
        ContentValues details = new ContentValues();

        details.put(MediaStore.Files.FileColumns.DISPLAY_NAME, inputFile.getName());
        details.put(MediaStore.Files.FileColumns.MIME_TYPE, mimeType);
        details.put(MediaStore.Files.FileColumns.DATE_ADDED, System.currentTimeMillis());

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            collection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);

            details.put(MediaStore.Files.FileColumns.DATE_TAKEN, System.currentTimeMillis());
            details.put(MediaStore.Files.FileColumns.RELATIVE_PATH, relativePath);
            details.put(MediaStore.Files.FileColumns.IS_PENDING, 1);
        }
        else {
            collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
        }

        Uri uri = resolver.insert(collection, details);
        if(uri == null)
            return;

        Log.d(TAG, "Writing to " + inputFile.getPath());

        try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "w", null)) {
            FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor());

            IOUtil.copy(new FileInputStream(inputFile), outStream);
        }

        details.clear();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            details.put(MediaStore.Files.FileColumns.IS_PENDING, 0);
        }

        resolver.update(uri, details, null, null);
    }

    private Uri saveToMediaStore(File inputFile, String mimeType, String relativePath) throws IOException {
        ContentResolver resolver = getApplicationContext().getContentResolver();
        Uri collection;

        ContentValues details = new ContentValues();

        details.put(MediaStore.Images.Media.DISPLAY_NAME, inputFile.getName());
        details.put(MediaStore.Images.Media.MIME_TYPE, mimeType);
        details.put(MediaStore.Images.Media.DATE_ADDED, System.currentTimeMillis());
        details.put(MediaStore.Images.Media.DATE_TAKEN, System.currentTimeMillis());

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            collection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);

            details.put(MediaStore.Images.Media.RELATIVE_PATH, relativePath);
            details.put(MediaStore.Images.Media.IS_PENDING, 1);
        }
        else {
            collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
        }

        Uri uri = resolver.insert(collection, details);
        if(uri == null)
            return null;

        Log.d(TAG, "Writing to " + inputFile.getPath());

        try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "w", null)) {
            FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor());

            IOUtil.copy(new FileInputStream(inputFile), outStream);
        }

        details.clear();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            details.put(MediaStore.Images.Media.IS_PENDING, 0);
        }

        resolver.update(uri, details, null, null);

        return uri;
    }

    private boolean processFile(boolean inMemory, File containerPath, File previewPath) throws IOException {
        String outputFileNameJpeg = fileNoExtension(containerPath.getName()) + ".jpg";
        String outputFileNameDng = fileNoExtension(containerPath.getName()) + ".dng";

        File tempFileJpeg = new File(previewPath, outputFileNameJpeg);
        File tempFileDng = new File(previewPath, outputFileNameDng);

        if (inMemory) {
            if(!mNativeProcessor.processInMemory(tempFileJpeg.getPath(), this))
                return false;
        }
        else {
            mNativeProcessor.processFile(containerPath.getPath(), tempFileJpeg.getPath(), this);
        }

        // Copy to media store
        if (BuildConfig.DEBUG && !inMemory)
            saveToFiles(containerPath, "application/zip", Environment.DIRECTORY_DOCUMENTS);

        if (tempFileDng.exists()) {
            saveToMediaStore(tempFileDng, "image/x-adobe-dng", Environment.DIRECTORY_DCIM + File.separator + "Camera");

            if(!tempFileDng.delete()) {
                Log.w(TAG, "Failed to delete " + tempFileDng.toString());
            }
        }

        Uri uri = null;

        if (tempFileJpeg.exists()) {
            uri = saveToMediaStore(tempFileJpeg, "image/jpeg", Environment.DIRECTORY_DCIM + File.separator + "Camera");
        }

        if (!inMemory) {
            if(!containerPath.delete()) {
                Log.w(TAG, "Failed to delete " + containerPath.toString());
            }
        }

        if(uri == null) {
            setProgressAsync(new Data.Builder()
                    .putInt(State.PROGRESS_STATE_KEY,       State.STATE_ERROR)
                    .putString(State.PROGRESS_ERROR_KEY,    "File not found")
                    .putInt(State.PROGRESS_PROGRESS_KEY,    100)
                    .build());
        }
        else {
            Data data = new Data.Builder()
                    .putInt(State.PROGRESS_STATE_KEY,     State.STATE_COMPLETED)
                    .putString(State.PROGRESS_URI_KEY,    uri.toString())
                    .putString(State.PROGRESS_IMAGE_PATH, tempFileJpeg.getPath())
                    .putInt(State.PROGRESS_PROGRESS_KEY,  100)
                    .build();

            setProgressAsync(data);
        }

        mNotifyManager.cancel(NOTIFICATION_ID);

        return true;
    }

    private void processInMemory(File previewDirectory) {
        // Process all in-memory requests first
        boolean moreToProcess = true;

        while(moreToProcess) {
            File inMemoryTmp = CameraProfile.generateCaptureFile(getApplicationContext());
            try {
                Log.d(TAG, "Processing in-memory container");
                moreToProcess = processFile(true, inMemoryTmp, previewDirectory);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process in-memory container", e);
                moreToProcess = false;
            }
        }
    }

    private void processOnStorage(File previewDirectory) {
        String metadataPath = CameraProfile.getRootOutputPath(getApplicationContext()).getPath();
        File root = new File(metadataPath);

        // Find all pending files and process them
        File[] pendingFiles = root.listFiles((dir, name) -> name.toLowerCase().endsWith("zip"));
        if(pendingFiles == null)
            return;

        // Process all files
        Arrays.sort(pendingFiles);

        for(File file : pendingFiles) {
            try {
                Log.d(TAG, "Processing " + file.getPath());
                processFile(false, file, previewDirectory);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process " + file.getPath(), e);
                if(!file.delete()) {
                    Log.w(TAG, "Failed to delete " + file.toString());
                }
            }
        }
    }

    private boolean init() {
        Context context = getApplicationContext();
        mNotifyManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            createChannel(mNotifyManager);
        }

        mNotificationBuilder = new NotificationCompat.Builder(context, NOTIFICATION_CHANNEL_ID)
                .setContentTitle("Please Wait")
                .setContentText("Processing Image")
                .setTicker("MotionCam")
                .setLargeIcon(BitmapFactory.decodeResource(context.getResources(), R.mipmap.icon))
                .setSmallIcon(R.drawable.ic_processing_notification)
                .setOngoing(true);

        mPreviewDirectory = new File(getApplicationContext().getFilesDir(), PREVIEW_PATH);

        // Create directory to store output for the capture preview
        if(!mPreviewDirectory.exists()) {
            if(!mPreviewDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + mPreviewDirectory);
                return false;
            }
        }

        mNativeProcessor = new NativeProcessor();

        return true;
    }

    @NonNull
    @Override
    public Result doWork() {
        if(!init())
            return Result.failure();

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        processInMemory(mPreviewDirectory);

        processOnStorage(mPreviewDirectory);

        return Result.success();
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    static public void createChannel(NotificationManager notifyManager) {
        NotificationChannel notificationChannel = new NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "MotionCam Notification",
                NotificationManager.IMPORTANCE_MIN);

        // Configure the notification channel.
        notificationChannel.setDescription("MotionCam image processing");
        notificationChannel.enableLights(false);
        notificationChannel.enableVibration(false);
        notificationChannel.setImportance(NotificationManager.IMPORTANCE_MIN);

        notifyManager.createNotificationChannel(notificationChannel);
    }

    @Override
    public String onPreviewSaved(String outputPath) {
        setProgressAsync(new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY,         State.STATE_PREVIEW_CREATED)
                .putString(State.PROGRESS_PREVIEW_PATH,   outputPath)
                .putInt(State.PROGRESS_PROGRESS_KEY,      0)
                .build());

        return "{}";
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
    public void onCompleted() {
    }

    @Override
    public void onError(String error) {
        setProgressAsync(new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY, State.STATE_ERROR)
                .putString(State.PROGRESS_ERROR_KEY, error)
                .build());
    }
}
