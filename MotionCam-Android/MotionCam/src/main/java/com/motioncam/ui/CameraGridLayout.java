package com.motioncam.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.motioncam.R;

public class CameraGridLayout extends FrameLayout {
    private Paint mPaintLines;
    private Paint mPaintCrop;

    private boolean mCropMode;
    private int mCropHorizontal;
    private int mCropVertical;

    public CameraGridLayout(@NonNull Context context) {
        super(context);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);

        createPaint();
        setWillNotDraw(false);
    }

    void createPaint() {
        mPaintLines = new Paint();

        mPaintLines.setAntiAlias(true);
        mPaintLines.setStrokeWidth(1);
        mPaintLines.setStyle(Paint.Style.STROKE);
        mPaintLines.setColor(Color.argb(96, 255, 255, 255));

        mPaintCrop = new Paint();

        mPaintCrop.setAntiAlias(false);
        mPaintCrop.setStyle(Paint.Style.FILL);
        mPaintCrop.setColor(getContext().getColor(R.color.background));
    }

    public void setCropMode(boolean cropMode, int cropHorizontal, int cropVertical) {
        mCropMode = cropMode;
        mCropHorizontal = cropHorizontal;
        mCropVertical = cropVertical;

        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int width = getWidth();
        int height = getHeight();

        if(mCropMode) {
            float croppedWidth = 0.5f * (width * (mCropHorizontal / 100.0f));
            float croppedHeight = 0.5f * (width * (mCropVertical / 100.0f));

            // Top
            canvas.drawRect(
                    0,      0,
                    width,  croppedHeight,
                    mPaintCrop);

            // Bottom
            canvas.drawRect(
                    0,      height - croppedHeight,
                    width,  height,
                    mPaintCrop);

            // Left
            canvas.drawRect(
                    0,              0,
                    croppedWidth,   height,
                    mPaintCrop);

            // Right
            canvas.drawRect(
                    width - croppedWidth,   0,
                    width,                  height,
                    mPaintCrop);
        }
        else {
            canvas.drawLine(width/3, 0, width/3, height, mPaintLines);
            canvas.drawLine(width/3*2, 0, width/3*2, height, mPaintLines);

            canvas.drawLine(0, height/3, width, height/3, mPaintLines);
            canvas.drawLine(0, height/3*2, width, height/3*2, mPaintLines);
        }
    }
}
