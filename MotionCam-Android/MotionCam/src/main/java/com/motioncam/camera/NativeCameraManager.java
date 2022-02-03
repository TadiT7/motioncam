package com.motioncam.camera;

import android.util.Size;

import java.io.Closeable;
import java.io.IOException;

public class NativeCameraManager implements Closeable {
    public NativeCameraManager() {
        CreateCameraManager();
    }

    @Override
    public void close() throws IOException {
        DestroyCameraManager();
    }

    public NativeCameraInfo[] getSupportedCameras() {
        return GetSupportedCameras();
    }

    public Size getRawConfigurationOutput(NativeCameraInfo cameraInfo) {
        Size rawOutput = GetRawOutputSize(cameraInfo.cameraId);
        if(rawOutput == null) {
            throw new NativeCamera.CameraException("Failed to get RAW output size");
        }

        return rawOutput;
    }

    public Size getPreviewConfigurationOutput(NativeCameraInfo cameraInfo, Size captureSize, Size displaySize) {
        Size previewOutput = GetPreviewOutputSize(cameraInfo.cameraId, captureSize, displaySize);
        if(previewOutput == null) {
            throw new NativeCamera.CameraException("Failed to get preview output size");
        }

        return previewOutput;
    }

    public NativeCameraMetadata getMetadata(NativeCameraInfo cameraInfo) {
        return GetMetadata(cameraInfo.cameraId);
    }

    private native void CreateCameraManager();
    private native void DestroyCameraManager();

    private native NativeCameraInfo[] GetSupportedCameras();
    private native NativeCameraMetadata GetMetadata(String cameraId);
    private native Size GetRawOutputSize(String cameraId);
    private native Size GetPreviewOutputSize(String cameraId, Size captureSize, Size displaySize);
}
