package com.motioncam.camera;

public class NativeCameraMetadata {
    public final int sensorOrientation;
    public final int isoMin;
    public final int isoMax;
    public final long exposureTimeMin;
    public final long exposureTimeMax;
    public final int maxAfRegions;
    public final int maxAeRegions;
    public final float[] cameraApertures;
    public final float[] focalLength;

    public NativeCameraMetadata(int sensorOrientation,
                                int isoMin,
                                int isoMax,
                                long exposureTimeMin,
                                long exposureTimeMax,
                                int maxAfRegions,
                                int maxAeRegions,
                                float[] cameraApertures,
                                float[] focalLength) {
        this.sensorOrientation = sensorOrientation;
        this.isoMin = isoMin;
        this.isoMax = isoMax;
        this.exposureTimeMin = exposureTimeMin;
        this.exposureTimeMax = exposureTimeMax;
        this.maxAfRegions = maxAfRegions;
        this.maxAeRegions = maxAeRegions;
        this.cameraApertures = cameraApertures;
        this.focalLength = focalLength;
    }
}
