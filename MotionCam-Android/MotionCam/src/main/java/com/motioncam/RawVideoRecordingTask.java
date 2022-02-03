package com.motioncam;

import android.app.Activity;
import android.os.StatFs;

import com.motioncam.camera.NativeCamera;
import com.motioncam.camera.VideoRecordingStats;
import com.motioncam.databinding.CameraActivityBinding;

import java.util.Locale;
import java.util.TimerTask;

class RawVideoRecordingTask extends TimerTask {
    private static final int DELAY_OUTPUT_FPS_MS = 3000;
    private final CameraActivityBinding mBinding;
    private final NativeCamera mNativeCamera;
    private final Activity mActivity;
    private final long mRecordStartTime;

    RawVideoRecordingTask(NativeCamera nativeCamera, Activity activity, CameraActivityBinding binding) {
        mNativeCamera = nativeCamera;
        mActivity = activity;
        mBinding = binding;
        mRecordStartTime = System.currentTimeMillis();
    }

    @Override
    public void run() {
        long timeRecording = System.currentTimeMillis() - mRecordStartTime;

        float mins = (timeRecording / 1000.0f) / 60.0f;
        int seconds = (int) ((mins - ((int) mins)) * 60);

        String recordingText = String.format(Locale.US, "00:%02d:%02d", (int) mins, seconds);

        mActivity.runOnUiThread(() -> {
            VideoRecordingStats stats = mNativeCamera.getVideoRecordingStats();

            mBinding.recordingTimerText.setText(recordingText);
            mBinding.previewFrame.memoryUsageProgress.setProgress(Math.round(stats.memoryUse * 100));

            // Keep track of space
            StatFs statFs = new StatFs(mActivity.getFilesDir().getPath());
            int spaceLeft = Math.round(100 * (statFs.getAvailableBytes() / (float) statFs.getTotalBytes()));

            mBinding.previewFrame.freeSpaceProgress.setProgress(spaceLeft);

            // Display space usage
            if (spaceLeft < 25) {
                mBinding.previewFrame.freeSpaceProgress.getProgressDrawable()
                        .setTint(mActivity.getColor(R.color.cancelAction));
            } else {
                mBinding.previewFrame.freeSpaceProgress.getProgressDrawable()
                        .setTint(mActivity.getColor(R.color.acceptAction));
            }

            // Display memory usage
            if (stats.memoryUse > 0.75f) {
                mBinding.previewFrame.memoryUsageProgress.getProgressDrawable()
                        .setTint(mActivity.getColor(R.color.cancelAction));
            } else {
                mBinding.previewFrame.memoryUsageProgress.getProgressDrawable()
                        .setTint(mActivity.getColor(R.color.acceptAction));
            }

            float sizeMb = stats.size / 1024.0f / 1024.0f;
            float sizeGb = sizeMb / 1024.0f;

            String size;

            if (sizeMb > 1024) {
                size = String.format(Locale.US, "%.2f GB", sizeGb);
            } else {
                size = String.format(Locale.US, "%d MB", Math.round(sizeMb));
            }

            String outputSizeText = String.format(Locale.US, "%s\n%s", size, mActivity.getString(R.string.size));

            // Wait for the frame rate to stabilise
            String outputFpsText;

            if (timeRecording > DELAY_OUTPUT_FPS_MS)
                outputFpsText = String.format(Locale.US, "%d\n%s", Math.round(stats.fps), mActivity.getString(R.string.output_fps));
            else
                outputFpsText = String.format(Locale.US, "-\n%s", mActivity.getString(R.string.output_fps));

            mBinding.previewFrame.outputFps.setText(outputFpsText);
            mBinding.previewFrame.outputSize.setText(outputSizeText);
        });
    }
}
