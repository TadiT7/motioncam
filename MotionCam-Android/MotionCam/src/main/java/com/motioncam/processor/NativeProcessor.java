package com.motioncam.processor;

import java.util.Objects;

public class NativeProcessor {
    static public void closeFd(int fd) {
        CloseFd(fd);
    }

    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        return ProcessInMemory(outputPath, listener);
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        ProcessFile(inputPath, outputPath, listener);
    }

    public void processRawVideo(int fds[], int numFramesToMerge, NativeDngConverterListener listener) {
        Objects.requireNonNull(fds);

        ProcessRawVideo(fds, numFramesToMerge, listener);
    }

    public ContainerMetadata getRawVideoMetadata(int fds[]) {
        Objects.requireNonNull(fds);

        return GetRawVideoMetadata(fds);
    }

    public boolean generateRawVideoPreview(final int fd, int numPreviews, NativeBitmapListener listener) {
        return GenerateRawVideoPreview(fd, numPreviews, listener);
    }

    native boolean ProcessInMemory(String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(String inputPath, String outputPath, NativeProcessorProgressListener progressListener);

    native boolean ProcessRawVideo(int fds[], int numFramesToMerge, NativeDngConverterListener progressListener);
    native ContainerMetadata GetRawVideoMetadata(int fds[]);
    native boolean GenerateRawVideoPreview(int fd, int numPreviews, NativeBitmapListener listener);

    static native void CloseFd(int fd);

    native String GetLastError();
}
