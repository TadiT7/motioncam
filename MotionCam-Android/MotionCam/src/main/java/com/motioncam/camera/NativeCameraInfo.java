package com.motioncam.camera;

import java.util.Arrays;

public class NativeCameraInfo {
    public final String cameraId;
    public final boolean isFrontFacing;
    public final int exposureCompRangeMin;
    public final int exposureCompRangeMax;
    public final int exposureCompStepNumerator;
    public final int exposureCompStepDenominator;
    public final int[] fpsRange;

    public NativeCameraInfo(
            String cameraId,
            boolean isFrontFacing,
            int exposureCompRangeMin,
            int exposureCompRangeMax,
            int exposureCompStepNumerator,
            int exposureCompStepDenominator,
            int[] fpsRange)
    {
        this.cameraId = cameraId;
        this.isFrontFacing = isFrontFacing;
        this.exposureCompRangeMin = exposureCompRangeMin;
        this.exposureCompRangeMax = exposureCompRangeMax;
        this.exposureCompStepNumerator = exposureCompStepNumerator;
        this.exposureCompStepDenominator = exposureCompStepDenominator;
        this.fpsRange = fpsRange;
    }

    @Override
    public String toString() {
        return "NativeCameraInfo{" +
                "cameraId='" + cameraId + '\'' +
                ", isFrontFacing=" + isFrontFacing +
                ", exposureCompRangeMin=" + exposureCompRangeMin +
                ", exposureCompRangeMax=" + exposureCompRangeMax +
                ", exposureCompStepNumerator=" + exposureCompStepNumerator +
                ", exposureCompStepDenominator=" + exposureCompStepDenominator +
                ", fpsRange=" + Arrays.toString(fpsRange) +
                '}';
    }
}
