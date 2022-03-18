package com.motioncam.camera;

import android.graphics.Bitmap;
import android.graphics.PointF;
import android.util.Size;
import android.view.Surface;

import com.motioncam.processor.NativeBitmapListener;
import com.squareup.moshi.JsonAdapter;
import com.squareup.moshi.Moshi;

import java.io.Closeable;
import java.io.IOException;

public class NativeCamera implements Closeable, NativeCameraSessionListener, NativeCameraRawPreviewListener {

    // Camera states
    public enum CameraState {
        INVALID(-1),
        READY(0),
        ACTIVE(1),
        CLOSED(2);

        private final int mState;

        CameraState(int state) {
            mState = state;
        }

        static public CameraState FromInt(int state) {
            for(CameraState cameraState : CameraState.values()) {
                if(cameraState.mState == state)
                    return cameraState;
            }

            return INVALID;
        }
    }

    public enum CameraFocusState {
        INVALID(-1),
        INACTIVE(0),
        PASSIVE_SCAN(1),
        PASSIVE_FOCUSED(2),
        ACTIVE_SCAN(3),
        FOCUS_LOCKED(4),
        NOT_FOCUS_LOCKED(5),
        PASSIVE_UNFOCUSED(6);

        private final int mState;

        CameraFocusState(int state) {
            mState = state;
        }

        static public CameraFocusState FromInt(int state) {
            for(CameraFocusState cameraFocusState : CameraFocusState.values()) {
                if(cameraFocusState.mState == state)
                    return cameraFocusState;
            }

            return INVALID;
        }
    }

    public enum CameraExposureState {
        INVALID(-1),
        INACTIVE(0),
        SEARCHING(1),
        CONVERGED(2),
        LOCKED(3),
        FLASH_REQUIRED(4),
        PRECAPTURE(5);

        private final int mState;

        CameraExposureState(int state) {
            mState = state;
        }

        static public CameraExposureState FromInt(int state) {
            for(CameraExposureState cameraExposureState : CameraExposureState.values()) {
                if(cameraExposureState.mState == state)
                    return cameraExposureState;
            }

            return INVALID;
        }
    }

    public interface CameraSessionListener {
        void onCameraStarted();
        void onCameraDisconnected();
        void onCameraError(int error);
        void onCameraSessionStateChanged(CameraState state);
        void onCameraExposureStatus(int iso, long exposureTime);
        void onCameraAutoFocusStateChanged(CameraFocusState state, float focusDistance);
        void onCameraAutoExposureStateChanged(CameraExposureState state);
        void onCameraHdrImageCaptureProgress(int progress);
        void onCameraHdrImageCaptureFailed();
        void onCameraHdrImageCaptureCompleted();

        void onMemoryAdjusting();
        void onMemoryStable();
    }

    public interface CameraRawPreviewListener {
        void onRawPreviewCreated(Bitmap bitmap);
        void onRawPreviewUpdated();
    }

    public static class CameraException extends RuntimeException {
        CameraException(String error) {
            super(error);
        }
    }

    private static int SESSION_ID = 1000;

    private final Moshi mJson = new Moshi.Builder().build();
    private CameraSessionListener mListener;
    private CameraRawPreviewListener mRawPreviewListener;
    private final int mSessionId;

    public NativeCamera() {
        mSessionId = -1;
    }

    public NativeCamera(CameraSessionListener cameraListener) {
        mListener = cameraListener;
        mSessionId = ++SESSION_ID;

        Create();
    }

    @Override
    public void close() throws IOException {
        if(mSessionId > 0) {
            Destroy();

            mListener = null;
        }
    }

    public void startCapture(
            NativeCameraInfo cameraInfo,
            Surface previewOutput,
            boolean setupForRawPreview,
            boolean preferRaw12,
            boolean preferRaw16,
            CameraStartupSettings startupSettings,
            long maxMemoryUsageBytes)
    {
        JsonAdapter<CameraStartupSettings> jsonAdapter = mJson.adapter(CameraStartupSettings.class);
        String startupSettingsJson = jsonAdapter.toJson(startupSettings);

        if(!StartCapture(
                cameraInfo.cameraId,
                previewOutput,
                setupForRawPreview,
                preferRaw12,
                preferRaw16,
                startupSettingsJson,
                this,
                maxMemoryUsageBytes))
        {
            throw new CameraException(GetLastError());
        }
    }

    public void stopCapture() {
        if(!StopCapture()) {
            throw new CameraException(GetLastError());
        }
    }

    public void pauseCapture() {
        PauseCapture();
    }

    public void resumeCapture() {
        ResumeCapture();
    }

    public void setAutoExposure() {
        SetAutoExposure();
    }

    public void setOIS(boolean ois) {
        SetOIS(ois);
    }

    public void setManualFocus(float focusDistance) {
        SetManualFocus(focusDistance);
    }

    public void setFocusForVideo(boolean focusForVideo) {
        SetFocusForVideo(focusForVideo);
    }

    public void setAWBLock(boolean lock) {
        SetAWBLock(lock);
    }

    public void setAELock(boolean lock) {
        SetAELock(lock);
    }

    public void setManualExposureValues(int iso, long exposureValue) {
        SetManualExposure(iso, exposureValue);
    }

    public void setExposureCompensation(float value) {
        SetExposureCompensation(value);
    }

    public PostProcessSettings getRawPreviewEstimatedPostProcessSettings() throws IOException {
        String settingsJson = GetRawPreviewEstimatedSettings();

        if(settingsJson == null) {
            return null;
        }

        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        return jsonAdapter.fromJson(settingsJson);
    }

    public void prepareHdrCapture(int iso, long exposure) {
        PrepareHdrCapture(iso, exposure);
    }

    public void captureImage(long bufferHandle, int numSaveImages, PostProcessSettings settings, String outputPath) {
        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CaptureImage(bufferHandle, numSaveImages, json, outputPath);
    }

    public void captureZslHdrImage(int numSaveImages, PostProcessSettings settings, String outputPath) {
        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CaptureZslHdrImage(numSaveImages, json, outputPath);
    }

    public void captureHdrImage(int numSaveImages, int baseIso, long baseExposure, int hdrIso, long hdrExposure, PostProcessSettings settings, String outputPath) {
        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CaptureHdrImage(numSaveImages, baseIso, baseExposure, hdrIso, hdrExposure, json, outputPath);
    }

    public Size getPreviewSize(int downscaleFactor) {
        return GetPreviewSize(downscaleFactor);
    }

    public void createPreviewImage(long bufferHandle, PostProcessSettings settings, int downscaleFactor, Bitmap dst) {
        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CreateImagePreview(bufferHandle, json, downscaleFactor, dst);
    }

    public NativeCameraBuffer[] getAvailableImages() {
        return GetAvailableImages();
    }

    public PostProcessSettings estimatePostProcessSettings(float shadowsBias) throws IOException {
        String settingsJson = EstimatePostProcessSettings(shadowsBias);
        if(settingsJson == null) {
            return null;
        }

        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        return jsonAdapter.fromJson(settingsJson);
    }

    public float estimateShadows(float bias) {
        return EstimateShadows(bias);
    }

    public double measureSharpness(long bufferHandle) {
        return MeasureSharpness(bufferHandle);
    }

    public void enableRawPreview(CameraRawPreviewListener listener, int previewQuality, boolean overrideWb) {
        mRawPreviewListener = listener;

        EnableRawPreview(this, previewQuality, overrideWb);
    }

    public void setRawPreviewSettings(
            float shadows,
            float contrast,
            float saturation,
            float blacks,
            float whitePoint,
            float tempOffset,
            float tintOffset,
            boolean useVideoPreview)
    {
        SetRawPreviewSettings(shadows, contrast, saturation, blacks, whitePoint, tempOffset, tintOffset, useVideoPreview);
    }

    public void disableRawPreview() {
        DisableRawPreview();
    }

    public void updateOrientation(NativeCameraBuffer.ScreenOrientation orientation) {
        UpdateOrientation(orientation.value);
    }

    public void setFocusPoint(PointF focusPt, PointF exposurePt) {
        SetFocusPoint(focusPt.x, focusPt.y, exposurePt.x, exposurePt.y);
    }

    public void setAutoFocus() {
        SetAutoFocus();
    }

    public void setAperture(float aperture) {
        SetLensAperture(aperture);
    }

    public void setTorch(boolean enable) {
        SetEnableTorch(enable);
    }

    public void activateCameraSettings() {
        ActivateCameraSettings();
    }

    public void streamToFile(int[] fds, int audioFd, int audioDeviceId, int numThreads) {
        StartStreamToFile(fds, audioFd, audioDeviceId, numThreads);
    }

    public void adjustMemory(long maxMemoryBytes) {
        AdjustMemoryUse(maxMemoryBytes);
    }

    public void endStream() {
        EndStream();
    }

    public void setFrameRate(int frameRate) {
        SetFrameRate(frameRate);
    }

    public void setVideoBin(boolean bin) {
        SetVideoBin(bin);
    }

    public void setVideoCropPercentage(int horizontal, int vertical) {
        SetVideoCropPercentage(horizontal, vertical);
    }

    public VideoRecordingStats getVideoRecordingStats() {
        return GetVideoRecordingStats();
    }

    public void generateStats(NativeBitmapListener listener) {
        GenerateStats(listener);
    }

    @Override
    public void onCameraStarted() {
        mListener.onCameraStarted();
    }

    @Override
    public void onCameraDisconnected() {
        mListener.onCameraDisconnected();
    }

    @Override
    public void onCameraError(int error) {
        mListener.onCameraError(error);
    }

    @Override
    public void onCameraSessionStateChanged(int state) {
        mListener.onCameraSessionStateChanged(CameraState.FromInt(state));
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
        mListener.onCameraExposureStatus(iso, exposureTime);
    }

    @Override
    public void onCameraAutoFocusStateChanged(int state, float focusDistance) {
        mListener.onCameraAutoFocusStateChanged(CameraFocusState.FromInt(state), focusDistance);
    }

    @Override
    public void onCameraAutoExposureStateChanged(int state) {
        mListener.onCameraAutoExposureStateChanged(CameraExposureState.FromInt(state));
    }

    @Override
    public void onCameraHdrImageCaptureFailed() {
        mListener.onCameraHdrImageCaptureFailed();
    }

    @Override
    public void onCameraHdrImageCaptureProgress(int image) {
        mListener.onCameraHdrImageCaptureProgress(image);
    }

    @Override
    public void onCameraHdrImageCaptureCompleted() {
        mListener.onCameraHdrImageCaptureCompleted();
    }

    @Override
    public void onMemoryAdjusting() {
        mListener.onMemoryAdjusting();
    }

    @Override
    public void onMemoryStable() {
        mListener.onMemoryStable();
    }

    @Override
    public Bitmap onRawPreviewBitmapNeeded(int width, int height) {
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);

        mRawPreviewListener.onRawPreviewCreated(bitmap);

        return bitmap;
    }

    @Override
    public void onRawPreviewUpdated() {
        mRawPreviewListener.onRawPreviewUpdated();
    }

    private native boolean Create();
    private native void Destroy();

    private native String GetLastError();

    private native boolean StartCapture(
            String cameraId,
            Surface previewSurface,
            boolean setupForRawPreview,
            boolean preferRaw12,
            boolean preferRaw16,
            String cameraStartupSettingsJson,
            NativeCameraSessionListener listener,
            long maxMemoryUsageBytes);

    private native boolean StopCapture();
    private native boolean PauseCapture();
    private native boolean ResumeCapture();

    private native boolean SetManualExposure(int iso, long exposureTime);
    private native boolean SetAutoExposure();
    private native boolean SetExposureCompensation(float value);
    private native boolean SetAWBLock(boolean lock);
    private native boolean SetAELock(boolean lock);
    private native boolean SetOIS(boolean on);
    private native boolean SetFocusForVideo(boolean focusForVideo);
    private native boolean SetLensAperture(float lensAperture);
    private native boolean SetEnableTorch(boolean enable);
    private native boolean SetManualFocus(float focusDistance);
    private native boolean SetFocusPoint(float focusX, float focusY, float exposureX, float exposureY);
    private native boolean SetAutoFocus();
    private native boolean ActivateCameraSettings();

    private native boolean EnableRawPreview(NativeCameraRawPreviewListener listener, int previewQuality, boolean overrideWb);
    private native boolean SetRawPreviewSettings(float shadows, float contrast, float saturation, float blacks, float whitePoint, float tempOffset, float tintOfset, boolean useVideoPreview);
    private native boolean DisableRawPreview();
    private native String GetRawPreviewEstimatedSettings();

    private native boolean UpdateOrientation(int orientation);

    private native boolean StartStreamToFile(int[] videoFds, int audioFd, int audioDeviceId, int numThreads);
    private native void EndStream();

    private native void PrepareHdrCapture(int iso, long exposure);
    private native boolean CaptureImage(long bufferHandle, int numSaveImages, String settings, String outputPath);

    private native boolean CaptureZslHdrImage(int numImages, String settings, String outputPath);
    private native boolean CaptureHdrImage(int numImages, int baseIso, long baseExposure, int hdrIso, long hdrExposure, String settings, String outputPath);

    private native NativeCameraBuffer[] GetAvailableImages();
    private native Size GetPreviewSize(int downscaleFactor);
    private native boolean CreateImagePreview(long bufferHandle, String settings, int downscaleFactor, Bitmap dst);

    private native double MeasureSharpness(long bufferHandle);

    private native float EstimateShadows(float bias);
    private native String EstimatePostProcessSettings(float shadowsBias);

    private native void SetFrameRate(int frameRate);
    private native void SetVideoCropPercentage(int horizontal, int vertical);
    private native VideoRecordingStats GetVideoRecordingStats();
    private native void SetVideoBin(boolean bin);

    private native void AdjustMemoryUse(long maxUseBytes);

    private native void GenerateStats(NativeBitmapListener listener);
}
