package com.motioncam.processor;

public class NativeProcessor {
    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        return ProcessInMemory(outputPath, listener);
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        ProcessFile(inputPath, outputPath, listener);
    }

    public void processVideo(String inputPath, int numFramesToMerge, NativeDngConverterListener listener) {
        ProcessVideo(inputPath, numFramesToMerge, listener);
    }

    public ContainerMetadata getMetadata(String inputPath) {
        return GetMetadata(inputPath);
    }

    native boolean ProcessInMemory(String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessVideo(String inputPath, int numFramesToMerge, NativeDngConverterListener progressListener);
    native ContainerMetadata GetMetadata(String inputPath);

    native String GetLastError();
}
