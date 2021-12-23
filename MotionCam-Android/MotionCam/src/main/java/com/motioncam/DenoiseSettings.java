package com.motioncam;

public class DenoiseSettings {
    public int numMergeImages;

    @Override
    public String toString() {
        return "DenoiseSettings{" +
                "numMergeImages=" + numMergeImages +
                '}';
    }

    private void estimateFromExposure(float ev, float shadows) {
        int mergeImages;

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
        }
        else {
            mergeImages             = 12;
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
