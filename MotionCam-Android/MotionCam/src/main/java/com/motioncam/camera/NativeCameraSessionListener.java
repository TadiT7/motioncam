package com.motioncam.camera;

public interface NativeCameraSessionListener {
    void onCameraDisconnected();
    void onCameraError(int error);
    void onCameraSessionStateChanged(int state);
    void onCameraExposureStatus(int iso, long exposureTime);
    void onCameraAutoFocusStateChanged(int state, float focusDistance);
    void onCameraAutoExposureStateChanged(int state);
    void onCameraHdrImageCaptureProgress(int progress);
    void onCameraHdrImageCaptureFailed();
    void onCameraHdrImageCaptureCompleted();

    void onMemoryAdjusting();
    void onMemoryStable();
}
