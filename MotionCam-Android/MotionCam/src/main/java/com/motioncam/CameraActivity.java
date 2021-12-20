package com.motioncam;

import android.Manifest;
import android.app.ProgressDialog;
import android.content.ContentResolver;
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
import android.graphics.SurfaceTexture;
import android.graphics.drawable.Drawable;
import android.location.Location;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
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
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.constraintlayout.motion.widget.MotionLayout;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.widget.ImageViewCompat;
import androidx.documentfile.provider.DocumentFile;
import androidx.viewpager2.widget.ViewPager2;
import androidx.work.Data;
import androidx.work.ExistingWorkPolicy;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkInfo;
import androidx.work.WorkManager;

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
import com.motioncam.camera.VideoRecordingStats;
import com.motioncam.databinding.CameraActivityBinding;
import com.motioncam.model.CameraProfile;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.ui.BitmapDrawView;
import com.motioncam.ui.CameraCapturePreviewAdapter;
import com.motioncam.worker.ImageProcessWorker;
import com.motioncam.worker.State;
import com.motioncam.worker.VideoProcessWorker;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
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
        MotionLayout.TransitionListener, AsyncNativeCameraOps.CaptureImageListener {

    public static final String TAG = "MotionCam";

    private static final int PERMISSION_REQUEST_CODE = 1;
    private static final int SETTINGS_ACTIVITY_REQUEST_CODE = 0x10;
    private static final int CONVERT_VIDEO_ACTIVITY_REQUEST_CODE = 0x20;

    private static final int MANUAL_CONTROL_MODE_ISO = 0;
    private static final int MANUAL_CONTROL_MODE_SHUTTER_SPEED = 1;
    private static final int MANUAL_CONTROL_MODE_FOCUS = 2;

    public static final String WORKER_IMAGE_PROCESSOR = "ImageProcessor";
    public static final String WORKER_VIDEO_PROCESSOR = "VideoProcessor";

    private static final CameraManualControl.SHUTTER_SPEED MAX_EXPOSURE_TIME =
            CameraManualControl.SHUTTER_SPEED.EXPOSURE_2__0;

    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.RECORD_AUDIO
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

    private static class Settings {
        boolean useDualExposure;
        boolean rawVideoToDng;
        boolean saveDng;
        boolean autoNightMode;
        boolean hdr;
        float contrast;
        float saturation;
        float temperatureOffset;
        float tintOffset;
        int jpegQuality;
        long memoryUseBytes;
        long rawVideoMemoryUseBytes;
        CaptureMode captureMode;
        SettingsViewModel.RawMode rawMode;
        int cameraPreviewQuality;
        int horizontalVideoCrop;
        int verticalVideoCrop;
        int frameRate;
        boolean videoBin;
        Uri rawVideoRecordingTempUri;

        void load(SharedPreferences prefs) {
            this.jpegQuality = prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY);
            this.contrast = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, CameraProfile.DEFAULT_CONTRAST / 100.0f);
            this.saturation = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, 1.00f);
            this.temperatureOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, 0);
            this.tintOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, 0);
            this.saveDng = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, false);
            this.autoNightMode = prefs.getBoolean(SettingsViewModel.PREFS_KEY_AUTO_NIGHT_MODE, true);
            this.hdr = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, true);
            this.horizontalVideoCrop = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_HORIZONTAL_VIDEO_CROP, 0);
            this.verticalVideoCrop = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_VERTICAL_VIDEO_CROP, 0);
            this.frameRate = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_FRAME_RATE, 30);
            this.videoBin = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_VIDEO_BIN, false);

            long nativeCameraMemoryUseMb = prefs.getInt(SettingsViewModel.PREFS_KEY_MEMORY_USE_MBYTES, SettingsViewModel.MINIMUM_MEMORY_USE_MB);
            nativeCameraMemoryUseMb = Math.min(nativeCameraMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);
            this.memoryUseBytes = nativeCameraMemoryUseMb * 1024 * 1024;

            long nativeRawVideoMemoryUseMb = prefs.getInt(SettingsViewModel.PREFS_KEY_RAW_VIDEO_MEMORY_USE_MBYTES, SettingsViewModel.MINIMUM_MEMORY_USE_MB);
            nativeRawVideoMemoryUseMb = Math.min(nativeRawVideoMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);
            this.rawVideoMemoryUseBytes = nativeRawVideoMemoryUseMb * 1024 * 1024;

            this.useDualExposure = prefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false);
            this.rawVideoToDng = prefs.getBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TO_DNG, true);

            this.captureMode = CaptureMode.valueOf(prefs.getString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, CaptureMode.ZSL.name()));

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

            String rawVideoRecordingTempUri = prefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, null);
            if(rawVideoRecordingTempUri != null && !rawVideoRecordingTempUri.isEmpty())
                this.rawVideoRecordingTempUri = Uri.parse(rawVideoRecordingTempUri);
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
                    .putInt(SettingsViewModel.PREFS_KEY_UI_HORIZONTAL_VIDEO_CROP, this.horizontalVideoCrop)
                    .putInt(SettingsViewModel.PREFS_KEY_UI_VERTICAL_VIDEO_CROP, this.verticalVideoCrop)
                    .putInt(SettingsViewModel.PREFS_KEY_UI_FRAME_RATE, this.frameRate)
                    .putBoolean(SettingsViewModel.PREFS_KEY_UI_VIDEO_BIN, this.videoBin)
                    .apply();
        }

        @Override
        public String toString() {
            return "Settings{" +
                    "useDualExposure=" + useDualExposure +
                    ", rawVideoToDng=" + rawVideoToDng +
                    ", saveDng=" + saveDng +
                    ", autoNightMode=" + autoNightMode +
                    ", hdr=" + hdr +
                    ", contrast=" + contrast +
                    ", saturation=" + saturation +
                    ", temperatureOffset=" + temperatureOffset +
                    ", tintOffset=" + tintOffset +
                    ", jpegQuality=" + jpegQuality +
                    ", memoryUseBytes=" + memoryUseBytes +
                    ", rawVideoMemoryUseBytes=" + rawVideoMemoryUseBytes +
                    ", captureMode=" + captureMode +
                    ", rawMode=" + rawMode +
                    ", cameraPreviewQuality=" + cameraPreviewQuality +
                    ", horizontalVideoCrop=" + horizontalVideoCrop +
                    ", verticalVideoCrop=" + verticalVideoCrop +
                    ", frameRate=" + frameRate +
                    ", videoBin=" + videoBin +
                    ", rawVideoRecordingTempUri=" + rawVideoRecordingTempUri +
                    '}';
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
    private List<NativeCameraInfo> mCameraInfos;
    private NativeCameraInfo mSelectedCamera;
    private NativeCameraMetadata mCameraMetadata;
    private SensorEventManager mSensorEventManager;
    private FusedLocationProviderClient mFusedLocationClient;
    private Location mLastLocation;
    private long mRecordStartTime;

    private CameraCapturePreviewAdapter mCameraCapturePreviewAdapter;

    private PostProcessSettings mPostProcessSettings = new PostProcessSettings();
    private PostProcessSettings mEstimatedSettings = new PostProcessSettings();

    private float mTemperatureOffset;
    private float mTintOffset;

    private boolean mManualControlsSet;
    private CaptureMode mCaptureMode;
    private PreviewControlMode mPreviewControlMode = PreviewControlMode.CONTRAST;
    private boolean mUserCaptureModeOverride;

    private FocusState mFocusState = FocusState.AUTO;
    private NativeCameraSessionBridge.CameraExposureState mExposureState;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private boolean mAwbLock;
    private boolean mAeLock;
    private boolean mAfLock;
    private boolean mOIS;
    private int mIso;
    private long mExposureTime;
    private float mFocusDistance;
    private int mUserIso;
    private long mUserExposureTime;
    private float mShadowOffset;
    private long mFocusRequestedTimestampMs;
    private Timer mRecordingTimer;

    private AtomicBoolean mImageCaptureInProgress = new AtomicBoolean(false);

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
            else if(seekBar == findViewById(R.id.manualControlSeekBar)) {
                onManualControlSettingsChanged(progress, fromUser);
            }
            else if(seekBar == mBinding.previewFrame.horizontalCropSeekBar) {
                onHorizontalCropChanged(progress, fromUser);
            }
            else if(seekBar == mBinding.previewFrame.verticalCropSeekBar) {
                onVerticalCropChanged(progress, fromUser);
            }
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
            if(seekBar == mBinding.previewFrame.horizontalCropSeekBar
                || seekBar == mBinding.previewFrame.verticalCropSeekBar) {
                toggleVideoCrop();
            }
            else if(seekBar == findViewById(R.id.manualControlSeekBar)) {
                hideManualControls();
            }
        }
    };

    @Override
    public void onBackPressed() {
        if (mBinding.main.getCurrentState() == mBinding.main.getEndState()) {
            mBinding.main.transitionToStart();
        } else {
            super.onBackPressed();
        }
    }

    private void prunePreviews() {
        // Clear out previous preview files
        AsyncTask.execute(() -> {
            File previewDirectory = new File(getFilesDir(), ImageProcessWorker.PREVIEW_PATH);
            long now = new Date().getTime();

            File[] previewFiles = previewDirectory.listFiles();
            if(previewFiles != null) {
                for (File f : previewFiles) {
                    long diff = now - f.lastModified();
                    if (diff > 2 * 24 * 60 * 60 * 1000) // 2 days old
                        f.delete();
                }
            }
        });
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        onWindowFocusChanged(true);

        prunePreviews();

        mBinding = CameraActivityBinding.inflate(getLayoutInflater());
        setContentView(mBinding.getRoot());

        mSettings = new Settings();

        mBinding.focusLockPointFrame.setOnClickListener(v -> onFixedFocusCancelled());
        mBinding.previewFrame.settingsBtn.setOnClickListener(v -> onSettingsClicked());
        mBinding.previewFrame.processVideoBtn.setOnClickListener(v -> OnProcessVideoClicked());

        mCameraCapturePreviewAdapter = new CameraCapturePreviewAdapter(getApplicationContext());
        mBinding.previewPager.setAdapter(mCameraCapturePreviewAdapter);

        mBinding.shareBtn.setOnClickListener(this::share);
        mBinding.openBtn.setOnClickListener(this::open);

        mBinding.onBackFromPreviewBtn.setOnClickListener(v -> mBinding.main.transitionToStart());
        mBinding.main.setTransitionListener(this);

        mBinding.shadowsSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.exposureSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.previewFrame.previewSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);

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

        WorkManager.getInstance(this)
                .getWorkInfosForUniqueWorkLiveData(WORKER_IMAGE_PROCESSOR)
                .observe(this, this::onProgressChanged);

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

    private void OnProcessVideoClicked() {
        Intent intent = new Intent(this, ConvertVideoActivity.class);
        startActivityForResult(intent, CONVERT_VIDEO_ACTIVITY_REQUEST_CODE);
    }

    private void onFixedFocusCancelled() {
        setFocusState(FocusState.AUTO, null);
    }

    private void setPostProcessingDefaults() {
        // Set initial preview values
        mPostProcessSettings.shadows = -1.0f;
        mPostProcessSettings.contrast = mSettings.contrast;
        mPostProcessSettings.saturation = mSettings.saturation;
        mPostProcessSettings.brightness = 1.125f;
        mPostProcessSettings.greens = 0.0f;
        mPostProcessSettings.blues = 0.0f;
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

        mBinding.rawCameraPreview.setBitmap(null);
        mBinding.main.transitionToStart();

        // Load UI settings
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        mSettings.load(sharedPrefs);

        Log.d(TAG, mSettings.toString());

        setPostProcessingDefaults();
        updatePreviewTabUi(true);

        setCaptureMode(CaptureMode.ZSL);
        setSaveRaw(mSettings.saveDng);
        setHdr(mSettings.hdr);

        // Defaults
        setAwbLock(false);
        setAeLock(false);
        setAfLock(false, false);
        //setOIS(true);

        mBinding.focusLockPointFrame.setVisibility(View.GONE);
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
        mBinding.previewPager.unregisterOnPageChangeCallback(mCapturedPreviewPagerListener);

        if(mNativeCamera != null) {
            if(mImageCaptureInProgress.getAndSet(false) && mCaptureMode == CaptureMode.RAW_VIDEO) {
                finaliseRawVideo(false);
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

    private int getInternalRecordingFd(String filename) {
        File outputDirectory = new File(getFilesDir(), VideoProcessWorker.VIDEOS_PATH);
        File outputFile = new File(outputDirectory, filename);

        try {
            if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + outputDirectory.toString());
                return -1;
            }
        }
        catch(Exception e) {
            e.printStackTrace();
            Log.e(TAG, "Error creating path: " + outputDirectory.toString());
            return - 1;
        }

        try(ParcelFileDescriptor pfd = ParcelFileDescriptor.open(
                outputFile, ParcelFileDescriptor.MODE_CREATE|ParcelFileDescriptor.MODE_READ_WRITE))
        {
            return pfd.detachFd();
        }
        catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
    }

    private int getRecordingFd(String filename, String mimeType) {
        if(mSettings.rawVideoRecordingTempUri == null)
            return -1;

        try {
            DocumentFile root = DocumentFile.fromTreeUri(this, mSettings.rawVideoRecordingTempUri);
            if (root.exists() && root.isDirectory() && root.canWrite()) {
                DocumentFile output = root.createFile(mimeType, filename);
                ContentResolver resolver = getApplicationContext().getContentResolver();

                try {
                    ParcelFileDescriptor pfd = resolver.openFileDescriptor(output.getUri(), "w", null);

                    if (pfd != null) {
                        return pfd.detachFd();
                    }
                } catch (FileNotFoundException e) {
                    e.printStackTrace();
                    return -1;
                }
            }
        }
        catch(Exception e) {
            e.printStackTrace();
        }

        String error = getString(R.string.invalid_raw_video_folder);

        Toast.makeText(CameraActivity.this, error, Toast.LENGTH_LONG).show();

        return -1;
    }

    private void startRawVideoRecording() {
        Log.i(TAG, "Starting RAW video recording (max memory usage: " + mSettings.rawVideoMemoryUseBytes + ")");

        // Try to get a writable fd
        String videoName = CameraProfile.generateFilename("VIDEO", ".container");
        String audioName = videoName.replace(".container", ".wav");

        int videoFd = getRecordingFd(videoName, "application/octet-stream");
        int audioFd = getRecordingFd(audioName, "audio/wav");

        if(videoFd < 0) {
            videoFd = getInternalRecordingFd(videoName);
        }

        if(audioFd < 0) {
            audioFd = getInternalRecordingFd(audioName);
        }

        if(videoFd < 0) {
            String error = getString(R.string.recording_failed);
            Toast.makeText(CameraActivity.this, error, Toast.LENGTH_LONG).show();
            return;
        }

        mNativeCamera.streamToFile(videoFd, -1, audioFd);
        mImageCaptureInProgress.set(true);

        mBinding.switchCameraBtn.setEnabled(false);
        mBinding.recordingTimer.setVisibility(View.VISIBLE);

        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.GONE);
        mBinding.previewFrame.videoRecordingStats.setVisibility(View.VISIBLE);

        mRecordStartTime = System.currentTimeMillis();

        // Update recording time
        mRecordingTimer = new Timer();
        mRecordingTimer.scheduleAtFixedRate(new TimerTask() {
            @Override
            public void run() {
                long timeRecording = System.currentTimeMillis() - mRecordStartTime;

                float mins = (timeRecording / 1000.0f) / 60.0f;
                int seconds = (int) ((mins - ((int) mins)) * 60);

                String recordingText = String.format(Locale.US, "00:%02d:%02d", (int) mins, seconds);

                runOnUiThread(() -> {
                    VideoRecordingStats stats = mNativeCamera.getVideoRecordingStats();

                    mBinding.recordingTimerText.setText(recordingText);
                    mBinding.previewFrame.memoryUsageProgress.setProgress(Math.round(stats.memoryUse * 100));

                    // Keep track of space
                    StatFs statFs = new StatFs(getFilesDir().getPath());
                    int spaceLeft = Math.round(100 * (statFs.getAvailableBytes() / (float) statFs.getTotalBytes()));

                    mBinding.previewFrame.freeSpaceProgress.setProgress(spaceLeft);

                    // End recording if memory usage is too high
                    if(stats.memoryUse > 0.75f) {
                        mBinding.previewFrame.memoryUsageProgress.getProgressDrawable()
                                .setTint(getColor(R.color.cancelAction));
                    }
                    else {
                        mBinding.previewFrame.memoryUsageProgress.getProgressDrawable()
                                .setTint(getColor(R.color.acceptAction));
                    }

                    float sizeMb = stats.size / 1024.0f / 1024.0f;
                    float sizeGb = sizeMb / 1024.0f;

                    String size;

                    if(sizeMb > 1024) {
                        size = String.format(Locale.US, "%.2f GB", sizeGb);
                    }
                    else {
                        size = String.format(Locale.US, "%d MB", Math.round(sizeMb));
                    }

                    String outputFpsText = String.format(Locale.US, "%.2f\n%s", stats.fps, getString(R.string.output_fps));
                    String outputSizeText = String.format(Locale.US, "%s\n%s", size, getString(R.string.size));

                    mBinding.previewFrame.outputFps.setText(outputFpsText);
                    mBinding.previewFrame.outputSize.setText(outputSizeText);
                });
            }
        }, 0, 500);
    }

    private void finaliseRawVideo(boolean showProgress) {
        mImageCaptureInProgress.set(false);

        mBinding.switchCameraBtn.setEnabled(true);
        mBinding.recordingTimer.setVisibility(View.GONE);

        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.VISIBLE);
        mBinding.previewFrame.videoRecordingStats.setVisibility(View.GONE);

        if(mRecordingTimer != null) {
            mRecordingTimer.cancel();
            mRecordingTimer = null;
        }

        ProgressDialog dialog = new ProgressDialog(this, R.style.BasicDialog);

        if(showProgress) {
            dialog.setIndeterminate(true);
            dialog.setCancelable(false);
            dialog.setTitle(getString(R.string.please_wait));
            dialog.setMessage(getString(R.string.saving_video));

            dialog.show();
        }

        AsyncTask.execute(() -> {
            mNativeCamera.endStream();

            if(showProgress)
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

        boolean currentFrontFacing = mSelectedCamera != null && mSelectedCamera.isFrontFacing;

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

    private void updateVideoUi() {
        String cropText = getText(R.string.crop).toString();
        String fpsText = getText(R.string.fps).toString();
        String resText = getText(R.string.output).toString();
        String binText = getText(R.string.bin).toString();

        mBinding.previewFrame.videoCropToggle.setText(
                String.format(Locale.US, "%d%% / %d%%\n%s", mSettings.horizontalVideoCrop, mSettings.verticalVideoCrop, cropText));

        mBinding.previewFrame.videoFrameRateBtn.setText(
                String.format(Locale.US, "%d\n%s", mSettings.frameRate, fpsText));

        mBinding.previewFrame.videoBinBtn.setText(
                String.format(Locale.US, "%s\n%s", mSettings.videoBin ? "2x2" : "1x1", binText));

        if(mNativeCamera != null) {
            Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

            int bin = mSettings.videoBin ? 2 : 1;

            int width = captureOutputSize.getWidth() / bin;
            int height = captureOutputSize.getHeight() / bin;

            width = Math.round(width - (mSettings.horizontalVideoCrop / 100.0f) * width);
            height = Math.round(height - (mSettings.verticalVideoCrop / 100.0f) * height);

            width = width / 4 * 4;
            height = height / 2 * 2;

            mBinding.previewFrame.videoResolution.setText(String.format(Locale.US, "%dx%d\n%s", width, height, resText));
        }

        if(mCaptureMode == CaptureMode.RAW_VIDEO && (mSettings.horizontalVideoCrop > 0 || mSettings.verticalVideoCrop > 0))
            mBinding.gridLayout.setCropMode(true, mSettings.horizontalVideoCrop, mSettings.verticalVideoCrop);
        else
            mBinding.gridLayout.setCropMode(false, 0, 0);
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

    private void setupRawVideoCapture() {
        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.VISIBLE);
        mBinding.previewFrame.previewControlBtns.setVisibility(View.GONE);
        mBinding.previewFrame.previewAdjustments.setVisibility(View.GONE);

        if(mNativeCamera != null) {
            mNativeCamera.setFrameRate(mSettings.frameRate);
            mNativeCamera.setVideoCropPercentage(mSettings.horizontalVideoCrop, mSettings.verticalVideoCrop);
            mNativeCamera.setVideoBin(mSettings.videoBin);
            mNativeCamera.adjustMemory(mSettings.rawVideoMemoryUseBytes);

            // Disable RAW preview
            if(mSettings.useDualExposure) {
                mNativeCamera.disableRawPreview();

                mBinding.rawCameraPreview.setVisibility(View.GONE);
                mBinding.shadowsLayout.setVisibility(View.GONE);

                mTextureView.setAlpha(1);
            }
        }
    }

    private void restoreFromRawVideoCapture() {
        if(mNativeCamera == null) {
            return;
        }

        mNativeCamera.setFrameRate(30);
        mNativeCamera.adjustMemory(mSettings.memoryUseBytes);

        if(mSettings.useDualExposure) {
            mBinding.rawCameraPreview.setVisibility(View.VISIBLE);
            mBinding.shadowsLayout.setVisibility(View.VISIBLE);

            if(mTextureView != null)
                mTextureView.setAlpha(0);

            mNativeCamera.enableRawPreview(this, mSettings.cameraPreviewQuality, false);
        }
    }

    private void setCaptureMode(CaptureMode captureMode) {
        if(mCaptureMode == captureMode || mImageCaptureInProgress.get())
            return;

        // Can't use night mode with manual controls
        if(mManualControlsSet && captureMode == CaptureMode.NIGHT)
            return;

        mBinding.nightModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.zslModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.burstModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.rawVideoModeBtn.setTextColor(getColor(R.color.textColor));

        mBinding.previewFrame.previewControlBtns.setVisibility(View.VISIBLE);
        mBinding.previewFrame.previewAdjustments.setVisibility(View.GONE);
        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.GONE);

        switch(captureMode) {
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
                break;
        }

        if(captureMode == CaptureMode.RAW_VIDEO) {
            setupRawVideoCapture();
        }

        // If previous capture mode was raw video, reset frame rate
        if(mCaptureMode == CaptureMode.RAW_VIDEO) {
            restoreFromRawVideoCapture();
        }

        mCaptureMode = captureMode;

        updatePreviewSettings();
        updateVideoUi();
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

    private void toggleVideoCrop() {
        if( mBinding.previewFrame.horizontalCrop.getVisibility() == View.VISIBLE
            || mBinding.previewFrame.verticalCrop.getVisibility() == View.VISIBLE)
        {
            mBinding.previewFrame.horizontalCrop.setVisibility(View.GONE);
            mBinding.previewFrame.verticalCrop.setVisibility(View.GONE);
        }
        else {
            mBinding.previewFrame.horizontalCropSeekBar.setProgress(mSettings.horizontalVideoCrop);
            mBinding.previewFrame.horizontalCrop.setVisibility(View.VISIBLE);

            mBinding.previewFrame.verticalCropSeekBar.setProgress(mSettings.verticalVideoCrop);
            mBinding.previewFrame.verticalCrop.setVisibility(View.VISIBLE);
        }
    }

    private void hideManualControls() {
        ((ImageView) findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        ((ImageView) findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        ((ImageView) findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        findViewById(R.id.manualControl).setVisibility(View.GONE);
        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void onManualControlSettingsChanged(int progress, boolean fromUser) {
        if(mNativeCamera != null && fromUser) {
            int selectionMode = (int) findViewById(R.id.manualControl).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
                mUserIso = mIsoValues.get(progress).getIso();
                mNativeCamera.setManualExposureValues(mUserIso, mUserExposureTime);
            }
            else if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
                mUserExposureTime = mExposureValues.get(progress).getExposureTime();
                mNativeCamera.setManualExposureValues(mUserIso, mUserExposureTime);
            }
            else if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                double in = (100.0f - progress) / 100.0f;
                double p =
                    Math.exp(in * (Math.log(mCameraMetadata.minFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)) +
                            Math.log(mCameraMetadata.hyperFocalDistance));

                mFocusDistance = (float) p;
                mNativeCamera.setManualFocus(mFocusDistance);

                onFocusStateChanged(NativeCameraSessionBridge.CameraFocusState.FOCUS_LOCKED, mFocusDistance);
            }
        }
    }

    private void toggleFocus() {
        // Hide if shown
        if(findViewById(R.id.manualControl).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControl).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                hideManualControls();
                return;
            }
        }

        setAfLock(true, false);

        String minFocus = "-";
        String maxFocus = "-";

        if(mCameraMetadata.minFocusDistance > 0)
            minFocus = String.format(Locale.US, "%.2f", 1.0f / mCameraMetadata.minFocusDistance);

        if(mCameraMetadata.hyperFocalDistance > 0)
            maxFocus = String.format(Locale.US, "%.2f", 1.0f / mCameraMetadata.hyperFocalDistance);

        ((TextView) findViewById(R.id.manualControlMinText)).setText(minFocus);
        ((TextView) findViewById(R.id.manualControlMaxText)).setText(maxFocus);

        double progress = 100.0 * (
            (Math.log(mFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)) /
            (Math.log(mCameraMetadata.minFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)));

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setMax(100);
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setProgress((int)Math.round(100 - progress));
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setTickMark(null);

        findViewById(R.id.manualControl).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_FOCUS);
        findViewById(R.id.manualControl).setVisibility(View.VISIBLE);

        ((ImageView) findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void toggleIso() {
        // Hide if shown
        if(findViewById(R.id.manualControl).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControl).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
                hideManualControls();
                return;
            }
        }

        if(!mManualControlsSet) {
            mUserIso = mIso;
            mUserExposureTime = mExposureTime;
            mManualControlsSet = true;

            setAeLock(true);
        }

        ((TextView) findViewById(R.id.manualControlMinText)).setText(mIsoValues.get(0).toString());
        ((TextView) findViewById(R.id.manualControlMaxText)).setText(mIsoValues.get(mIsoValues.size() - 1).toString());

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setMax(mIsoValues.size() - 1);
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setTickMark(
                AppCompatResources.getDrawable(this, R.drawable.seekbar_tick_mark));

        int isoIdx = CameraManualControl.GetClosestIso(mIsoValues, mUserIso).ordinal();

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setProgress(isoIdx);

        findViewById(R.id.manualControl).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_ISO);
        findViewById(R.id.manualControl).setVisibility(View.VISIBLE);

        ((ImageView) findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void toggleShutterSpeed() {
        // Hide if shown
        if(findViewById(R.id.manualControl).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControl).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
                hideManualControls();
                return;
            }
        }

        if(!mManualControlsSet) {
            mUserIso = mIso;
            mUserExposureTime = mExposureTime;
            mManualControlsSet = true;

            setAeLock(true);
        }

        ((TextView) findViewById(R.id.manualControlMinText)).setText(mExposureValues.get(0).toString());
        ((TextView) findViewById(R.id.manualControlMaxText)).setText(mExposureValues.get(mExposureValues.size() - 1).toString());

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setMax(mExposureValues.size() - 1);
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setTickMark(AppCompatResources.getDrawable(this, R.drawable.seekbar_tick_mark));

        int shutterIso = CameraManualControl.GetClosestShutterSpeed(mUserExposureTime).ordinal();

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setProgress(shutterIso);

        findViewById(R.id.manualControl).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_SHUTTER_SPEED);
        findViewById(R.id.manualControl).setVisibility(View.VISIBLE);

        ((ImageView) findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void setAfLock(boolean lock, boolean uiOnly) {
        if(mAfLock == lock)
            return;

        int color = lock ? R.color.colorAccent : R.color.white;
        int drawable = lock ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) findViewById(R.id.afLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(this, drawable));
        ((TextView) findViewById(R.id.afLockBtnText)).setTextColor(getColor(color));

        if(!uiOnly && mNativeCamera != null) {
            mNativeCamera.setManualFocus(lock ? mFocusDistance : 0);
        }

        mAfLock = lock;

        if(!mAfLock) {
            hideManualControls();
        }

        mBinding.focusLockPointFrame.setVisibility(View.GONE);
    }

    private void setAwbLock(boolean lock) {
        if(mAwbLock == lock)
            return;

        int color = lock ? R.color.colorAccent : R.color.white;
        int drawable = lock ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) findViewById(R.id.awbLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(this, drawable));
        ((TextView) findViewById(R.id.awbLockBtnText)).setTextColor(getColor(color));

        if(mNativeCamera != null) {
            mNativeCamera.setAWBLock(lock);
        }

        mAwbLock = lock;
    }

    private void setAeLock(boolean lock) {
        if(mAeLock == lock)
            return;

        int color = lock ? R.color.colorAccent : R.color.white;
        int drawable = lock ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) findViewById(R.id.aeLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(this, drawable));
        ((TextView) findViewById(R.id.aeLockBtnText)).setTextColor(getColor(color));

        if(mNativeCamera != null) {
            if(mManualControlsSet && !lock) {
                mNativeCamera.setAutoExposure();
                mManualControlsSet = false;
            }
            else {
                mNativeCamera.setAELock(lock);
            }
        }

        if(lock && mCaptureMode == CaptureMode.NIGHT)
            setCaptureMode(CaptureMode.ZSL);

        mAeLock = lock;

        if(!mAeLock) {
            hideManualControls();
        }
    }

//    private void setOIS(boolean ois) {
//        if(mOIS == ois)
//            return;
//
//        int color = ois ? R.color.colorAccent : R.color.white;
//        ((TextView) findViewById(R.id.oisBtn)).setTextColor(getColor(color));
//
//        if(mNativeCamera != null) {
//            mNativeCamera.setOIS(ois);
//        }
//
//        mOIS = ois;
//    }

    private void onHorizontalCropChanged(int progress, boolean fromUser) {
        mSettings.horizontalVideoCrop = progress;
        if(mNativeCamera != null)
            mNativeCamera.setVideoCropPercentage(mSettings.horizontalVideoCrop, mSettings.verticalVideoCrop);

        updateVideoUi();
    }

    private void onVerticalCropChanged(int progress, boolean fromUser) {
        mSettings.verticalVideoCrop = progress;
        if(mNativeCamera != null)
            mNativeCamera.setVideoCropPercentage(mSettings.horizontalVideoCrop, mSettings.verticalVideoCrop);

        updateVideoUi();
    }

    private void toggleFrameRate() {
        int frameRate = mSettings.frameRate;

        if(frameRate == 30)
            frameRate = 25;
        else if(frameRate == 25)
            frameRate = 24;
        else if(frameRate == 24)
            frameRate = 12;
        else if(frameRate == 12)
            frameRate = 5;
        else if(frameRate == 5)
            frameRate = 1;
        else
            frameRate = 30;

        mSettings.frameRate = frameRate;

        if(mNativeCamera != null)
            mNativeCamera.setFrameRate(mSettings.frameRate);

        updateVideoUi();
    }

    private void toggleVideoBin() {
        mSettings.videoBin = !mSettings.videoBin;

        if(mNativeCamera != null)
            mNativeCamera.setVideoBin(mSettings.videoBin);

        updateVideoUi();
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
                mPostProcessSettings.saturation = progress > halfPoint ? 1.0f + ((progress - halfPoint) / halfPoint * 0.25f) : progress / seekBarMax * 2.0f;
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

            if(useHdr) {
                float a = 1.6f;
                if (mCameraMetadata.cameraApertures.length > 0)
                    a = mCameraMetadata.cameraApertures[0];

                long exposureTime = Math.round(mExposureTime / 4.0f);

                hdrExposure = CameraManualControl.Exposure.Create(
                        CameraManualControl.GetClosestShutterSpeed(exposureTime),
                        CameraManualControl.GetClosestIso(mIsoValues, mIso));

                hdrExposure = CameraManualControl.MapToExposureLine(a, hdrExposure, CameraManualControl.HDR_EXPOSURE_LINE);

                Log.i(TAG, "Precapturing HDR image");
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
                finaliseRawVideo(true);
            }
            else {
                startRawVideoRecording();
            }
        }
        else if(mode == CaptureMode.NIGHT) {
            mBinding.captureBtn.setEnabled(false);

            mBinding.captureProgressBar.setVisibility(View.VISIBLE);
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
            long cameraExposure = Math.round(mExposureTime * Math.pow(2.0f, mEstimatedSettings.exposure));

            // We'll estimate the shadows again since the exposure has been adjusted
            settings.shadows = -1;

            CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(cameraExposure),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            CameraManualControl.Exposure hdrExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(mExposureTime / 4),
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

            mNativeCamera.captureHdrImage(
                denoiseSettings.numMergeImages,
                baseExposure.iso.getIso(),
                baseExposure.shutterSpeed.getExposureTime(),
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
                    denoiseSettings.numMergeImages,  settings, CameraProfile.generateCaptureFile(this).getPath());
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
        mCameraInfos = Arrays.asList(mNativeCamera.getSupportedCameras());

        if(mCameraInfos.isEmpty()) {
            // Stop
            mNativeCamera = null;

            // No supported cameras. Display message to user and exist
            AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                    .setCancelable(false)
                    .setTitle(R.string.error)
                    .setMessage(R.string.not_supported_error)
                    .setPositiveButton(R.string.ok, (dialog, which) -> finish());

            dialogBuilder.create().show();

            return;
        }

        // Pick first camera if none selected
        if(mSelectedCamera == null) {
            mSelectedCamera = mCameraInfos.get(0);
        }

        Log.d(TAG, mSelectedCamera.toString());
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

        if(mSelectedCamera == null) {
            Log.e(TAG, "No cameras found");
            return;
        }

        // Exposure compensation frame
        findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

        // Set up camera manual controls
        mCameraMetadata = mNativeCamera.getMetadata(mSelectedCamera);

        // Keep range of valid ISO/shutter speeds
        mIsoValues = CameraManualControl.GetIsoValuesInRange(mCameraMetadata.isoMin, mCameraMetadata.isoMax);

        mExposureValues = CameraManualControl.GetExposureValuesInRange(
                mCameraMetadata.exposureTimeMin,
                Math.min(MAX_EXPOSURE_TIME.getExposureTime(), mCameraMetadata.exposureTimeMax));

//        if(mCameraMetadata.oisSupport) {
//            findViewById(R.id.oisBtn).setVisibility(View.VISIBLE);
//        }
//        else {
//            findViewById(R.id.oisBtn).setVisibility(View.INVISIBLE);
//        }

        ((TextView) findViewById(R.id.isoBtn)).setText("-");
        ((TextView) findViewById(R.id.shutterSpeedBtn)).setText("-");
        ((TextView) findViewById(R.id.focusBtn)).setText("-");

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

        updateVideoUi();
    }

    /**
     * configureTransform()
     * Courtesy to https://github.com/google/cameraview/blob/master/library/src/main/api14/com/google/android/cameraview/TextureViewPreview.java#L108
     */
    private void configureTransform(int width, int height, Size previewOutputSize) {
        Matrix matrix = new Matrix();
        int displayOrientation = getWindowManager().getDefaultDisplay().getRotation();

        if (displayOrientation % 180 == 90) {
            // Rotate the camera preview when the screen is landscape.
            matrix.setPolyToPoly(
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
                                    0.f, 0.f, // top right
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
            matrix.postRotate(180, width / 2, height / 2);
        }

        mTextureView.setTransform(matrix);
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

        mSurface = new Surface(surfaceTexture);
        mNativeCamera.startCapture(
                mSelectedCamera,
                mSurface,
                mSettings.useDualExposure,
                mSettings.rawMode == SettingsViewModel.RawMode.RAW12,
                mSettings.rawMode == SettingsViewModel.RawMode.RAW16);

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

        findViewById(R.id.aeLockBtn).setOnClickListener(v -> setAeLock(!mAeLock));
        findViewById(R.id.awbLockBtn).setOnClickListener(v -> setAwbLock(!mAwbLock));
        //findViewById(R.id.oisBtn).setOnClickListener(v -> setOIS(!mOIS));
        findViewById(R.id.isoBtn).setOnClickListener(v -> toggleIso());
        findViewById(R.id.focusBtn).setOnClickListener(v -> toggleFocus());
        findViewById(R.id.afLockBtn).setOnClickListener(v -> setAfLock(!mAfLock, false));
        findViewById(R.id.shutterSpeedBtn).setOnClickListener(v -> toggleShutterSpeed());

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setOnSeekBarChangeListener(mSeekBarChangeListener);

        mBinding.previewFrame.videoCropToggle.setOnClickListener(v -> toggleVideoCrop());
        mBinding.previewFrame.videoFrameRateBtn.setOnClickListener(v -> toggleFrameRate());
        mBinding.previewFrame.videoBinBtn.setOnClickListener(v -> toggleVideoBin());

        mBinding.previewFrame.horizontalCropSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.previewFrame.verticalCropSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);

        configureTransform(width, height, previewOutputSize);
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
        if(mIso >= 1600 || mExposureTime > CameraManualControl.SHUTTER_SPEED.EXPOSURE_1_40.getExposureTime())
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

                mBinding.manualControlsFrame.setVisibility(View.VISIBLE);
                mBinding.manualControlsFrame.setAlpha(0.0f);

                updateManualControlView(mSensorEventManager.getOrientation());
            });
        }
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
        final CameraManualControl.ISO cameraIso = CameraManualControl.GetClosestIso(mIsoValues, iso);
        final CameraManualControl.SHUTTER_SPEED cameraShutterSpeed = CameraManualControl.GetClosestShutterSpeed(exposureTime);

        runOnUiThread(() -> {
            ((TextView) findViewById(R.id.isoBtn)).setText(cameraIso.toString());
            ((TextView) findViewById(R.id.shutterSpeedBtn)).setText(cameraShutterSpeed.toString());

            mIso = iso;
            mExposureTime = exposureTime;

            autoSwitchCaptureMode();
        });
    }

    @Override
    public void onCameraAutoFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state, float focusDistance) {
        runOnUiThread(() -> onFocusStateChanged(state, focusDistance));
    }

    private void startImageProcessor() {
        OneTimeWorkRequest request =
                new OneTimeWorkRequest.Builder(ImageProcessWorker.class).build();

        WorkManager.getInstance(this)
                .enqueueUniqueWork(WORKER_IMAGE_PROCESSOR, ExistingWorkPolicy.APPEND_OR_REPLACE, request);

//        WorkManager.getInstance(getApplicationContext())
//                .getWorkInfoByIdLiveData(request.getId())
//                .observe(this, workInfo -> {
//                    if (workInfo == null) {
//                        return;
//                    }
//
//                    Data progress;
//
//                    if (workInfo.getState().isFinished()) {
//                        progress = workInfo.getOutputData();
//                    } else {
//                        progress = workInfo.getProgress();
//                    }
//
//                    int state = progress.getInt(State.PROGRESS_STATE_KEY, -1);
//
//                    if (state == State.STATE_PREVIEW_CREATED) {
//                        onPreviewSaved(progress.getString(State.PROGRESS_PREVIEW_PATH));
//                    }
//                    else if (state == State.STATE_COMPLETED) {
//                        String[] completedImages = progress.getStringArray(State.PROGRESS_IMAGE_PATH);
//                        String[] completedUris   = progress.getStringArray(State.PROGRESS_URI_KEY);
//
//                        for(int i = 0; i < completedImages.length; i++) {
//                            onProcessingCompleted(new File(completedImages[i]), Uri.parse(completedUris[i]));
//                        }
//                    }
//                });
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
    public void onMemoryAdjusting() {
        runOnUiThread(() -> {
            mBinding.captureBtn.setEnabled(false);

            mBinding.captureProgressBar.setVisibility(View.VISIBLE);
            mBinding.captureProgressBar.setIndeterminateMode(true);
        });
    }

    @Override
    public void onMemoryStable() {
        runOnUiThread(() -> {
            mBinding.captureBtn.setEnabled(true);
            mBinding.captureProgressBar.setVisibility(View.INVISIBLE);
            mBinding.captureProgressBar.setIndeterminateMode(false);
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

    private void alignManualControlView(NativeCameraBuffer.ScreenOrientation orientation) {
        ViewGroup.LayoutParams params = mBinding.manualControlsFrame.getLayoutParams();
        if(     orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE
            ||  orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE)
        {
            params.width = mBinding.cameraFrame.getHeight();
        }
        else {
            params.width = mBinding.cameraFrame.getWidth();
        }

        mBinding.manualControlsFrame.setLayoutParams(params);
        mBinding.manualControlsFrame.setAlpha(1.0f);

        final int rotation;
        final int translationX;
        final int translationY;

        // Update position of manual controls
        if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {
            rotation = 180;
            translationX = 0;
            translationY = -(mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight()) / 2;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
            rotation = 90;
            translationX = -mBinding.cameraFrame.getWidth()/2 + mBinding.manualControlsFrame.getHeight()/2;
            translationY = 0;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE) {
            rotation = -90;
            translationX = mBinding.cameraFrame.getWidth()/2 - mBinding.manualControlsFrame.getHeight()/2;
            translationY = 0;
        }
        else {
            // Portrait
            rotation = 0;
            translationX = 0;
            translationY = (mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight()) / 2;
        }

        mBinding.manualControlsFrame.setRotation(rotation);
        mBinding.manualControlsFrame.setTranslationX(translationX);
        mBinding.manualControlsFrame.setTranslationY(translationY);
    }

    private void updateManualControlView(NativeCameraBuffer.ScreenOrientation orientation) {
        mBinding.manualControlsFrame.post(() -> alignManualControlView(orientation));
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

        updateManualControlView(orientation);
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
                    mTintOffset,
                    mCaptureMode == CaptureMode.RAW_VIDEO);
        }
    }

    private void onFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state, float focusDistance) {
        Log.i(TAG, "Focus state: " + state.name());

        if(mTextureView == null)
            return;

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
                        .withEndAction(() -> mBinding.focusLockPointFrame.setVisibility(View.GONE))
                        .start();
            }
        }

        float focusDistanceMeters = 1.0f / focusDistance;

        ((TextView) findViewById(R.id.focusBtn)).setText(String.format(Locale.US, "%.2f M", focusDistanceMeters));

        mFocusDistance = focusDistance;
    }

    private void setAutoExposureState(NativeCameraSessionBridge.CameraExposureState state) {
        boolean timePassed = System.currentTimeMillis() - mFocusRequestedTimestampMs > 3000;

        if(state == NativeCameraSessionBridge.CameraExposureState.SEARCHING && timePassed)
        {
            setFocusState(FocusState.AUTO, null);
        }

        mExposureState = state;
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

        // AF lock turned off
        setAfLock(false, true);

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

    private void onProgressChanged(List<WorkInfo> workInfos) {
        WorkInfo currentWorkInfo = null;

        for(WorkInfo workInfo : workInfos) {
            if(workInfo.getState() == WorkInfo.State.RUNNING) {
                currentWorkInfo = workInfo;
                break;
            }
        }

        if (currentWorkInfo != null) {
            Data progress = currentWorkInfo.getProgress();

            int state = progress.getInt(State.PROGRESS_STATE_KEY, -1);

            if (state == State.STATE_PREVIEW_CREATED) {
                onPreviewSaved(progress.getString(State.PROGRESS_PREVIEW_PATH));
            }
            else if (state == State.STATE_PROCESSING) {
                int progressAmount = progress.getInt(State.PROGRESS_PROGRESS_KEY, 0);

                mBinding.processingProgress.setProgress(progressAmount);
            }
        }
        else {
            mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
        }

        for(WorkInfo workInfo : workInfos) {
            if (workInfo.getState() == WorkInfo.State.SUCCEEDED) {
                Data output = workInfo.getOutputData();
                int state = output.getInt(State.PROGRESS_STATE_KEY, -1);

                if (state == State.STATE_COMPLETED) {
                    String[] completedImages = output.getStringArray(State.PROGRESS_IMAGE_PATH);
                    String[] completedUris = output.getStringArray(State.PROGRESS_URI_KEY);

                    for (int i = 0; i < completedImages.length; i++) {
                        mCameraCapturePreviewAdapter.complete(new File(completedImages[i]), Uri.parse(completedUris[i]));
                    }
                }
            }
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
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
