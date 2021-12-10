package com.motioncam.processor;

public class NativeProcessor {
    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        return ProcessInMemory(outputPath, listener);
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        ProcessFile(inputPath, outputPath, listener);
    }

    public void processVideo(int fd, int numFramesToMerge, NativeDngConverterListener listener) {
        ProcessVideo(fd, numFramesToMerge, listener);
    }

    public ContainerMetadata getMetadata(int fd) {
        return GetMetadata(fd);
    }

    native boolean ProcessInMemory(String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessVideo(int fd, int numFramesToMerge, NativeDngConverterListener progressListener);
    native ContainerMetadata GetMetadata(int fd);

    native String GetLastError();
}
