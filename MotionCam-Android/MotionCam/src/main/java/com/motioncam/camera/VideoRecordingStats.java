package com.motioncam.camera;

public class VideoRecordingStats {
    public final float fps;
    public final float memoryUse;
    public final long size;

    public VideoRecordingStats(float memoryUse, float fps, long size) {
        this.memoryUse = memoryUse;
        this.fps = fps;
        this.size = size;
    }
}
