package com.motioncam.processor;

import android.graphics.Bitmap;

public interface NativeRawVideoPreviewListener {
    Bitmap createBitmap(int width, int height);
}
