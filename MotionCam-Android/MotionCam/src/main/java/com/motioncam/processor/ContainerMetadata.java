package com.motioncam.processor;

public class ContainerMetadata {
    public final float frameRate;
    public final int numFrames;
    public final int numSegments;

    public ContainerMetadata(float frameRate, int numFrames, int numSegments) {
        this.frameRate = frameRate;
        this.numFrames = numFrames;
        this.numSegments = numSegments;
    }
}
