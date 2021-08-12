package com.motioncam;

public class DenoiseSettings {
    public float spatialWeight;
    public int numMergeImages;
    public float sharpen0;
    public float sharpen1;

    double log2(double v) {
        return Math.log(v) / Math.log(2);
    }

    @Override
    public String toString() {
        return "DenoiseSettings{" +
                "spatialWeight=" + spatialWeight +
                ", numMergeImages=" + numMergeImages +
                ", sharpen0=" + sharpen0 +
                ", sharpen1=" + sharpen1 +
                '}';
    }

    private void estimateFromExposure(float ev, float shadows) {
        int mergeImages;
        float chromaBlendWeight;
        float spatialDenoiseWeight;

        sharpen0 = 2.0f;
        sharpen1 = 2.0f;

        if(ev > 11.99) {
            spatialDenoiseWeight    = 0.25f;
            mergeImages             = 0;
        }
        else if(ev > 9.99) {
            spatialDenoiseWeight    = 0.5f;
            mergeImages             = 2;
        }
        else if(ev > 7.99) {
            spatialDenoiseWeight    = 1.0f;
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            spatialDenoiseWeight    = 1.0f;
            mergeImages             = 6;
        }
        else if(ev > 3.99) {
            spatialDenoiseWeight    = 1.0f;
            mergeImages             = 8;
        }
        else if(ev > 0) {
            spatialDenoiseWeight    = 1.5f;
            mergeImages             = 10;
            sharpen1                = 2.5f;
        }
        else {
            spatialDenoiseWeight    = 1.5f;
            mergeImages             = 12;
            sharpen1                = 2.5f;
        }

        if(shadows > 7.99) {
            mergeImages             += 2;
        }

        if(shadows > 15.99) {
            mergeImages             += 2;
        }

        this.numMergeImages     = mergeImages;
        this.spatialWeight      = spatialDenoiseWeight;
    }

    public DenoiseSettings(float noiseProfile, float ev, float shadows) {
        estimateFromExposure(ev, shadows);
    }
}
