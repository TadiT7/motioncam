package com.motioncam.processor;

public class ContainerMetadata {
    public final float frameRate;
    public final int numFrames;

    public ContainerMetadata(float frameRate, int numFrames) {
        this.frameRate = frameRate;
        this.numFrames = numFrames;
    }
}
