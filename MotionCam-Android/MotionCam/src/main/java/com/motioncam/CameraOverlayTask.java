package com.motioncam;

import android.app.Activity;
import android.graphics.Bitmap;

import com.motioncam.camera.NativeCamera;
import com.motioncam.databinding.CameraActivityBinding;

import java.util.TimerTask;

class CameraOverlayTask extends TimerTask {
    private final CameraActivityBinding mBinding;
    private final NativeCamera mNativeCamera;
    private final Activity mActivity;

    private Bitmap mWhiteLevel = null;
    private Bitmap mBlackLevel = null;

    // TODO: Move this
    final int TYPE_WHITE_LEVEL = 1;
    final int TYPE_BLACK_LEVEL = 2;

    public CameraOverlayTask(NativeCamera nativeCamera, CameraActivityBinding binding, Activity activity) {
        mNativeCamera = nativeCamera;
        mBinding = binding;
        mActivity = activity;
    }

    @Override
    public void run() {
        mNativeCamera.generateStats((width, height, type) -> {
            if (type == TYPE_WHITE_LEVEL) {
                if (mWhiteLevel == null)
                    mWhiteLevel = Bitmap.createBitmap(width, height, Bitmap.Config.ALPHA_8);
                return mWhiteLevel;
            } else if (type == TYPE_BLACK_LEVEL) {
                if (mBlackLevel == null)
                    mBlackLevel = Bitmap.createBitmap(width, height, Bitmap.Config.ALPHA_8);
                return mBlackLevel;
            }
            return null;
        });

        mActivity.runOnUiThread(() -> {
            mBinding.cameraOverlayClipLow.setImageBitmap(mBlackLevel);
            mBinding.cameraOverlayClipHigh.setImageBitmap(mWhiteLevel);
        });
    }
}
