package com.motioncam.processor;

import android.graphics.Bitmap;

public interface NativeBitmapListener {
    Bitmap createBitmap(int width, int height, int type);
}
