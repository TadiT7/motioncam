package com.motioncam.worker;

import android.app.NotificationManager;
import android.content.ContentResolver;
import android.content.Context;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.text.TextUtils;
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
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class VideoProcessWorker extends Worker implements NativeDngConverterListener {
    public static final String TAG = "MotionVideoCamWorker";

    public enum WorkerMode {
        EXPORT,
        MOVE
    }

    public static final String INPUT_MODE_KEY = "input_mode";
    public static final String INPUT_NAME_KEY = "input_name";
    public static final String INPUT_VIDEO_URI_KEY = "input_video_uri";
    public static final String INPUT_AUDIO_URI_KEY = "input_audio_uri";
    public static final String INPUT_DELETE_AFTER_EXPORT_KEY = "delete_after_export";
    public static final String INPUT_CORRECT_VIGNETTE_KEY = "correct_vignette";
    public static final String INPUT_NUM_FRAMES_TO_MERGE = "num_frames_to_merge";

    public static final String OUTPUT_URI_KEY = "output_uri";

    public static final int NOTIFICATION_ID = 0x90005002;
    public static final String VIDEOS_PATH = "videos";

    private NativeProcessor mNativeProcessor;
    private NotificationCompat.Builder mNotificationBuilder;
    private NotificationManager mNotifyManager;
    private List<Uri> mInputUris;
    private String mInputName;
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

    private void moveVideo(List<Uri> inputUris) throws IOException {
        ContentResolver resolver = getApplicationContext().getContentResolver();

        for(Uri uri : inputUris) {
            File videoPath = new File(uri.getPath());
            if(!videoPath.exists())
                continue;

            DocumentFile output = mOutputDocument.createFile("application/octet-stream", videoPath.getName() + ".tmp");

            try(InputStream inputStream = new FileInputStream(videoPath);
                OutputStream outputStream = resolver.openOutputStream(output.getUri()))
            {
                IOUtil.copy(inputStream, outputStream);
            }

            // Rename once we have deleted the original
            output.renameTo(videoPath.getName());
        }
    }

    private void processVideo(List<Uri> inputUris, int numFramesToMerge, boolean correctVignette) throws IOException {
        mInputUris = inputUris;

        ContentResolver resolver = getApplicationContext().getContentResolver();
        List<Integer> fds = new ArrayList<>();

        for(Uri uri : inputUris) {
            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "r", null)) {
                if (pfd == null) {
                    continue;
                }

                int fd = pfd.detachFd();
                if(fd >= 0) {
                    fds.add(fd);
                }
                else {
                    Log.e(TAG, "Failed to open " + uri);
                }
            }
        }

        // If we are not able to open all the inputs, we need to stop
        if(fds.size() != inputUris.size()) {
            for(Integer fd : fds) {
                if(fd != null)
                    NativeProcessor.closeFd(fd);
            }

            throw new RuntimeException("Failed to open inputs");
        }

        final int[] finalFds = fds.stream().mapToInt(i -> i).toArray();

        mNativeProcessor.processRawVideo(finalFds, numFramesToMerge, correctVignette, this);
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

    private Result succeed(Data result) {
        mNotifyManager.cancel(NOTIFICATION_ID);

        return Result.success(result);
    }

    private Result fail(String reason) {
        mNotifyManager.cancel(NOTIFICATION_ID);

        Log.e(TAG, "Failed: " + reason);

        return Result.failure();
    }

    @NonNull
    @Override
    public Result doWork() {
        if(!init()) {
            return fail("Failed to initialise");
        }

        setForegroundAsync(new ForegroundInfo(NOTIFICATION_ID, mNotificationBuilder.build()));

        Log.d(TAG, "Starting video worker");

        Data inputData = getInputData();

        // Figure out if we are exporting or moving this video
        String workerModeString = inputData.getString(INPUT_MODE_KEY);
        WorkerMode workerMode = WorkerMode.EXPORT;

        if(workerModeString != null) {
            workerMode = WorkerMode.valueOf(workerModeString);
        }

        int numFramesToMerge = inputData.getInt(INPUT_NUM_FRAMES_TO_MERGE, 0);
        boolean correctVignette = inputData.getBoolean(INPUT_CORRECT_VIGNETTE_KEY, true);

        String name = inputData.getString(INPUT_NAME_KEY);
        String inputAudioUriString = inputData.getString(INPUT_AUDIO_URI_KEY);
        String[] inputVideoUriString = inputData.getStringArray(INPUT_VIDEO_URI_KEY);
        boolean deleteAfterExport = inputData.getBoolean(INPUT_DELETE_AFTER_EXPORT_KEY, false);

        if(name == null) {
            return fail("Invalid name");
        }

        // Keep input name
        mInputName = name;

        if(inputAudioUriString == null && inputVideoUriString == null) {
            Log.d(TAG, "Nothing to do");
            return succeed(new Data.Builder().build());
        }

        String outputUriString = inputData.getString(OUTPUT_URI_KEY);
        if(outputUriString == null) {
            return fail("Invalid output uri");
        }

        Uri outputUri = Uri.parse(outputUriString);
        DocumentFile output = DocumentFile.fromTreeUri(getApplicationContext(), outputUri);
        if(output == null) {
            return fail("Invalid output root");
        }

        Uri audioInputUri = null;
        if(inputAudioUriString != null)
            audioInputUri = Uri.parse(inputAudioUriString);

        if(workerMode == WorkerMode.EXPORT) {
            String finalOutputDirectoryName = name;

            int i = 1;
            while (output.findFile(finalOutputDirectoryName) != null) {
                finalOutputDirectoryName = name + "-" + i;
                ++i;
            }

            mOutputDocument = output.createDirectory(finalOutputDirectoryName);
        }
        else {
            mOutputDocument = output;
        }

        if(mOutputDocument == null || !mOutputDocument.exists() ) {
            return fail("Failed to create output directory");
        }

        // Process video first
        List<Uri> videoUris = new ArrayList<>();

        if(inputVideoUriString != null) {
            for (String uri : inputVideoUriString)
                videoUris.add(Uri.parse(uri));

            try {
                if(workerMode == WorkerMode.EXPORT) {
                    Log.i(TAG, "Processing video segments " + TextUtils.join(",", inputVideoUriString));
                    processVideo(videoUris, numFramesToMerge, correctVignette);
                }
                else if(workerMode == WorkerMode.MOVE) {
                    Log.i(TAG, "Moving video segments " + TextUtils.join(",", inputVideoUriString));
                    moveVideo(videoUris);
                }
            }
            catch (Exception e) {
                Log.e(TAG, "Error while processing video segments", e);
            }
        }

        if(audioInputUri != null) {
            try {
                Log.i(TAG, "Moving " + audioInputUri);
                moveAudio(audioInputUri);
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to move audio from " + inputAudioUriString, e);
            }
        }

        // Remove all files
        boolean isDeleted = false;

        if(deleteAfterExport || workerMode == WorkerMode.MOVE) {
            for(Uri videoUri : videoUris) {
                isDeleted |= Util.DeleteUri(getApplicationContext(), videoUri);
            }

            isDeleted |= Util.DeleteUri(getApplicationContext(), audioInputUri);
        }

        // Reset deleted flag when we are moving the video
        isDeleted = workerMode == WorkerMode.MOVE ? false : isDeleted;

        Log.d(TAG, "Stopping video worker");

        Data result = new Data.Builder()
                .putString(State.PROGRESS_MODE_KEY, workerMode.name())
                .putInt(State.PROGRESS_STATE_KEY, State.STATE_COMPLETED)
                .putBoolean(State.PROGRESS_DELETED, isDeleted)
                .putString(State.PROGRESS_NAME_KEY, name)
                .build();

        return succeed(result);
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
        if(mInputUris == null)
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
                .putString(State.PROGRESS_NAME_KEY, mInputName)
                .build());

        mNotificationBuilder
                .setProgress(100, progress, false)
                .setContentText(getApplicationContext().getString(R.string.processing_video));

        mNotifyManager.notify(NOTIFICATION_ID, mNotificationBuilder.build());

        return true;
    }

    @Override
    public void onCompleted() {
    }

    @Override
    public void onAttemptingRecovery() {
        mNotificationBuilder.setContentText(
                getApplicationContext().getString(R.string.video_recovery_progress));

        mNotifyManager.notify(NOTIFICATION_ID, mNotificationBuilder.build());
    }

    @Override
    public void onError(String error) {
    }
}
