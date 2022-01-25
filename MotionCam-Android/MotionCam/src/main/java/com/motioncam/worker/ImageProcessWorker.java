package com.motioncam.worker;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.BitmapFactory;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.RequiresApi;
import androidx.core.app.NotificationCompat;
import androidx.core.content.FileProvider;
import androidx.work.Data;
import androidx.work.ForegroundInfo;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

import com.motioncam.BuildConfig;
import com.motioncam.R;
import com.motioncam.model.CameraProfile;
import com.motioncam.processor.NativeProcessor;
import com.motioncam.processor.NativeProcessorProgressListener;

import org.apache.commons.io.FileUtils;
import org.apache.commons.io.IOUtil;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class ImageProcessWorker extends Worker implements NativeProcessorProgressListener {
    public static final String TAG = "MotionCamImageWorker";

    public static final int NOTIFICATION_ID             = 0;
    public static final String NOTIFICATION_CHANNEL_ID  = "MotionCamNotification";

    public static final String PREVIEW_PATH             = "preview";
    public static final String AUTHORITY                = "com.motioncam.provider";

    private NativeProcessor mNativeProcessor;
    private File mPreviewDirectory;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;

    static class WorkResult {
        final String error;
        final String outputPath;
        final Uri outputUri;

        WorkResult(String outputPath, Uri outputUri, String error)
        {
            this.outputPath = outputPath;
            this.outputUri = outputUri;
            this.error = error;
        }
    }

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
            collection = MediaStore.Files.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);

            details.put(MediaStore.Files.FileColumns.DATE_TAKEN, System.currentTimeMillis());
            details.put(MediaStore.Files.FileColumns.RELATIVE_PATH, relativePath);
            details.put(MediaStore.Files.FileColumns.IS_PENDING, 1);

            Uri uri = resolver.insert(collection, details);
            if(uri == null)
                return;

            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "w", null)) {
                FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor());

                IOUtil.copy(new FileInputStream(inputFile), outStream);
            }

            details.clear();
            details.put(MediaStore.Files.FileColumns.IS_PENDING, 0);

            resolver.update(uri, details, null, null);
        }
        else {
            File filesPath  = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS), "MotionCam");
            File outputPath = new File(filesPath, inputFile.getName());

            Log.d(TAG, "Writing to " + outputPath.getPath());

            if(!filesPath.exists() && !filesPath.mkdirs()) {
                Log.e(TAG, "Failed to create " + filesPath.toString());
                return;
            }

            FileUtils.copyFile(inputFile, outputPath);
        }
    }

    private Uri saveToMediaStore(File inputFile, String mimeType, String relativePath) throws IOException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ContentResolver resolver = getApplicationContext().getContentResolver();
            Uri collection;

            ContentValues details = new ContentValues();

            details.put(MediaStore.Images.Media.DISPLAY_NAME, inputFile.getName());
            details.put(MediaStore.Images.Media.MIME_TYPE, mimeType);
            details.put(MediaStore.Images.Media.DATE_ADDED, System.currentTimeMillis());
            details.put(MediaStore.Images.Media.DATE_TAKEN, System.currentTimeMillis());

            collection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);

            details.put(MediaStore.Images.Media.RELATIVE_PATH, relativePath);
            details.put(MediaStore.Images.Media.IS_PENDING, 1);

            Uri uri = resolver.insert(collection, details);
            if(uri == null)
                return null;

            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "w", null);
                 InputStream inStream = new FileInputStream(inputFile);
                 FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor()) )
            {
                IOUtil.copy(inStream, outStream);
            }

            details.clear();
            details.put(MediaStore.Images.Media.IS_PENDING, 0);

            resolver.update(uri, details, null, null);

            return uri;
        }
        else {
            File dcimPath   = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM), "Camera");
            File outputPath = new File(dcimPath, inputFile.getName());

            if(!dcimPath.exists() && !dcimPath.mkdirs()) {
                Log.e(TAG, "Failed to create " + dcimPath.toString());
                return null;
            }

            Log.d(TAG, "Writing to " + outputPath.getPath());

            FileUtils.copyFile(inputFile, outputPath);

            MediaScannerConnection.scanFile(
                    getApplicationContext(),
                    new String[] { outputPath.getAbsolutePath() },
                    new String[] { mimeType },
                    null);

            return FileProvider.getUriForFile(getApplicationContext(), AUTHORITY, outputPath);
        }
    }

    private WorkResult processFile(boolean inMemory, File containerPath, File previewPath) throws IOException {
        String outputFileNameJpeg = fileNoExtension(containerPath.getName()) + ".jpg";
        String outputFileNameDng = fileNoExtension(containerPath.getName()) + ".dng";

        File tempFileJpeg = new File(previewPath, outputFileNameJpeg);
        File tempFileDng = new File(previewPath, outputFileNameDng);

        if (inMemory) {
            if(!mNativeProcessor.processInMemory(tempFileJpeg.getPath(), this))
                return null;
        }
        else {
            mNativeProcessor.processFile(containerPath.getPath(), tempFileJpeg.getPath(), this);
        }

        // Copy to media store
        Uri uri = null;

        if (BuildConfig.DEBUG && !inMemory)
            saveToFiles(containerPath, "application/zip", Environment.DIRECTORY_DOCUMENTS);

        if (tempFileDng.exists()) {
            saveToMediaStore(tempFileDng, "image/x-adobe-dng", Environment.DIRECTORY_DCIM + File.separator + "Camera");

            if (!tempFileDng.delete()) {
                Log.w(TAG, "Failed to delete " + tempFileDng.toString());
            }
        }

        if (tempFileJpeg.exists()) {
            uri = saveToMediaStore(tempFileJpeg, "image/jpeg", Environment.DIRECTORY_DCIM + File.separator + "Camera");
        }

        if (!inMemory) {
            if(!containerPath.delete()) {
                Log.w(TAG, "Failed to delete " + containerPath.toString());
            }
        }

        if(uri == null)
            return new WorkResult(null, null, "File not found");

        return new WorkResult(tempFileJpeg.getPath(), uri, null);
    }

    private List<WorkResult> processInMemory(File previewDirectory) {
        Log.d(TAG, "Processing in-memory containers");

        List<WorkResult> completedFiles = new ArrayList<>();

        while(true) {
            File inMemoryTmp = CameraProfile.generateCaptureFile(getApplicationContext());

            try {
                WorkResult result = processFile(true, inMemoryTmp, previewDirectory);
                if(result == null)
                    break;

                if(result.error == null)
                    completedFiles.add(result);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process in-memory container", e);
            }
        }

        return completedFiles;
    }

    private List<WorkResult> processOnStorage(File previewDirectory) {
        List<WorkResult> completedFiles = new ArrayList<>();

        String metadataPath = CameraProfile.getRootOutputPath(getApplicationContext()).getPath();
        File root = new File(metadataPath);

        // Find all pending files and process them
        File[] pendingFiles = root.listFiles((dir, name) -> name.toLowerCase().endsWith("zip"));
        if(pendingFiles == null)
            return completedFiles;

        // Process all files
        Arrays.sort(pendingFiles);

        for(File file : pendingFiles) {
            try {
                Log.d(TAG, "Processing " + file.getPath());
                WorkResult result = processFile(false, file, previewDirectory);
                if(result != null && result.error == null)
                    completedFiles.add(result);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process " + file.getPath(), e);
                if(!file.delete()) {
                    Log.w(TAG, "Failed to delete " + file.toString());
                }
            }
        }

        return completedFiles;
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
        if(!init()) {
            mNotifyManager.cancel(NOTIFICATION_ID);
            return Result.failure();
        }

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        try {
            List<WorkResult> completedFiles = new ArrayList<>();

            completedFiles.addAll(processInMemory(mPreviewDirectory));
            completedFiles.addAll(processOnStorage(mPreviewDirectory));

            mNotifyManager.cancel(NOTIFICATION_ID);

            String[] completedUris = completedFiles.stream()
                    .map(e -> e.outputUri.toString())
                    .toArray(String[]::new);

            String[] completedImagePaths = completedFiles.stream()
                    .map(e -> e.outputPath)
                    .toArray(String[]::new);

            Data result = new Data.Builder()
                    .putInt(State.PROGRESS_STATE_KEY, State.STATE_COMPLETED)
                    .putStringArray(State.PROGRESS_URI_KEY, completedUris)
                    .putStringArray(State.PROGRESS_IMAGE_PATH, completedImagePaths)
                    .build();

            return Result.success(result);
        }
        catch(Exception e) {
            Log.e(TAG, "Error processing image", e);
            return Result.failure();
        }
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
