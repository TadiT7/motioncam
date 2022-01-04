package com.motioncam.camera;

import java.util.Arrays;

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
    public final float minFocusDistance;
    public final float hyperFocalDistance;
    public final boolean oisSupport;

    public NativeCameraMetadata(int sensorOrientation,
                                int isoMin,
                                int isoMax,
                                long exposureTimeMin,
                                long exposureTimeMax,
                                int maxAfRegions,
                                int maxAeRegions,
                                float[] cameraApertures,
                                float[] focalLength,
                                float minFocusDistance,
                                float hyperFocalDistance,
                                boolean oisSupport)
    {
        this.sensorOrientation = sensorOrientation;
        this.isoMin = isoMin;
        this.isoMax = isoMax;
        this.exposureTimeMin = exposureTimeMin;
        this.exposureTimeMax = exposureTimeMax;
        this.maxAfRegions = maxAfRegions;
        this.maxAeRegions = maxAeRegions;
        this.cameraApertures = cameraApertures;
        this.focalLength = focalLength;
        this.minFocusDistance = minFocusDistance*8;
        this.hyperFocalDistance = hyperFocalDistance/8;
        this.oisSupport = oisSupport;
    }

    @Override
    public String toString() {
        return "NativeCameraMetadata{" +
                "sensorOrientation=" + sensorOrientation +
                ", isoMin=" + isoMin +
                ", isoMax=" + isoMax +
                ", exposureTimeMin=" + exposureTimeMin +
                ", exposureTimeMax=" + exposureTimeMax +
                ", maxAfRegions=" + maxAfRegions +
                ", maxAeRegions=" + maxAeRegions +
                ", cameraApertures=" + Arrays.toString(cameraApertures) +
                ", focalLength=" + Arrays.toString(focalLength) +
                ", minFocusDistance=" + minFocusDistance +
                ", hyperFocalDistance=" + hyperFocalDistance +
                ", oisSupport=" + oisSupport +
                '}';
    }
}
