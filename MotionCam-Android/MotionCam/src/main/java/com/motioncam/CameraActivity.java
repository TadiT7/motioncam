package com.motioncam;

import android.Manifest;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.graphics.drawable.Drawable;
import android.location.Location;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.Size;
import android.util.TypedValue;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.BounceInterpolator;
import android.widget.FrameLayout;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.motion.widget.MotionLayout;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.widget.ImageViewCompat;
import androidx.viewpager2.widget.ViewPager2;

import com.bumptech.glide.Glide;
import com.google.android.gms.location.FusedLocationProviderClient;
import com.google.android.gms.location.LocationCallback;
import com.google.android.gms.location.LocationRequest;
import com.google.android.gms.location.LocationResult;
import com.google.android.gms.location.LocationServices;
import com.jakewharton.processphoenix.ProcessPhoenix;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.CameraManualControl;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.NativeCameraInfo;
import com.motioncam.camera.NativeCameraMetadata;
import com.motioncam.camera.NativeCameraSessionBridge;
import com.motioncam.camera.PostProcessSettings;
import com.motioncam.databinding.CameraActivityBinding;
import com.motioncam.model.CameraProfile;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.processor.ProcessorReceiver;
import com.motioncam.processor.ProcessorService;
import com.motioncam.ui.BitmapDrawView;
import com.motioncam.ui.CameraCapturePreviewAdapter;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;

public class CameraActivity extends AppCompatActivity implements
        SensorEventManager.SensorEventHandler,
        TextureView.SurfaceTextureListener,
        NativeCameraSessionBridge.CameraSessionListener,
        NativeCameraSessionBridge.CameraRawPreviewListener,
        View.OnTouchListener,
        ProcessorReceiver.Receiver,
        MotionLayout.TransitionListener, AsyncNativeCameraOps.CaptureImageListener {

    public static final String TAG = "MotionCam";

    private static final int PERMISSION_REQUEST_CODE = 1;
    private static final int SETTINGS_ACTIVITY_REQUEST_CODE = 0x10;

    private static final CameraManualControl.SHUTTER_SPEED MAX_EXPOSURE_TIME = CameraManualControl.SHUTTER_SPEED.EXPOSURE_30__0;

    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.ACCESS_FINE_LOCATION
    };

    private enum FocusState {
        AUTO,
        FIXED,
        FIXED_AF_AE
    }

    private enum CaptureMode {
        NIGHT,
        ZSL,
        BURST,
        RAW_VIDEO
    }

    private enum PreviewControlMode {
        CONTRAST,
        COLOUR,
        TINT,
        WARMTH
    }

    private enum FaceDetectionMode {
        FAST,
        NORMAL
    }

    private static class Settings {
        boolean useDualExposure;
        boolean saveDng;
        boolean autoNightMode;
        boolean hdr;
        float contrast;
        float saturation;
        float temperatureOffset;
        float tintOffset;
        int jpegQuality;
        long memoryUseBytes;
        CaptureMode captureMode;
        SettingsViewModel.RawMode rawMode;
        int cameraPreviewQuality;

        void load(SharedPreferences prefs) {
            this.jpegQuality = prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY);
            this.contrast = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, CameraProfile.DEFAULT_CONTRAST / 100.0f);
            this.saturation = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, 1.00f);
            this.temperatureOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, 0);
            this.tintOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, 0);
            this.saveDng = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, false);
            this.autoNightMode = prefs.getBoolean(SettingsViewModel.PREFS_KEY_AUTO_NIGHT_MODE, true);
            this.hdr = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, true);

            long nativeCameraMemoryUseMb = prefs.getInt(SettingsViewModel.PREFS_KEY_MEMORY_USE_MBYTES, SettingsViewModel.MINIMUM_MEMORY_USE_MB);
            nativeCameraMemoryUseMb = Math.min(nativeCameraMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);

            this.useDualExposure = prefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false);
            this.memoryUseBytes = nativeCameraMemoryUseMb * 1024 * 1024;

            this.captureMode =
                    CaptureMode.valueOf(prefs.getString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, CaptureMode.ZSL.name()));

            String captureModeStr = prefs.getString(SettingsViewModel.PREFS_KEY_CAPTURE_MODE, SettingsViewModel.RawMode.RAW10.name());
            this.rawMode = SettingsViewModel.RawMode.valueOf(captureModeStr);

            switch (prefs.getInt(SettingsViewModel.PREFS_KEY_CAMERA_PREVIEW_QUALITY, 0)) {
                default:
                case 0: // Low
                    this.cameraPreviewQuality = 4;
                    break;
                case 1: // Medium
                    this.cameraPreviewQuality = 3;
                    break;
                case 2: // High
                    this.cameraPreviewQuality = 2;
                    break;
            }
        }

        void save(SharedPreferences prefs) {
            prefs.edit()
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, this.contrast)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, this.saturation)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, this.temperatureOffset)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, this.tintOffset)
                    .putBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, this.saveDng)
                    .putBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, this.hdr)
                    .putString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, this.captureMode.name())
                    .apply();
        }

        @Override
        public String toString() {
            return "Settings{" +
                    "useDualExposure=" + useDualExposure +
                    ", saveDng=" + saveDng +
                    ", hdr=" + hdr +
                    ", autoNightMode=" + autoNightMode +
                    ", contrast=" + contrast +
                    ", saturation=" + saturation +
                    ", temperatureOffset=" + temperatureOffset +
                    ", tintOffset=" + tintOffset +
                    ", jpegQuality=" + jpegQuality +
                    ", memoryUseBytes=" + memoryUseBytes +
                    ", captureMode=" + captureMode +
                    ", rawMode=" + rawMode +
                    ", cameraPreviewQuality=" + cameraPreviewQuality +
                    '}';
        }
    }

    private class FaceDetectionTask extends TimerTask {
        @Override
        public void run() {
            if(mNativeCamera == null)
                mNativeCamera = null;

            final RectF[] faces = mNativeCamera.detectFaces();
            int selectedFace = 0;

            if(faces == null || faces.length == 0) {
                if(mFaceDetectionMode == FaceDetectionMode.FAST) {
                    mFaceDetectionTimer.cancel();

                    mFaceDetectionTimer = new Timer("FaceDetection");
                    mFaceDetectionTimer.scheduleAtFixedRate(new FaceDetectionTask(), 1000, 1000);
                    mFaceDetectionMode = FaceDetectionMode.NORMAL;
                }

                return;
            }

            // Use largest found face
            for(int i = 1; i < faces.length; i++) {
                if((faces[i].width() * faces[i].width()) > (faces[selectedFace].width() * faces[selectedFace].width()))
                {
                    selectedFace = i;
                }
            }

            final int finalSelectedFace = selectedFace;

            mFaceDetectionTimer.cancel();

            mFaceDetectionTimer = new Timer("FaceDetection");
            mFaceDetectionTimer.scheduleAtFixedRate(new FaceDetectionTask(), 500, 500);
            mFaceDetectionMode = FaceDetectionMode.FAST;

            runOnUiThread(() -> {
                Matrix m = new Matrix();
                float[] pts = new float[]{ faces[finalSelectedFace].centerX(), faces[finalSelectedFace].centerY() };

                m.setRotate(mSensorEventManager.getOrientation().angle, 0.5f, 0.5f);
                m.mapPoints(pts);

                onSetFocusPt(pts[0] * mTextureView.getWidth(), pts[1] * mTextureView.getHeight());
            });
        }
    }

    private Settings mSettings;
    private boolean mHavePermissions;
    private TextureView mTextureView;
    private Surface mSurface;
    private CameraActivityBinding mBinding;
    private List<CameraManualControl.SHUTTER_SPEED> mExposureValues;
    private List<CameraManualControl.ISO> mIsoValues;
    private NativeCameraSessionBridge mNativeCamera;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private List<NativeCameraInfo> mCameraInfos;
    private NativeCameraInfo mSelectedCamera;
    private NativeCameraMetadata mCameraMetadata;
    private SensorEventManager mSensorEventManager;
    private FusedLocationProviderClient mFusedLocationClient;
    private ProcessorReceiver mProgressReceiver;
    private Location mLastLocation;
    private String mVideoOutputPath;

    private CameraCapturePreviewAdapter mCameraCapturePreviewAdapter;

    private final ViewPager2.OnPageChangeCallback mCapturedPreviewPagerListener = new ViewPager2.OnPageChangeCallback() {
        @Override
        public void onPageSelected(int position) {
            onCapturedPreviewPageChanged(position);
        }
    };

    private final LocationCallback mLocationCallback = new LocationCallback() {
        public void onLocationResult(LocationResult locationResult) {
            if (locationResult == null) {
                return;
            }

            onReceivedLocation(locationResult.getLastLocation());
        }
    };

    private final SeekBar.OnSeekBarChangeListener mSeekBarChangeListener = new SeekBar.OnSeekBarChangeListener() {

        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            if(seekBar == mBinding.shadowsSeekBar) {
                onShadowsSeekBarChanged(progress);
            }
            else if(seekBar == mBinding.exposureSeekBar) {
                onExposureCompSeekBarChanged(progress);
            }
            else if(seekBar == mBinding.previewFrame.previewSeekBar) {
                if (fromUser)
                    updatePreviewControlsParam(progress);
            }
            else if(seekBar == findViewById(R.id.manualControlIsoSeekBar)) {
                onManualControlSettingsChanged(progress, fromUser);
            }
            else if(seekBar == findViewById(R.id.manualControlShutterSpeedSeekBar)) {
                onManualControlSettingsChanged(progress, fromUser);
            }
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
        }
    };

    private PostProcessSettings mPostProcessSettings = new PostProcessSettings();
    private PostProcessSettings mEstimatedSettings = new PostProcessSettings();

    private float mTemperatureOffset;
    private float mTintOffset;

    private boolean mManualControlsEnabled;
    private boolean mManualControlsSet;
    private CaptureMode mCaptureMode = CaptureMode.NIGHT;
    private PreviewControlMode mPreviewControlMode = PreviewControlMode.CONTRAST;
    private boolean mUserCaptureModeOverride;

    private FocusState mFocusState = FocusState.AUTO;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private int mIso;
    private long mExposureTime;
    private float mShadowOffset;
    private AtomicBoolean mImageCaptureInProgress = new AtomicBoolean(false);
    private long mFocusRequestedTimestampMs;
    private Timer mFaceDetectionTimer;
    private FaceDetectionMode mFaceDetectionMode = FaceDetectionMode.NORMAL;

    @Override
    public void onBackPressed() {
        if (mBinding.main.getCurrentState() == mBinding.main.getEndState()) {
            mBinding.main.transitionToStart();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        onWindowFocusChanged(true);

        // Clear out previous preview files
        File previewDirectory = new File(getFilesDir(), ProcessorService.PREVIEW_PATH);
        long now = new Date().getTime();

        File[] previewFiles = previewDirectory.listFiles();
        if(previewFiles != null) {
            for (File f : previewFiles) {
                long diff = now - f.lastModified();
                if (diff > 2 * 24 * 60 * 60 * 1000) // 2 days old
                    f.delete();
            }
        }

        mBinding = CameraActivityBinding.inflate(getLayoutInflater());
        setContentView(mBinding.getRoot());

        mSettings = new Settings();
        mProgressReceiver = new ProcessorReceiver(new Handler());

        mBinding.focusLockPointFrame.setOnClickListener(v -> onFixedFocusCancelled());
        mBinding.previewFrame.settingsBtn.setOnClickListener(v -> onSettingsClicked());

        mCameraCapturePreviewAdapter = new CameraCapturePreviewAdapter(getApplicationContext());
        mBinding.previewPager.setAdapter(mCameraCapturePreviewAdapter);

        mBinding.shareBtn.setOnClickListener(this::share);
        mBinding.openBtn.setOnClickListener(this::open);

        mBinding.onBackFromPreviewBtn.setOnClickListener(v -> mBinding.main.transitionToStart());
        mBinding.main.setTransitionListener(this);

        mBinding.shadowsSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.exposureSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.previewFrame.previewSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setOnSeekBarChangeListener(mSeekBarChangeListener);
        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((Switch) findViewById(R.id.manualControlSwitch)).setOnCheckedChangeListener((buttonView, isChecked) -> onCameraManualControlEnabled(isChecked));

        // Buttons
        mBinding.captureBtn.setOnTouchListener((v, e) -> onCaptureTouched(e));
        mBinding.captureBtn.setOnClickListener(v -> onCaptureClicked());
        mBinding.switchCameraBtn.setOnClickListener(v -> onSwitchCameraClicked());

        mBinding.nightModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.burstModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.zslModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.rawVideoModeBtn.setOnClickListener(this::onCaptureModeClicked);

        mBinding.previewFrame.contrastBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.colourBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.tintBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.warmthBtn.setOnClickListener(this::onPreviewModeClicked);

        mSensorEventManager = new SensorEventManager(this, this);
        mFusedLocationClient = LocationServices.getFusedLocationProviderClient(this);

        requestPermissions();
    }

    private void open(View view) {
        Uri uri = mCameraCapturePreviewAdapter.getOutput(mBinding.previewPager.getCurrentItem());
        if (uri == null)
            return;

        Intent openIntent = new Intent();

        openIntent.setAction(Intent.ACTION_VIEW);
        openIntent.setDataAndType(uri, "image/jpeg");
        openIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_ACTIVITY_NO_HISTORY);

        startActivity(openIntent);
    }

    private void share(View view) {
        Uri uri = mCameraCapturePreviewAdapter.getOutput(mBinding.previewPager.getCurrentItem());
        if (uri == null)
            return;

        Intent shareIntent = new Intent();

        shareIntent.setAction(Intent.ACTION_SEND);
        shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
        shareIntent.setType("image/jpeg");

        startActivity(Intent.createChooser(shareIntent, getResources().getText(R.string.send_to)));
    }

    private void onExposureCompSeekBarChanged(int progress) {
        if (mNativeCamera != null) {
            float value = progress / (float) mBinding.exposureSeekBar.getMax();
            mNativeCamera.setExposureCompensation(value);
        }
    }

    private void onSettingsClicked() {
        Intent intent = new Intent(this, SettingsActivity.class);
        startActivityForResult(intent, SETTINGS_ACTIVITY_REQUEST_CODE);
    }

    private void onFixedFocusCancelled() {
        setFocusState(FocusState.AUTO, null);
    }

    private void onCameraManualControlEnabled(boolean enabled) {
        if (mManualControlsEnabled == enabled)
            return;

        mManualControlsEnabled = enabled;
        mManualControlsSet = false;

        if (mManualControlsEnabled) {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.VISIBLE);
            findViewById(R.id.infoFrame).setVisibility(View.GONE);
            mBinding.exposureLayout.setVisibility(View.GONE);
        } else {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.GONE);
            findViewById(R.id.infoFrame).setVisibility(View.VISIBLE);
            findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

            mBinding.exposureLayout.setVisibility(View.VISIBLE);

            if (mNativeCamera != null) {
                mNativeCamera.setAutoExposure();
            }
        }

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void setPostProcessingDefaults() {
        // Set initial preview values
        mPostProcessSettings.shadows = -1.0f;
        mPostProcessSettings.contrast = mSettings.contrast;
        mPostProcessSettings.saturation = mSettings.saturation;
        mPostProcessSettings.greens = 4.0f;
        mPostProcessSettings.blues = 6.0f;
        mPostProcessSettings.sharpen0 = 2.25f;
        mPostProcessSettings.sharpen1 = 2.0f;
        mPostProcessSettings.pop = 1.25f;
        mPostProcessSettings.whitePoint = -1;
        mPostProcessSettings.blacks = -1;
        mPostProcessSettings.tonemapVariance = 0.25f;
        mPostProcessSettings.jpegQuality = mSettings.jpegQuality;

        mTemperatureOffset = mSettings.temperatureOffset;
        mTintOffset = mSettings.tintOffset;
        mPostProcessSettings.dng = mSettings.saveDng;

        mShadowOffset = 0.0f;
    }

    @Override
    protected void onResume() {
        super.onResume();

        mSensorEventManager.enable();
        mProgressReceiver.setReceiver(this);

        mBinding.rawCameraPreview.setBitmap(null);
        mBinding.main.transitionToStart();

        // Load UI settings
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        mSettings.load(sharedPrefs);

        Log.d(TAG, mSettings.toString());

        setPostProcessingDefaults();
        updatePreviewTabUi(true);

        setCaptureMode(mSettings.captureMode);
        setSaveRaw(mSettings.saveDng);
        setHdr(mSettings.hdr);

        // Reset manual controls
        ((Switch) findViewById(R.id.manualControlSwitch)).setChecked(false);
        updateManualControlView(mSensorEventManager.getOrientation());

        mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);
        mBinding.previewPager.registerOnPageChangeCallback(mCapturedPreviewPagerListener);

        mFocusState = FocusState.AUTO;
        mAutoFocusPoint = null;
        mAutoExposurePoint = null;
        mUserCaptureModeOverride = false;

        // Start camera when we have all the permissions
        if (mHavePermissions) {
            initCamera();

            // Request location updates
            if (    ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                ||  ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED)
            {
                LocationRequest locationRequest = LocationRequest.create();

                mFusedLocationClient.requestLocationUpdates(locationRequest, mLocationCallback, Looper.getMainLooper());
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        // Save UI settings
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        // Update the settings
        mSettings.contrast = mPostProcessSettings.contrast;
        mSettings.saturation = mPostProcessSettings.saturation;
        mSettings.tintOffset = mTintOffset;
        mSettings.temperatureOffset = mTemperatureOffset;
        mSettings.saveDng = mPostProcessSettings.dng;
        mSettings.captureMode = mCaptureMode;

        mSettings.save(sharedPrefs);

        mSensorEventManager.disable();
        mProgressReceiver.setReceiver(null);
        mBinding.previewPager.unregisterOnPageChangeCallback(mCapturedPreviewPagerListener);

        if(mFaceDetectionTimer != null) {
            mFaceDetectionTimer.cancel();
            mFaceDetectionTimer = null;
        }

        if(mNativeCamera != null) {
            if(mImageCaptureInProgress.getAndSet(false) && mCaptureMode == CaptureMode.RAW_VIDEO) {
                finaliseRawVideo();
            }

            mNativeCamera.stopCapture();
        }

        if(mSurface != null) {
            mSurface.release();
            mSurface = null;
        }

        mBinding.cameraFrame.removeView(mTextureView);
        mTextureView = null;

        mFusedLocationClient.removeLocationUpdates(mLocationCallback);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if(mNativeCamera != null) {
            mNativeCamera.destroy();
            mNativeCamera = null;
        }
    }

    private void finaliseRawVideo() {
        ProgressDialog dialog = new ProgressDialog(this, R.style.BasicDialog);

        dialog.setIndeterminate(true);
        dialog.setCancelable(false);
        dialog.setTitle("Please Wait");
        dialog.setMessage("Finalising video to " + mVideoOutputPath);

        dialog.show();

        AsyncTask.execute(() -> {
            mNativeCamera.endStream();
            dialog.dismiss();
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if(requestCode == SETTINGS_ACTIVITY_REQUEST_CODE) {
            // Restart process when coming back from settings since we may need to reload the camera library
            ProcessPhoenix.triggerRebirth(this);
        }
    }

    private void onCameraSelectionChanged(View view) {
        setActiveCamera((String) view.getTag());
    }

    private void setActiveCamera(String cameraId) {
        if(cameraId.equals(mSelectedCamera.cameraId))
            return;

        mSelectedCamera = null;

        for(NativeCameraInfo cameraInfo : mCameraInfos) {
            if(cameraId.equals(cameraInfo.cameraId)) {
                mSelectedCamera = cameraInfo;
            }
        }

        // Fade out current preview
        mBinding.cameraFrame.animate()
                .alpha(0)
                .setDuration(250)
                .start();

        // Update selection
        ViewGroup cameraSelectionFrame = findViewById(R.id.cameraSelection);
        for(int i = 0; i < cameraSelectionFrame.getChildCount(); i++) {
            View cameraSelection = cameraSelectionFrame.getChildAt(i);

            if(cameraSelection.getTag().equals(cameraId)) {
                ImageViewCompat.setImageTintList(
                        cameraSelection.findViewById(R.id.cameraZoomImage),
                        ColorStateList.valueOf(ContextCompat.getColor(this, R.color.colorAccent)));
            }
            else {
                ImageViewCompat.setImageTintList(
                        cameraSelection.findViewById(R.id.cameraZoomImage),
                        ColorStateList.valueOf(ContextCompat.getColor(this, R.color.white)));
            }
        }

        // Stop the camera in the background then start the new camera
        CompletableFuture
                .runAsync(() -> mNativeCamera.stopCapture())
                .thenRun(() -> runOnUiThread(() -> {
                    mBinding.cameraFrame.removeView(mTextureView);
                    mTextureView = null;
                    ((BitmapDrawView) findViewById(R.id.rawCameraPreview)).setBitmap(null);

                    if(mSurface != null) {
                        mSurface.release();
                        mSurface = null;
                    }

                    mBinding.cameraFrame.setAlpha(1.0f);
                    initCamera();
                }));
    }

    private void onSwitchCameraClicked() {
        if(mCameraInfos == null || mCameraInfos.isEmpty()) {
            return;
        }

        boolean currentFrontFacing = mSelectedCamera == null ? false : mSelectedCamera.isFrontFacing;

        // Rotate switch camera button
        mBinding.switchCameraBtn.setEnabled(false);

        int rotation = (int) mBinding.switchCameraBtn.getRotation();
        rotation = (rotation + 180) % 360;

        mBinding.switchCameraBtn.animate()
                .rotation(rotation)
                .setDuration(250)
                .start();

        if(currentFrontFacing) {
            setActiveCamera(mCameraInfos.get(0).cameraId);
        }
        else {
            for (int i = 0; i < mCameraInfos.size(); i++) {
                if (mCameraInfos.get(i).isFrontFacing) {
                    setActiveCamera(mCameraInfos.get(i).cameraId);
                }
            }
        }
    }

    private void updatePreviewTabUi(boolean updateModeSelection) {
        final float seekBarMax = mBinding.previewFrame.previewSeekBar.getMax();
        int progress = Math.round(seekBarMax / 2);

        View selectionView = null;

        mBinding.previewFrame.contrastValue.setText(
                getString(R.string.value_percent, Math.round(mPostProcessSettings.contrast * 100)));

        int saturationProgress;

        if(mPostProcessSettings.saturation <= 1.0f) {
            saturationProgress = Math.round(mPostProcessSettings.saturation * 50);
        }
        else {
            saturationProgress = Math.round(50 + (50 * ((mPostProcessSettings.saturation / 1.25f) - 0.8f) / 0.2f));
        }

        mBinding.previewFrame.colourValue.setText(getString(R.string.value_percent, saturationProgress));

        mBinding.previewFrame.warmthValue.setText(
                getString(R.string.value_percent, Math.round((mTemperatureOffset + 1000.0f) / 2000.0f * 100.0f)));

        mBinding.previewFrame.tintValue.setText(
                getString(R.string.value_percent, Math.round((mTintOffset + 50.0f) / 100.0f * 100.0f)));

        if(updateModeSelection) {
            switch(mPreviewControlMode) {
                case CONTRAST:
                    progress = Math.round(mPostProcessSettings.contrast * seekBarMax);
                    selectionView = mBinding.previewFrame.contrastBtn;
                    break;

                case COLOUR:
                    progress = Math.round(mPostProcessSettings.saturation / 2.0f * seekBarMax);
                    selectionView = mBinding.previewFrame.colourBtn;
                    break;

                case TINT:
                    progress = Math.round(((mTintOffset + 50.0f) / 100.0f * seekBarMax));
                    selectionView = mBinding.previewFrame.tintBtn;
                    break;

                case WARMTH:
                    progress = Math.round(((mTemperatureOffset + 1000.0f) / 2000.0f * seekBarMax));
                    selectionView = mBinding.previewFrame.warmthBtn;
                    break;
            }

            mBinding.previewFrame.contrastBtn.setBackground(null);
            mBinding.previewFrame.colourBtn.setBackground(null);
            mBinding.previewFrame.tintBtn.setBackground(null);
            mBinding.previewFrame.warmthBtn.setBackground(null);

            selectionView.setBackgroundColor(getColor(R.color.colorPrimaryDark));
            mBinding.previewFrame.previewSeekBar.setProgress(progress);
        }
    }

    private void setCaptureMode(CaptureMode captureMode) {
        if(mCaptureMode == captureMode)
            return;

        mBinding.nightModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.zslModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.burstModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.rawVideoModeBtn.setTextColor(getColor(R.color.textColor));

        mCaptureMode = captureMode;

        switch(mCaptureMode) {
            case NIGHT:
                mBinding.nightModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case ZSL:
                mBinding.zslModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case BURST:
                mBinding.burstModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case RAW_VIDEO:
                mBinding.rawVideoModeBtn.setTextColor(getColor(R.color.colorAccent));
                Toast.makeText(this, "Warning: This is an experimental feature.", Toast.LENGTH_SHORT).show();
                break;
        }
    }

    private void setHdr(boolean hdr) {
        int color = hdr ? R.color.colorAccent : R.color.white;
        mBinding.previewFrame.hdrEnableBtn.setTextColor(getColor(color));

        for (Drawable drawable : mBinding.previewFrame.hdrEnableBtn.getCompoundDrawables()) {
            if (drawable != null) {
                drawable.setColorFilter(new PorterDuffColorFilter(ContextCompat.getColor(this, color), PorterDuff.Mode.SRC_IN));
            }
        }

        mSettings.hdr = hdr;
    }

    private void setSaveRaw(boolean saveRaw) {
        int color = saveRaw ? R.color.colorAccent : R.color.white;
        mBinding.previewFrame.rawEnableBtn.setTextColor(getColor(color));

        for (Drawable drawable : mBinding.previewFrame.rawEnableBtn.getCompoundDrawables()) {
            if (drawable != null) {
                drawable.setColorFilter(new PorterDuffColorFilter(ContextCompat.getColor(this, color), PorterDuff.Mode.SRC_IN));
            }
        }

        mPostProcessSettings.dng = saveRaw;
    }

    private void onCaptureModeClicked(View v) {
        // Don't allow capture mode changes during capture
        if(mImageCaptureInProgress.get())
            return;

        mUserCaptureModeOverride = true;

        if(v == mBinding.nightModeBtn) {
            setCaptureMode(CaptureMode.NIGHT);
        }
        else if(v == mBinding.zslModeBtn) {
            setCaptureMode(CaptureMode.ZSL);
        }
        else if(v == mBinding.burstModeBtn) {
            setCaptureMode(CaptureMode.BURST);
        }
        else if(v == mBinding.rawVideoModeBtn) {
            setCaptureMode(CaptureMode.RAW_VIDEO);
        }
    }

    private void onPreviewModeClicked(View v) {
        if(v == mBinding.previewFrame.contrastBtn) {
            mPreviewControlMode = PreviewControlMode.CONTRAST;
        }
        else if(v == mBinding.previewFrame.colourBtn) {
            mPreviewControlMode = PreviewControlMode.COLOUR;
        }
        else if(v == mBinding.previewFrame.tintBtn) {
            mPreviewControlMode = PreviewControlMode.TINT;
        }
        else if(v == mBinding.previewFrame.warmthBtn) {
            mPreviewControlMode = PreviewControlMode.WARMTH;
        }

        updatePreviewTabUi(true);
    }

    private void updatePreviewControlsParam(int progress) {
        final float seekBarMax = mBinding.previewFrame.previewSeekBar.getMax();
        final float halfPoint = seekBarMax / 2.0f;

        switch(mPreviewControlMode) {
            case CONTRAST:
                mPostProcessSettings.contrast = progress / seekBarMax;
                break;

            case COLOUR: {
                int s = progress;
                mPostProcessSettings.saturation = s > halfPoint ? 1.0f + ((s - halfPoint) / halfPoint * 0.25f) : s / seekBarMax * 2.0f;
            }
            break;

            case TINT:
                mTintOffset = (progress / seekBarMax - 0.5f) * 100.0f;
                break;

            case WARMTH:
                mTemperatureOffset = (progress / seekBarMax - 0.5f) * 2000.0f;
                break;
        }

        updatePreviewSettings();
        updatePreviewTabUi(false);
    }

    private void onManualControlSettingsChanged(int progress, boolean fromUser) {
        if (mManualControlsEnabled && mNativeCamera != null && fromUser) {
            int shutterSpeedIdx = ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).getProgress();
            int isoIdx = ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).getProgress();

            CameraManualControl.SHUTTER_SPEED shutterSpeed = mExposureValues.get(shutterSpeedIdx);
            CameraManualControl.ISO iso = mIsoValues.get(isoIdx);

            mNativeCamera.setManualExposureValues(iso.getIso(), shutterSpeed.getExposureTime());

            ((TextView) findViewById(R.id.manualControlIsoText)).setText(iso.toString());
            ((TextView) findViewById(R.id.manualControlShutterSpeedText)).setText(shutterSpeed.toString());

            mManualControlsSet = true;

            // Don't allow night mode
            if (mCaptureMode == CaptureMode.NIGHT)
                setCaptureMode(CaptureMode.ZSL);
        }
    }

    private boolean onCaptureTouched(MotionEvent e) {
        if(mNativeCamera == null)
            return false;

        if(e.getAction() == MotionEvent.ACTION_DOWN && mCaptureMode == CaptureMode.ZSL) {
            // Keep estimated settings now
            try
            {
                mEstimatedSettings = mNativeCamera.getRawPreviewEstimatedPostProcessSettings();
            }
            catch (IOException exception) {
                Log.e(TAG, "Failed to get estimated settings", exception);
                return false;
            }

            boolean useHdr = mSettings.hdr;
            CameraManualControl.Exposure hdrExposure;

            float hdrEv = (float) Math.pow(2.0f, mEstimatedSettings.hdr);

            if(useHdr) {
                hdrExposure = CameraManualControl.Exposure.Create(
                        CameraManualControl.GetClosestShutterSpeed(Math.round(mExposureTime / hdrEv)),
                        CameraManualControl.GetClosestIso(mIsoValues, mIso));

                float a = 1.6f;
                if (mCameraMetadata.cameraApertures.length > 0)
                    a = mCameraMetadata.cameraApertures[0];

                hdrExposure = CameraManualControl.MapToExposureLine(a, hdrExposure, CameraManualControl.HDR_EXPOSURE_LINE);

                mNativeCamera.prepareHdrCapture(hdrExposure.iso.getIso(), hdrExposure.shutterSpeed.getExposureTime());
            }
        }

        return false;
    }

    private void capture(CaptureMode mode) {
        if(mNativeCamera == null)
            return;

        if(mode != CaptureMode.RAW_VIDEO && !mImageCaptureInProgress.compareAndSet(false, true)) {
            Log.e(TAG, "Aborting capture, one is already in progress");
            return;
        }

        // Store capture mode
        mPostProcessSettings.captureMode = mCaptureMode.name();

        float a = 1.6f;

        if(mCameraMetadata.cameraApertures.length > 0)
            a = mCameraMetadata.cameraApertures[0];

        if(mode == CaptureMode.BURST) {
            mImageCaptureInProgress.set(false);

            // Pass native camera handle
            Intent intent = new Intent(this, PostProcessActivity.class);

            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_HANDLE, mNativeCamera.getHandle());
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_ID, mSelectedCamera.cameraId);
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_FRONT_FACING, mSelectedCamera.isFrontFacing);

            startActivity(intent);
        }
        else if(mode == CaptureMode.RAW_VIDEO) {
            if(mImageCaptureInProgress.get()) {
                mImageCaptureInProgress.set(false);

                mBinding.captureProgressBar.setVisibility(View.INVISIBLE);
                mBinding.captureProgressBar.setIndeterminateMode(false);

                mBinding.previewFrame.settingsBtn.setEnabled(true);
                mBinding.switchCameraBtn.setEnabled(true);

                finaliseRawVideo();
            }
            else {
                File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
                File file = new File(dir, CameraProfile.generateFilename());

                mNativeCamera.streamToFile(file.getPath());
                mImageCaptureInProgress.set(true);

                mBinding.captureProgressBar.setVisibility(View.VISIBLE);
                mBinding.captureProgressBar.setIndeterminateMode(true);

                mBinding.previewFrame.settingsBtn.setEnabled(false);
                mBinding.switchCameraBtn.setEnabled(false);

                mVideoOutputPath = file.getPath();
            }
        }
        else if(mode == CaptureMode.NIGHT) {
            mBinding.captureBtn.setEnabled(false);

            mBinding.captureProgressBar.setIndeterminateMode(false);

            PostProcessSettings settings = mPostProcessSettings.clone();

            try
            {
                mEstimatedSettings = mNativeCamera.getRawPreviewEstimatedPostProcessSettings();
            }
            catch (IOException exception) {
                Log.e(TAG, "Failed to get estimated settings", exception);
            }

            // Map camera exposure to our own
            long cameraExposure;
            float hdr = Math.max(1.0f, mEstimatedSettings.hdr);

            float hdrEv = (float) Math.pow(2.0f, hdr);

            cameraExposure = Math.round(mExposureTime * Math.pow(2.0f, mEstimatedSettings.exposure));

            // We'll estimate the shadows again since the exposure has been adjusted
            settings.shadows = -1;

            CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(cameraExposure),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            CameraManualControl.Exposure hdrExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(Math.round(cameraExposure / hdrEv)),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            baseExposure = CameraManualControl.MapToExposureLine(a, baseExposure, CameraManualControl.EXPOSURE_LINE);
            hdrExposure = CameraManualControl.MapToExposureLine(a, hdrExposure, CameraManualControl.HDR_EXPOSURE_LINE);

            DenoiseSettings denoiseSettings = new DenoiseSettings(
                    mEstimatedSettings.noiseSigma,
                    (float) baseExposure.getEv(a),
                    settings.shadows);

            mBinding.cameraFrame
                    .animate()
                    .alpha(0.5f)
                    .setDuration(125)
                    .start();

            settings.exposure = 0.0f;
            settings.temperature = mEstimatedSettings.temperature + mTemperatureOffset;
            settings.tint = mEstimatedSettings.tint + mTintOffset;
            settings.sharpen0 = denoiseSettings.sharpen0;
            settings.sharpen1 = denoiseSettings.sharpen1;

            long exposure = baseExposure.shutterSpeed.getExposureTime();
            int iso = baseExposure.iso.getIso();

            mNativeCamera.captureHdrImage(
                denoiseSettings.numMergeImages,
                iso,
                exposure,
                hdrExposure.iso.getIso(),
                hdrExposure.shutterSpeed.getExposureTime(),
                settings,
                CameraProfile.generateCaptureFile(this).getPath());
        }
        else {
            PostProcessSettings settings = mPostProcessSettings.clone();

            CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(mExposureTime),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            DenoiseSettings denoiseSettings = new DenoiseSettings(
                    0,
                    (float) baseExposure.getEv(a),
                    settings.shadows);

            settings.exposure = 0.0f;
            settings.temperature = mEstimatedSettings.temperature + mTemperatureOffset;
            settings.tint = mEstimatedSettings.tint + mTintOffset;
            settings.shadows = mEstimatedSettings.shadows;
            settings.sharpen0 = denoiseSettings.sharpen0;
            settings.sharpen1 = denoiseSettings.sharpen1;

            mBinding.cameraFrame
                    .animate()
                    .alpha(0.5f)
                    .setDuration(125)
                    .withEndAction(() -> mBinding.cameraFrame
                            .animate()
                            .alpha(1.0f)
                            .setDuration(125)
                            .start())
                    .start();

            mNativeCamera.captureZslHdrImage(
                    denoiseSettings.numMergeImages,
                    settings,
                    CameraProfile.generateCaptureFile(this).getPath());
        }
    }

    private void onCaptureClicked() {
        capture(mCaptureMode);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);

        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE           |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION  |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN       |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION         |
                    View.SYSTEM_UI_FLAG_FULLSCREEN              |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private void requestPermissions() {
        ArrayList<String> needPermissions = new ArrayList<>();

        for(String permission : REQUEST_PERMISSIONS) {
            if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                needPermissions.add(permission);
            }
        }

        if(!needPermissions.isEmpty()) {
            String[] permissions = needPermissions.toArray(new String[0]);
            ActivityCompat.requestPermissions(this, permissions, PERMISSION_REQUEST_CODE);
        }
        else {
            onPermissionsGranted();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (PERMISSION_REQUEST_CODE != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        // Check if camera permission has been denied
        for(int i = 0; i < permissions.length; i++) {
            if(grantResults[i] == PackageManager.PERMISSION_DENIED && permissions[i].equals(Manifest.permission.CAMERA)) {
                runOnUiThread(this::onPermissionsDenied);
                return;
            }
        }

        runOnUiThread(this::onPermissionsGranted);
    }

    private void onPermissionsGranted() {
        mHavePermissions = true;

        // Kick off image processor in case there are images we have not processed
        startImageProcessor();
    }

    private void onPermissionsDenied() {
        mHavePermissions = false;
        finish();
    }

    private void createCamera() {
        // Load our native camera library
        if(mSettings.useDualExposure) {
            try {
                System.loadLibrary("native-camera-opencl");
            } catch (Exception e) {
                e.printStackTrace();
                System.loadLibrary("native-camera-host");
            }
        }
        else {
            System.loadLibrary("native-camera-host");
        }

        mNativeCamera = new NativeCameraSessionBridge(this, mSettings.memoryUseBytes, null);
        mAsyncNativeCameraOps = new AsyncNativeCameraOps(mNativeCamera);

        mCameraInfos = Arrays.asList(mNativeCamera.getSupportedCameras());

        if(mCameraInfos.isEmpty()) {
            // No supported cameras. Display message to user and exist
            AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                    .setCancelable(false)
                    .setTitle(R.string.error)
                    .setMessage(R.string.not_supported_error)
                    .setPositiveButton(R.string.ok, (dialog, which) -> finish());

            dialogBuilder.show();
            return;
        }

        // Pick first camera if none selected
        if(mSelectedCamera == null) {
            mSelectedCamera = mCameraInfos.get(0);
        }
    }

    private void setupCameraSwitchButtons() {
        ViewGroup cameraSelectionFrame = findViewById(R.id.cameraSelection);

        Map<String, Float> cameraMetadataMap = new HashMap<>();
        Set<String> seenFocalLength = new HashSet<>();

        // Get metadata for all available cameras
        for(NativeCameraInfo cameraInfo : mCameraInfos) {
            if(cameraInfo.isFrontFacing)
                continue;

            NativeCameraMetadata metadata = mNativeCamera.getMetadata(cameraInfo);
            float focalLength = 0.0f;
            if(metadata.focalLength != null && metadata.focalLength.length > 0) {
                focalLength = metadata.focalLength[0];
            }

            if(!seenFocalLength.contains(String.valueOf(focalLength)))
                cameraMetadataMap.put(cameraInfo.cameraId, focalLength);

            if(focalLength > 0)
                seenFocalLength.add(String.valueOf(focalLength));
        }

        List<String> cameraList = cameraMetadataMap
                .entrySet()
                .stream()
                .sorted(Map.Entry.comparingByValue())
                .map(Map.Entry::getKey)
                .collect(Collectors.toList());

        int dp = 10;

        // Add camera to view
        for(int i = 0; i < cameraList.size(); i++) {
            View cameraSelection =
                    getLayoutInflater().inflate(R.layout.camera_selector, cameraSelectionFrame, false);

            float px = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, getResources().getDisplayMetrics());
            ViewGroup.LayoutParams layoutParams = cameraSelection.findViewById(R.id.cameraZoomImage).getLayoutParams();

            layoutParams.width = Math.round(px);

            cameraSelection.findViewById(R.id.cameraZoomImage).setLayoutParams(layoutParams);

            if(cameraList.get(i).equals(mCameraInfos.get(0).cameraId)) {
                ImageViewCompat.setImageTintList(
                        cameraSelection.findViewById(R.id.cameraZoomImage),
                        ColorStateList.valueOf(ContextCompat.getColor(this, R.color.colorAccent)));
            }

            cameraSelection.setTag(cameraList.get(i));
            cameraSelection.setOnClickListener(this::onCameraSelectionChanged);

            if (i == 0) {
                ((TextView) cameraSelection.findViewById(R.id.cameraZoomIndicator)).setText("-");
            }
            else if (i == cameraList.size() - 1) {
                ((TextView) cameraSelection.findViewById(R.id.cameraZoomIndicator)).setText("+");
            }
            else {
                cameraSelection.findViewById(R.id.cameraZoomIndicator).setVisibility(View.GONE);
            }

            cameraSelectionFrame.addView(cameraSelection);

            dp += 5;
        }
    }

    private void initCamera() {
        if (mNativeCamera == null) {
            createCamera();

            setupCameraSwitchButtons();
        }

        // Exposure compensation frame
        findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

        // Set up camera manual controls
        mCameraMetadata = mNativeCamera.getMetadata(mSelectedCamera);

        // Keep range of valid ISO/shutter speeds
        mIsoValues = CameraManualControl.GetIsoValuesInRange(mCameraMetadata.isoMin, mCameraMetadata.isoMax);

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setMax(mIsoValues.size() - 1);

        mExposureValues = CameraManualControl.GetExposureValuesInRange(
                mCameraMetadata.exposureTimeMin,
                Math.min(MAX_EXPOSURE_TIME.getExposureTime(), mCameraMetadata.exposureTimeMax));

        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setMax(mExposureValues.size() - 1);

        int numEvSteps = mSelectedCamera.exposureCompRangeMax - mSelectedCamera.exposureCompRangeMin;
        mBinding.exposureSeekBar.setMax(numEvSteps);
        mBinding.exposureSeekBar.setProgress(numEvSteps / 2);

        mBinding.shadowsSeekBar.setProgress(50);

        // Create texture view for camera preview
        mTextureView = new TextureView(this);
        mBinding.cameraFrame.addView(
                mTextureView,
                0,
                new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mTextureView.setSurfaceTextureListener(this);
        mTextureView.setOnTouchListener(this);

        if (mTextureView.isAvailable()) {
            onSurfaceTextureAvailable(
                    mTextureView.getSurfaceTexture(),
                    mTextureView.getWidth(),
                    mTextureView.getHeight());
        }
    }

    /**
     * configureTransform()
     * Courtesy to https://github.com/google/cameraview/blob/master/library/src/main/api14/com/google/android/cameraview/TextureViewPreview.java#L108
     */
    private void configureTransform(int textureWidth, int textureHeight, Size previewOutputSize) {
        int displayOrientation = getWindowManager().getDefaultDisplay().getRotation() * 90;

        int width = textureWidth;
        int height = textureWidth * previewOutputSize.getWidth() / previewOutputSize.getHeight();

        if (Surface.ROTATION_90 == displayOrientation || Surface.ROTATION_270 == displayOrientation) {
            height = (textureWidth * previewOutputSize.getHeight()) / previewOutputSize.getWidth();
        }

        Matrix cameraMatrix = new Matrix();

        if (displayOrientation % 180 == 90) {
            // Rotate the camera preview when the screen is landscape.
            cameraMatrix.setPolyToPoly(
                    new float[]{
                            0.f, 0.f, // top left
                            width, 0.f, // top right
                            0.f, height, // bottom left
                            width, height, // bottom right
                    }, 0,
                    displayOrientation == 90 ?
                            // Clockwise
                            new float[]{
                                    0.f, height, // top left
                                    0.f, 0.f,    // top right
                                    width, height, // bottom left
                                    width, 0.f, // bottom right
                            } : // mDisplayOrientation == 270
                            // Counter-clockwise
                            new float[]{
                                    width, 0.f, // top left
                                    width, height, // top right
                                    0.f, 0.f, // bottom left
                                    0.f, height, // bottom right
                            }, 0,
                    4);
        }
        else if (displayOrientation == 180) {
            cameraMatrix.postRotate(180, width / 2.0f, height / 2.0f);
        }

        if(mSelectedCamera.isFrontFacing)
            cameraMatrix.preScale(1, -1, width / 2.0f, height / 2.0f);

        mTextureView.setTransform(cameraMatrix);
    }

    @Override
    public void onSurfaceTextureAvailable(@NonNull SurfaceTexture surfaceTexture, int width, int height) {
        Log.d(TAG, "onSurfaceTextureAvailable() w: " + width + " h: " + height);

        if(mNativeCamera == null || mSelectedCamera == null) {
            Log.e(TAG, "Native camera not available");
            return;
        }

        if(mSurface != null) {
            Log.w(TAG, "Surface still exists, releasing");
            mSurface.release();
            mSurface = null;
        }

        startCamera(surfaceTexture, width, height);
    }

    private void startCamera(SurfaceTexture surfaceTexture, int width, int height) {
        int displayWidth;
        int displayHeight;

        if(mSettings.useDualExposure) {
            // Use small preview window since we're not using the camera preview.
            displayWidth = 640;
            displayHeight = 480;
        }
        else {
            // Get display size
            Display display = getWindowManager().getDefaultDisplay();

            displayWidth = display.getMode().getPhysicalWidth();
            displayHeight = display.getMode().getPhysicalHeight();
        }

        // Get capture size so we can figure out the correct aspect ratio
        Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

        // If we couldn't find any RAW outputs, this camera doesn't actually support RAW10
        if(captureOutputSize == null) {
            displayUnsupportedCameraError();
            return;
        }

        Size previewOutputSize =
                mNativeCamera.getPreviewConfigurationOutput(mSelectedCamera, captureOutputSize, new Size(displayWidth, displayHeight));
        surfaceTexture.setDefaultBufferSize(previewOutputSize.getWidth(), previewOutputSize.getHeight());

        configureTransform(width, height, previewOutputSize);

        mSurface = new Surface(surfaceTexture);
        mNativeCamera.startCapture(mSelectedCamera, mSurface, mSettings.useDualExposure, mSettings.rawMode == SettingsViewModel.RawMode.RAW16);

        // Update orientation in case we've switched front/back cameras
        NativeCameraBuffer.ScreenOrientation orientation = mSensorEventManager.getOrientation();
        if(orientation != null)
            onOrientationChanged(orientation);

        if(mSettings.useDualExposure) {
            mBinding.rawCameraPreview.setVisibility(View.VISIBLE);
            mBinding.shadowsLayout.setVisibility(View.VISIBLE);

            mTextureView.setAlpha(0);

            mNativeCamera.enableRawPreview(this, mSettings.cameraPreviewQuality, false);
        }
        else {
            mBinding.rawCameraPreview.setVisibility(View.GONE);
            mBinding.shadowsLayout.setVisibility(View.GONE);

            mTextureView.setAlpha(1);
        }

        mBinding.previewFrame.previewControls.setVisibility(View.VISIBLE);

        mBinding.previewFrame.previewAdjustmentsBtn.setOnClickListener(v -> {
            mBinding.previewFrame.previewControlBtns.setVisibility(View.GONE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.VISIBLE);
        });

        mBinding.previewFrame.previewAdjustments.findViewById(R.id.closePreviewAdjustmentsBtn).setOnClickListener(v -> {
            mBinding.previewFrame.previewControlBtns.setVisibility(View.VISIBLE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.GONE);
        });

        mBinding.previewFrame.rawEnableBtn.setOnClickListener(v -> setSaveRaw(!mPostProcessSettings.dng));
        mBinding.previewFrame.hdrEnableBtn.setOnClickListener(v -> setHdr(!mSettings.hdr));

        // Kill previous timer
        if(mFaceDetectionTimer != null) {
            mFaceDetectionTimer.cancel();
            mFaceDetectionTimer = null;
        }

//        mFaceDetectionTimer = new Timer("FaceDetection");
//        mFaceDetectionTimer.scheduleAtFixedRate(new FaceDetectionTask(), 1000, 1000);
    }

    @Override
    public void onSurfaceTextureSizeChanged(@NonNull SurfaceTexture surface, int width, int height) {
        Log.d(TAG, "onSurfaceTextureSizeChanged() w: " + width + " h: " + height);
    }

    @Override
    public boolean onSurfaceTextureDestroyed(@NonNull SurfaceTexture surface) {
        Log.d(TAG, "onSurfaceTextureDestroyed()");

        // Release camera
        if(mNativeCamera != null) {
            mNativeCamera.disableRawPreview();
            mNativeCamera.stopCapture();
        }

        if(mSurface != null) {
            mSurface.release();
            mSurface = null;
        }

        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(@NonNull SurfaceTexture surface) {
    }

    private void autoSwitchCaptureMode() {
        if(!mSettings.autoNightMode
            || mCaptureMode == CaptureMode.BURST
            || mCaptureMode == CaptureMode.RAW_VIDEO
            || mManualControlsSet
            || mUserCaptureModeOverride)
        {
            return;
        }

        // Switch to night mode if we high ISO/shutter speed
        if(mIso > 1000 || mExposureTime > CameraManualControl.SHUTTER_SPEED.EXPOSURE_1_40.getExposureTime())
            setCaptureMode(CaptureMode.NIGHT);
        else
            setCaptureMode(CaptureMode.ZSL);
    }

    @Override
    public void onCameraDisconnected() {
        Log.i(TAG, "Camera has disconnected");
    }

    private void displayUnsupportedCameraError() {
        AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                .setCancelable(false)
                .setTitle(R.string.error)
                .setMessage(R.string.camera_error)
                .setPositiveButton(R.string.ok, (dialog, which) -> finish());

        dialogBuilder.show();
    }

    @Override
    public void onCameraError(int error) {
        Log.e(TAG, "Camera has failed");

        runOnUiThread(this::displayUnsupportedCameraError);
    }

    @Override
    public void onCameraSessionStateChanged(NativeCameraSessionBridge.CameraState cameraState) {
        Log.i(TAG, "Camera state changed " + cameraState.name());

        if(cameraState == NativeCameraSessionBridge.CameraState.ACTIVE) {
            runOnUiThread(() ->
            {
                mBinding.switchCameraBtn.setEnabled(true);
                updatePreviewSettings();
            });
        }
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
//        Log.i(TAG, "ISO: " + iso + " Exposure Time: " + exposureTime/(1000.0*1000.0));

        final CameraManualControl.ISO cameraIso = CameraManualControl.GetClosestIso(mIsoValues, iso);
        final CameraManualControl.SHUTTER_SPEED cameraShutterSpeed = CameraManualControl.GetClosestShutterSpeed(exposureTime);

        runOnUiThread(() -> {
            if(!mManualControlsSet) {
                ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setProgress(cameraIso.ordinal());
                ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setProgress(cameraShutterSpeed.ordinal());

                ((TextView) findViewById(R.id.manualControlIsoText)).setText(cameraIso.toString());
                ((TextView) findViewById(R.id.manualControlShutterSpeedText)).setText(cameraShutterSpeed.toString());
            }

            ((TextView) findViewById(R.id.infoIsoText)).setText(cameraIso.toString());
            ((TextView) findViewById(R.id.infoShutterSpeedText)).setText(cameraShutterSpeed.toString());

            mIso = iso;
            mExposureTime = exposureTime;

            autoSwitchCaptureMode();
        });
    }

    @Override
    public void onCameraAutoFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state) {
        runOnUiThread(() -> {
            onFocusStateChanged(state);
        });
    }

    private void startImageProcessor() {
        // Start service to process the image
        Intent intent = new Intent(this, ProcessorService.class);

        intent.putExtra(ProcessorService.METADATA_PATH_KEY, CameraProfile.getRootOutputPath(this).getPath());
        intent.putExtra(ProcessorService.RECEIVER_KEY, mProgressReceiver);

        Objects.requireNonNull(startService(intent));
    }

    @Override
    public void onCameraAutoExposureStateChanged(NativeCameraSessionBridge.CameraExposureState state) {
        Log.i(TAG, "Exposure state: " + state.name());
        runOnUiThread(() -> setAutoExposureState(state));
    }

    @Override
    public void onCameraHdrImageCaptureProgress(int progress) {
        runOnUiThread( () -> {
            if(progress < 100) {
                mBinding.captureProgressBar.setProgress(progress);
            }
            else {
                mBinding.captureProgressBar.setIndeterminateMode(true);
            }
        });
    }

    @Override
    public void onCameraHdrImageCaptureFailed() {
        Log.i(TAG, "HDR capture failed");

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

            // Tell user we didn't capture image
            AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                    .setCancelable(false)
                    .setTitle(R.string.error)
                    .setMessage(R.string.capture_failed)
                    .setPositiveButton(R.string.ok, (dialog, which) -> {});

            dialogBuilder.show();
        });
    }

    @Override
    public void onCameraHdrImageCaptureCompleted() {
        Log.i(TAG, "HDR capture completed");

        mImageCaptureInProgress.set(false);

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

            mBinding.cameraFrame
                    .animate()
                    .alpha(1.0f)
                    .setDuration(125)
                    .start();

            startImageProcessor();
        });
    }

    @Override
    public void onCaptured(long handle) {
        Log.i(TAG, "ZSL capture completed");

        mImageCaptureInProgress.set(false);

        mBinding.captureBtn.setEnabled(true);
        mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

        startImageProcessor();
    }

    @Override
    public void onRawPreviewCreated(Bitmap bitmap) {
        runOnUiThread(() -> ((BitmapDrawView) findViewById(R.id.rawCameraPreview)).setBitmap(bitmap));
    }

    @Override
    public void onRawPreviewUpdated() {
        runOnUiThread(() -> findViewById(R.id.rawCameraPreview).invalidate());
    }

    private void updateManualControlView(NativeCameraBuffer.ScreenOrientation orientation) {
        mBinding.manualControlsFrame.setAlpha(0.0f);
        mBinding.manualControlsFrame.post(() ->  {
            final int rotation;
            final int translationX;
            final int translationY;

            // Update position of manual controls
            if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {
                rotation = 180;
                translationX = 0;
                translationY = 0;
            }
            else if(orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
                rotation = 90;
                translationX = -mBinding.cameraFrame.getWidth() / 2 + mBinding.manualControlsFrame.getHeight() / 2;
                translationY = mBinding.cameraFrame.getHeight() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
            }
            else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE) {
                rotation = -90;
                translationX = mBinding.cameraFrame.getWidth() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
                translationY = mBinding.cameraFrame.getHeight() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
            }
            else {
                // Portrait
                rotation = 0;
                translationX = 0;
                translationY = mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight();
            }

            mBinding.manualControlsFrame.setRotation(rotation);
            mBinding.manualControlsFrame.setTranslationX(translationX);
            mBinding.manualControlsFrame.setTranslationY(translationY);

            mBinding.manualControlsFrame
                    .animate()
                    .setDuration(500)
                    .alpha(1.0f)
                    .start();
        });
    }

    @Override
    public void onOrientationChanged(NativeCameraBuffer.ScreenOrientation orientation) {
        Log.i(TAG, "Orientation is " + orientation);

        if(mNativeCamera != null) {
            if(mSelectedCamera.isFrontFacing) {
                if(orientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT)
                    mNativeCamera.updateOrientation(NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT);
                else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT)
                    mNativeCamera.updateOrientation(NativeCameraBuffer.ScreenOrientation.PORTRAIT);
            }
            else
                mNativeCamera.updateOrientation(orientation);
        }

        updateManualControlView(orientation);

        final int duration = 500;

        mBinding.switchCameraBtn
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

        mBinding.previewFrame.settingsBtn
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

        mBinding.capturePreview
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

    }

    private void onShadowsSeekBarChanged(int progress) {
        mShadowOffset = 6.0f * ((progress - 50.0f) / 100.0f);

        updatePreviewSettings();
    }

    private void updatePreviewSettings() {
        if(mPostProcessSettings != null && mNativeCamera != null) {
            mNativeCamera.setRawPreviewSettings(
                    mShadowOffset,
                    mPostProcessSettings.contrast,
                    mPostProcessSettings.saturation,
                    0.05f,
                    1.0f,
                    mTemperatureOffset,
                    mTintOffset);
        }
    }

    private void onFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state) {
        Log.i(TAG, "Focus state: " + state.name());

        if( state == NativeCameraSessionBridge.CameraFocusState.PASSIVE_SCAN ||
            state == NativeCameraSessionBridge.CameraFocusState.ACTIVE_SCAN)
        {
            if(mAutoFocusPoint == null) {
                FrameLayout.LayoutParams layoutParams =
                        (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

                layoutParams.setMargins(
                        (mTextureView.getWidth() - mBinding.focusLockPointFrame.getWidth()) / 2,
                        (mTextureView.getHeight() - mBinding.focusLockPointFrame.getHeight()) / 2,
                        0,
                        0);

                mBinding.focusLockPointFrame.setAlpha(1.0f);
                mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
            }

            if(mBinding.focusLockPointFrame.getVisibility() == View.INVISIBLE) {
                mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
                mBinding.focusLockPointFrame.setAlpha(0.0f);

                mBinding.focusLockPointFrame
                        .animate()
                        .alpha(1)
                        .setDuration(250)
                        .start();
            }
        }
        else if(state == NativeCameraSessionBridge.CameraFocusState.PASSIVE_FOCUSED) {
            if(mBinding.focusLockPointFrame.getVisibility() == View.VISIBLE) {
                mBinding.focusLockPointFrame
                        .animate()
                        .alpha(0)
                        .setStartDelay(500)
                        .setDuration(250)
                        .withEndAction(() -> mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE))
                        .start();
            }
        }
    }

    private void setAutoExposureState(NativeCameraSessionBridge.CameraExposureState state) {
        boolean timePassed = System.currentTimeMillis() - mFocusRequestedTimestampMs > 3000;

        if(state == NativeCameraSessionBridge.CameraExposureState.SEARCHING && timePassed) {
            setFocusState(FocusState.AUTO, null);
        }
    }

    private void setFocusState(FocusState state, PointF focusPt) {
        if(mFocusState == FocusState.AUTO && state == FocusState.AUTO)
            return;

        // Don't update if the focus points are very close to each other
        if(focusPt != null && mAutoFocusPoint != null) {
            double d = Math.hypot(focusPt.x - mAutoFocusPoint.x, focusPt.y - mAutoFocusPoint.y);
            if(d < 0.05) {
                return;
            }
        }

        mFocusState = state;

        if(state == FocusState.FIXED) {
            mAutoExposurePoint = focusPt;
            mAutoFocusPoint = focusPt;

            mNativeCamera.setFocusPoint(mAutoFocusPoint, mAutoExposurePoint);
            mFocusRequestedTimestampMs = System.currentTimeMillis();
        }
        else if(state == FocusState.AUTO) {
            mAutoFocusPoint = null;
            mAutoExposurePoint = null;

            mNativeCamera.setAutoFocus();
        }
    }

    private void onSetFocusPt(float touchX, float touchY) {
        if(mNativeCamera == null)
            return;

        // If settings AF regions is not supported, do nothing
        if(mCameraMetadata.maxAfRegions <= 0)
            return;

        float x = touchX / mTextureView.getWidth();
        float y = touchY / mTextureView.getHeight();

        // Ignore edges
        if (x < 0.05 || x > 0.95 ||
            y < 0.05 || y > 0.95)
        {
            return;
        }

        Matrix m = new Matrix();
        float[] pts = new float[] { x, y };

        m.setRotate(-mCameraMetadata.sensorOrientation, 0.5f, 0.5f);
        m.mapPoints(pts);

        PointF pt = new PointF(pts[0], pts[1]);

        FrameLayout.LayoutParams layoutParams =
                (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

        layoutParams.setMargins(
                Math.round(touchX) - mBinding.focusLockPointFrame.getWidth() / 2,
                Math.round(touchY) - mBinding.focusLockPointFrame.getHeight() / 2,
                0,
                0);

        mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
        mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
        mBinding.focusLockPointFrame.setAlpha(1.0f);
        mBinding.focusLockPointFrame.animate().cancel();

        setFocusState(FocusState.FIXED, pt);
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        if(v == mTextureView) {
            if(event.getAction() == MotionEvent.ACTION_UP) {
                onSetFocusPt(event.getX(), event.getY());
            }
        }

        return true;
    }

    @Override
    public void onProcessingStarted() {

    }

    @Override
    public void onProcessingProgress(int progress) {

    }

    @Override
    public void onPreviewSaved(String outputPath) {
        Glide.with(this)
                .load(outputPath)
                .dontAnimate()
                .into(mBinding.capturePreview);

        mCameraCapturePreviewAdapter.add(new File(outputPath));

        mBinding.capturePreview.setScaleX(0.5f);
        mBinding.capturePreview.setScaleY(0.5f);
        mBinding.capturePreview
                .animate()
                .scaleX(1)
                .scaleY(1)
                .setInterpolator(new BounceInterpolator())
                .setDuration(500)
                .start();
    }

    @Override
    public void onProcessingCompleted(File internalPath, Uri contentUri) {
        mCameraCapturePreviewAdapter.complete(internalPath, contentUri);

        if(mCameraCapturePreviewAdapter.isProcessing(mBinding.previewPager.getCurrentItem())) {
            mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
        }
        else {
            mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            onCaptureTouched(MotionEvent.obtain(0, 0, MotionEvent.ACTION_DOWN, 0, 0, 0));
            onCaptureClicked();
            return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_VOLUME_UP) {
            capture(CaptureMode.BURST);
            return true;
        }

        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void onTransitionStarted(MotionLayout motionLayout, int startId, int endId)  {
        mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
    }

    @Override
    public void onTransitionChange(MotionLayout motionLayout, int startId, int endId, float progress) {

    }

    @Override
    public void onTransitionCompleted(MotionLayout motionLayout, int currentId)  {
        if(mNativeCamera == null)
            return;

        if(currentId == mBinding.main.getEndState()) {
            // Reset exposure/shadows
            mBinding.exposureSeekBar.setProgress(mBinding.exposureSeekBar.getMax() / 2);
            mBinding.shadowsSeekBar.setProgress(mBinding.shadowsSeekBar.getMax() / 2);

            mBinding.previewPager.setCurrentItem(0);

            mNativeCamera.pauseCapture();

            if(mCameraCapturePreviewAdapter.isProcessing(mBinding.previewPager.getCurrentItem())) {
                mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
            }
        }
        else if(currentId == mBinding.main.getStartState()) {
            mNativeCamera.resumeCapture();
        }
    }

    @Override
    public void onTransitionTrigger(MotionLayout motionLayout, int triggerId, boolean positive, float progress) {
    }

    void onCapturedPreviewPageChanged(int position) {
        if(mCameraCapturePreviewAdapter.isProcessing(position)) {
            mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
        }
        else {
            mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
        }
    }

    void onReceivedLocation(Location lastLocation) {
        mLastLocation = lastLocation;

        if(mPostProcessSettings != null) {
            mPostProcessSettings.gpsLatitude = mLastLocation.getLatitude();
            mPostProcessSettings.gpsLongitude = mLastLocation.getLongitude();
            mPostProcessSettings.gpsAltitude = mLastLocation.getAltitude();
            mPostProcessSettings.gpsTime = String.valueOf(mLastLocation.getTime());
        }
    }
}
