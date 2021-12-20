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

import org.apache.commons.io.IOUtil;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

public class VideoProcessWorker extends Worker implements NativeDngConverterListener {
    public static final String TAG = "MotionVideoCamWorker";

    public static final String INPUT_URI_KEY = "input_uri";
    public static final String INPUT_AUDIO_URI_KEY = "input_audio_uri";
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

    private String getName(Uri uri) {
        if(uri.getScheme().equalsIgnoreCase("file")) {
            return new File(uri.getPath()).getName();
        }
        else {
            return DocumentFile.fromSingleUri(getApplicationContext(), uri).getName();
        }
    }

    private void processVideo(Uri inputUri, int numFramesToMerge) throws IOException {
        mInputUri = inputUri;

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
                .setContentTitle(context.getString(R.string.please_wait))
                .setContentText(context.getString(R.string.processing_video))
                .setTicker(context.getString(R.string.app_name))
                .setLargeIcon(BitmapFactory.decodeResource(context.getResources(), R.mipmap.icon))
                .setSmallIcon(R.drawable.ic_processing_notification)
                .setOngoing(true);

        mNativeProcessor = new NativeProcessor();

        return true;
    }

    @NonNull
    @Override
    public Result doWork() {
        if(!init()) {
            Log.e(TAG, "Failed to initialise");
            mNotifyManager.cancel(NOTIFICATION_ID);
            return Result.failure();
        }

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        Data inputData = getInputData();
        int numFramesToMerge = inputData.getInt(INPUT_NUM_FRAMES_TO_MERGE, 0);

        String inputUriString       = inputData.getString(INPUT_URI_KEY);
        String inputAudioUriString  = inputData.getString(INPUT_AUDIO_URI_KEY);

        Log.d(TAG, "Starting video worker");

        if(inputAudioUriString == null && inputUriString == null) {
            Log.d(TAG, "Nothing to do");
            return Result.success();
        }

        String outputUriString = inputData.getString(OUTPUT_URI_KEY);
        if(outputUriString == null) {
            Log.e(TAG, "Invalid output URI");
            return Result.failure();
        }

        Uri outputUri = Uri.parse(outputUriString);
        DocumentFile output = DocumentFile.fromTreeUri(getApplicationContext(), outputUri);
        if(output == null) {
            Log.e(TAG, "Invalid output");
            return Result.failure();
        }

        Uri videoInputUri = null;
        if(inputUriString != null)
            videoInputUri = Uri.parse(inputUriString);

        Uri audioInputUri = null;
        if(inputAudioUriString != null)
            audioInputUri = Uri.parse(inputAudioUriString);

        // Get output folder
        String outputName;

        if(videoInputUri != null)
            outputName = getName(videoInputUri);
        else if(audioInputUri != null)
            outputName = getName(audioInputUri);
        else {
            mNotifyManager.cancel(NOTIFICATION_ID);
            return Result.failure();
        }

        String outputDirectoryName = getNameWithoutExtension(outputName);
        String finalOutputDirectoryName = outputDirectoryName;

        int i = 1;
        while(output.findFile(finalOutputDirectoryName) != null) {
            finalOutputDirectoryName = outputDirectoryName + "-" + i;
            ++i;
        }

        mOutputDocument = output.createDirectory(finalOutputDirectoryName);

        if(mOutputDocument == null || !mOutputDocument.exists() ) {
            Log.e(TAG, "Failed to create output directory");
            mNotifyManager.cancel(NOTIFICATION_ID);
            return Result.failure();
        }

        // Process video first
        if(videoInputUri != null) {
            try {
                Log.d(TAG, "Processing video " + inputUriString);
                processVideo(videoInputUri, numFramesToMerge);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process " + inputUriString, e);
            }
        }

        if(audioInputUri != null) {
            try {
                moveAudio(audioInputUri);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to move audio from " + inputUriString, e);
            }
        }

        Log.d(TAG, "Stopping video worker");

        mNotifyManager.cancel(NOTIFICATION_ID);

        Data result = new Data.Builder()
                .putInt(State.PROGRESS_STATE_KEY, State.STATE_COMPLETED)
                .putString(State.PROGRESS_INPUT_URI_KEY, inputUriString)
                .build();

        return Result.success(result);
    }

    private void moveAudio(Uri audioInputUri) throws IOException {
        DocumentFile dstAudioFile = mOutputDocument.createFile("audio/wav", getName(audioInputUri));
        if(dstAudioFile == null) {
            Log.e(TAG, "Failed to create destination audio file");
            return;
        }

        InputStream inputStream;

        if(audioInputUri.getScheme().equalsIgnoreCase("file")) {
            inputStream = new FileInputStream(audioInputUri.getPath());
        }
        else {
            DocumentFile audioFile = DocumentFile.fromSingleUri(getApplicationContext(), audioInputUri);
            inputStream = getApplicationContext().getContentResolver().openInputStream(audioFile.getUri());
        }

        try (OutputStream outputStream = getApplicationContext().getContentResolver().openOutputStream(dstAudioFile.getUri())) {
            if(inputStream != null && outputStream != null)
                IOUtil.copy(inputStream, outputStream);
        }
        finally {
            if(inputStream != null)
                inputStream.close();
        }
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
