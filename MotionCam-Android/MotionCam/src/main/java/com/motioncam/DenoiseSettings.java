package com.motioncam;

public class DenoiseSettings {
    public int numMergeImages;
    public float sharpen0;
    public float sharpen1;

    double log2(double v) {
        return Math.log(v) / Math.log(2);
    }

    @Override
    public String toString() {
        return "DenoiseSettings{" +
                "numMergeImages=" + numMergeImages +
                ", sharpen0=" + sharpen0 +
                ", sharpen1=" + sharpen1 +
                '}';
    }

    private void estimateFromExposure(float ev, float shadows) {
        int mergeImages;
        float chromaBlendWeight;

        sharpen0 = 2.0f;
        sharpen1 = 2.0f;

        if(ev > 7.99) {
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            mergeImages             = 6;
        }
        else if(ev > 3.99) {
            mergeImages             = 8;
        }
        else if(ev > 0) {
            mergeImages             = 12;
            sharpen0                = 2.0f;
            sharpen1                = 3.0f;
        }
        else {
            mergeImages             = 12;
            sharpen0                = 2.0f;
            sharpen1                = 3.0f;
        }

        if(shadows > 7.99) {
            mergeImages             += 4;
        }

        if(shadows > 15.99) {
            mergeImages             += 2;
        }

        this.numMergeImages     = mergeImages;
    }

    public DenoiseSettings(float noiseProfile, float ev, float shadows) {
        estimateFromExposure(ev, shadows);
    }
}
