package com.motioncam;

import android.Manifest;
import android.app.ProgressDialog;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.graphics.SurfaceTexture;
import android.location.Location;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
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
import android.view.Gravity;
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
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.appcompat.widget.SwitchCompat;
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
import com.motioncam.Settings.CaptureMode;
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
import java.util.Collections;
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
    private static final int DELAY_OUTPUT_FPS_MS = 3000;

    private static final int MANUAL_CONTROL_MODE_ISO = 0;
    private static final int MANUAL_CONTROL_MODE_SHUTTER_SPEED = 1;
    private static final int MANUAL_CONTROL_MODE_FOCUS = 2;

    private static final int OVERLAY_UPDATE_FREQUENCY_MS = 100;

    private static final int[] ALL_FRAME_RATE_OPTIONS = new int[] { 120, 60, 50, 48, 30, 25, 24, 15, 10, 5, 2, 1};

    public static final String WORKER_IMAGE_PROCESSOR = "ImageProcessor";
    public static final String WORKER_VIDEO_PROCESSOR = "VideoProcessor";

    private static final CameraManualControl.SHUTTER_SPEED MAX_EXPOSURE_TIME =
            CameraManualControl.SHUTTER_SPEED.EXPOSURE_2__0;

    private static final String[] MINIMUM_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.WRITE_EXTERNAL_STORAGE
    };

    private static final String[] ADDITIONAL_PERMISSIONS = {
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.RECORD_AUDIO
    };

    private enum FocusState {
        AUTO,
        FIXED,
        FIXED_AF_AE
    }

    private Settings mSettings;
    private Settings.CameraSettings mCameraSettings;

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
    private int mAudioInputId;

    private CameraCapturePreviewAdapter mCameraCapturePreviewAdapter;

    private PostProcessSettings mPostProcessSettings = new PostProcessSettings();
    private PostProcessSettings mEstimatedSettings = new PostProcessSettings();

    private float mTemperatureOffset;
    private float mTintOffset;

    private CaptureMode mCaptureMode;
    private boolean mUserCaptureModeOverride;

    private FocusState mFocusState = FocusState.AUTO;
    private NativeCameraSessionBridge.CameraExposureState mExposureState;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private boolean mAwbLock;
    private boolean mAeLock;
    private boolean mAfLock;
    private int mIso;
    private long mExposureTime;
    private float mFocusDistance;
    private float mShadowOffset;
    private long mFocusRequestedTimestampMs;
    private Timer mRecordingTimer;
    private Timer mOverlayTimer;
    private boolean mUnsupportedFrameRate;

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
            else if(seekBar == findViewById(R.id.manualControlSeekBar)) {
                onManualControlSettingsChanged(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.widthCropSeekBar)) {
                onWidthCropChanged(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.heightCropSeekBar)) {
                onHeightCropChanged(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.contrastSeekBar)) {
                onCameraSettingsContrastUpdated(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.colorSeekBar)) {
                onCameraSettingsColourUpdated(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.tintSeekBar)) {
                onCameraSettingsTintUpdated(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.warmthSeekBar)) {
                onCameraSettingsWarmthUpdated(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.sharpnessSeekBar)) {
                onCameraSettingsSharpnessUpdated(progress, fromUser);
            }
            else if(seekBar == mBinding.cameraSettings.findViewById(R.id.detailSeekBar)) {
                onCameraSettingsDetailUpdated(progress, fromUser);
            }
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
        }
    };

    private class OverlayTimer extends TimerTask {
        private Bitmap mBitmap = null;

        @Override
        public void run() {
            AsyncNativeCameraOps ops = new AsyncNativeCameraOps(mNativeCamera);

            ops.generateStats(bitmap ->
            {
                if(bitmap != null)
                    mBinding.cameraOverlay.setImageBitmap(bitmap);

                mBitmap = bitmap;
            }, mBitmap);
        }
    }

    private class RecordingTimerTask extends TimerTask {
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

                // Display space usage
                if(spaceLeft < 25) {
                    mBinding.previewFrame.freeSpaceProgress.getProgressDrawable()
                            .setTint(getColor(R.color.cancelAction));
                }
                else {
                    mBinding.previewFrame.freeSpaceProgress.getProgressDrawable()
                            .setTint(getColor(R.color.acceptAction));
                }

                // Display memory usage
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

                String outputSizeText = String.format(Locale.US, "%s\n%s", size, getString(R.string.size));

                // Wait for the frame rate to stabilise
                String outputFpsText;

                if(timeRecording > DELAY_OUTPUT_FPS_MS)
                    outputFpsText = String.format(Locale.US, "%d\n%s", Math.round(stats.fps), getString(R.string.output_fps));
                else
                    outputFpsText = String.format(Locale.US, "-\n%s", getString(R.string.output_fps));

                mBinding.previewFrame.outputFps.setText(outputFpsText);
                mBinding.previewFrame.outputSize.setText(outputSizeText);
            });
        }
    }

    @Override
    public void onBackPressed() {
        if (mBinding.main.getCurrentState() == mBinding.main.getEndState()) {
            mBinding.main.transitionToStart();
        } else {
            super.onBackPressed();
        }
    }

    private void onCameraSettingsSharpnessUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            mPostProcessSettings.sharpen0 = 1.0f + (progress / 50.0f);
        }
    }

    private void onCameraSettingsDetailUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            mPostProcessSettings.sharpen1 = 1.0f + (progress / 50.0f);
        }
    }

    private void onCameraSettingsContrastUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            mPostProcessSettings.contrast = progress / 100.0f;
            updatePreviewSettings();
        }
    }

    private void onCameraSettingsColourUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            final float halfPoint = 50;
            final float max = 100;

            float value = progress > halfPoint ? 1.0f + ((progress - halfPoint) / halfPoint * 0.25f) : progress / max * 2.0f;

            mPostProcessSettings.saturation = value;
            updatePreviewSettings();
        }
    }

    private void onCameraSettingsTintUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            mTintOffset = (progress / 100.0f - 0.5f) * 100.0f;
            updatePreviewSettings();
        }
    }

    private void onCameraSettingsWarmthUpdated(int progress, boolean fromUser) {
        if(fromUser) {
            mTemperatureOffset = (progress / 100.0f - 0.5f) * 2000.0f;
            updatePreviewSettings();
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

    private void releasePermissions(ContentResolver contentResolver) {
        List<Uri> takenUris = new ArrayList<>();

        if(mSettings.rawVideoRecordingTempUri != null)
            takenUris.add(mSettings.rawVideoRecordingTempUri);

        if(mSettings.rawVideoRecordingTempUri2 != null)
            takenUris.add(mSettings.rawVideoRecordingTempUri2);

        if(mSettings.rawVideoExportUri != null)
            takenUris.add(mSettings.rawVideoExportUri);

        // Release previous permission if set
        List<UriPermission> persistedUriPermissions = contentResolver.getPersistedUriPermissions();

        final int takenFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;

        for(UriPermission permission : persistedUriPermissions) {
            if(takenUris.contains(permission.getUri()))
                continue;

            try {
                Log.i(CameraActivity.TAG, "Releasing permissions for " + permission.getUri());
                contentResolver.releasePersistableUriPermission(permission.getUri(), takenFlags);
            }
            catch(SecurityException e) {
                e.printStackTrace();
            }
        }
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
        mCameraSettings = new Settings.CameraSettings();

        mBinding.focusLockPointFrame.setOnClickListener(v -> onFixedFocusCancelled());
        mBinding.previewFrame.settingsBtn.setOnClickListener(v -> onSettingsClicked());
        mBinding.previewFrame.processVideoBtn.setOnClickListener(v -> onProcessVideoClicked());

        mCameraCapturePreviewAdapter = new CameraCapturePreviewAdapter(getApplicationContext());
        mBinding.previewPager.setAdapter(mCameraCapturePreviewAdapter);

        mBinding.shareBtn.setOnClickListener(this::share);
        mBinding.openBtn.setOnClickListener(this::open);

        mBinding.onBackFromPreviewBtn.setOnClickListener(v -> mBinding.main.transitionToStart());
        mBinding.main.setTransitionListener(this);

        mBinding.shadowsSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);
        mBinding.exposureSeekBar.setOnSeekBarChangeListener(mSeekBarChangeListener);

        // Buttons
        mBinding.captureBtn.setOnTouchListener((v, e) -> onCaptureTouched(e));
        mBinding.captureBtn.setOnClickListener(v -> onCaptureClicked());
        mBinding.switchCameraBtn.setOnClickListener(v -> onSwitchCameraClicked());

        mBinding.nightModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.burstModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.zslModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.rawVideoModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.cameraSettingsBtn.setOnClickListener(this::onToggleCameraSettings);

        // Camera settings
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.contrastSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.colorSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.tintSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.warmthSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.sharpnessSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.detailSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        mBinding.cameraSettings.findViewById(R.id.saveDngBtn)
                .setOnClickListener(v -> setSaveRaw(!mPostProcessSettings.dng));

        mBinding.cameraSettings.findViewById(R.id.exposureOverlayBtn)
                .setOnClickListener(v -> setExposureOverlay(!mSettings.exposureOverlay));

        mBinding.cameraSettings.findViewById(R.id.hdrBtn)
                .setOnClickListener(v -> setHdr(!mSettings.hdr));

        mBinding.cameraSettings.findViewById(R.id.oisBtn)
                .setOnClickListener(v -> setOIS(!mSettings.cameraStartupSettings.ois));

        findViewById(R.id.aeLockBtn).setOnClickListener(v -> setAeLock(!mAeLock));
        findViewById(R.id.awbLockBtn).setOnClickListener(v -> setAwbLock(!mAwbLock));
        findViewById(R.id.afLockBtn).setOnClickListener(v -> setAfLock(!mAfLock, false));

        findViewById(R.id.isoBtn).setOnClickListener(v -> toggleIso());
        findViewById(R.id.focusBtn).setOnClickListener(v -> toggleFocus());
        findViewById(R.id.shutterSpeedBtn).setOnClickListener(v -> toggleShutterSpeed());
        findViewById(R.id.manualControlPlusBtn).setOnClickListener(v -> onManualControlPlus());
        findViewById(R.id.manualControlMinusBtn).setOnClickListener(v -> onManualControlMinus());

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.pixelBinSwitch))
                .setOnCheckedChangeListener((btn, isChecked) -> toggleVideoBin(isChecked));

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.widthCropSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.heightCropSeekBar))
                .setOnSeekBarChangeListener(mSeekBarChangeListener);

        mBinding.previewFrame.previewControls.setVisibility(View.VISIBLE);

        mSensorEventManager = new SensorEventManager(this, this);
        mFusedLocationClient = LocationServices.getFusedLocationProviderClient(this);
        mAudioInputId = -1;

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

    private void onProcessVideoClicked() {
        Intent intent = new Intent(this, ConvertVideoActivity.class);
        startActivityForResult(intent, CONVERT_VIDEO_ACTIVITY_REQUEST_CODE);
    }

    private void onFixedFocusCancelled() {
        setFocusState(FocusState.AUTO, null);
    }

    private void setPostProcessingDefaults() {
        // Set initial preview values
        mPostProcessSettings.shadows = -1.0f;
        mPostProcessSettings.brightness = 1.125f;
        mPostProcessSettings.greens = 0.0f;
        mPostProcessSettings.blues = 0.0f;
        mPostProcessSettings.pop = 1.25f;
        mPostProcessSettings.whitePoint = -1;
        mPostProcessSettings.blacks = -1;
        mPostProcessSettings.tonemapVariance = 0.25f;
        mPostProcessSettings.jpegQuality = mSettings.jpegQuality;
        mPostProcessSettings.dng = mSettings.saveDng;

        mPostProcessSettings.contrast = 0.5f;
        mPostProcessSettings.saturation = 1.0f;
        mPostProcessSettings.sharpen0 = 2.25f;
        mPostProcessSettings.sharpen1 = 2.0f;

        mTemperatureOffset = 0;
        mTintOffset = 0;

        if(mCameraSettings != null) {
            mPostProcessSettings.contrast = mCameraSettings.contrast;
            mPostProcessSettings.saturation = mCameraSettings.saturation;
            mPostProcessSettings.sharpen0 = mCameraSettings.sharpness;
            mPostProcessSettings.sharpen1 = mCameraSettings.detail;

            mTemperatureOffset = mCameraSettings.temperatureOffset;
            mTintOffset = mCameraSettings.tintOffset;
        }

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

        // Release URIs we are not using anymore
        releasePermissions(getContentResolver());

        // Hide camera settings
        toggleCameraSettings(false);

        // Get audio inputs
        enumerateAudioInputs();

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

    private void saveSettings() {
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        // Update the settings
        mSettings.saveDng = mPostProcessSettings.dng;
        mSettings.captureMode = mCaptureMode;

        // Reset frame rate when an unsupported frame has been selected
        if(mUnsupportedFrameRate)
            mSettings.cameraStartupSettings.frameRate = 30;

        mSettings.save(sharedPrefs);

        // Camera specific settings
        if(mCameraSettings != null && mSelectedCamera != null) {
            mCameraSettings.contrast = mPostProcessSettings.contrast;
            mCameraSettings.saturation = mPostProcessSettings.saturation;
            mCameraSettings.tintOffset = mTintOffset;
            mCameraSettings.temperatureOffset = mTemperatureOffset;
            mCameraSettings.sharpness = mPostProcessSettings.sharpen0;
            mCameraSettings.detail = mPostProcessSettings.sharpen1;

            mCameraSettings.save(sharedPrefs, mSelectedCamera.cameraId);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        mSensorEventManager.disable();
        mBinding.previewPager.unregisterOnPageChangeCallback(mCapturedPreviewPagerListener);

        saveSettings();

        if(mOverlayTimer != null) {
            mOverlayTimer.cancel();
            mOverlayTimer = null;
        }

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

    private int createNewFile(Uri rootFolder, String filename, String mimeType) {
        try {
            DocumentFile root = DocumentFile.fromTreeUri(this, rootFolder);

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

        return -1;
    }

    private List<Integer> getVideoRecordingFds(String filename, String mimeType) {
        List<Integer> fds = new ArrayList<>();

        // Primary recording folder should always be available
        if(mSettings.rawVideoRecordingTempUri == null) {
            return new ArrayList<>();
        }

        int fd0 = createNewFile(mSettings.rawVideoRecordingTempUri, filename, mimeType);
        if (fd0 < 0) {
            return fds;
        }

        fds.add(fd0);

        // Try to get second recording location
        if(mSettings.useSecondaryRawVideoStorage && mSettings.rawVideoRecordingTempUri2 != null) {
            int fd1 = createNewFile(mSettings.rawVideoRecordingTempUri2, CameraProfile.nextSegment(filename), mimeType);
            if (fd1 >= 0) {
                fds.add(fd1);
            }
            else {
                String error = getString(R.string.invalid_secondary_raw_video_folder);
                Toast.makeText(CameraActivity.this, error, Toast.LENGTH_LONG).show();
            }
        }

        return fds;
    }

    private void startRawVideoRecording() {
        Log.i(TAG, "Starting RAW video recording (max memory usage: " + mSettings.rawVideoMemoryUseBytes + ")");

        // Try to get a writable fds
        String videoName = CameraProfile.generateFilename("VIDEO", 0, ".container");
        String audioName = videoName.replace(".0.container", ".wav");

        // Get audio fd
        int audioFd = -1;

        if(mSettings.rawVideoRecordingTempUri != null)
            audioFd = createNewFile(mSettings.rawVideoRecordingTempUri, audioName, "audio/wav");

        if(audioFd < 0)
            audioFd = getInternalRecordingFd(audioName);

        if(audioFd < 0) {
            String error = getString(R.string.recording_failed);
            Toast.makeText(CameraActivity.this, error, Toast.LENGTH_LONG).show();

            Log.e(TAG, "Failed to start recording audioFd < 0");
            return;
        }

        // Get a fd for each configured video storage location
        List<Integer> videoFds = getVideoRecordingFds(videoName, "application/octet-stream");
        if(videoFds.isEmpty()) {
            int fd = getInternalRecordingFd(videoName);
            if(fd < 0) {
                String error = getString(R.string.recording_failed);
                Toast.makeText(CameraActivity.this, error, Toast.LENGTH_LONG).show();

                Log.e(TAG, "Failed to start recording videoFds < 0");
                return;
            }

            videoFds.add(fd);
        }

        // Create list of all fds
        int fds[] = videoFds
                .stream()
                .mapToInt(i->i)
                .toArray();

        int numThreads = mSettings.enableRawVideoCompression ? mSettings.numRawVideoCompressionThreads : 2;

        Log.d(TAG, "streamToFile(enableRawCompression=" + mSettings.enableRawVideoCompression + ", numThreads=" + numThreads + ")");

        mNativeCamera.streamToFile(fds, audioFd, mAudioInputId, mSettings.enableRawVideoCompression, numThreads);
        mImageCaptureInProgress.set(true);

        mBinding.switchCameraBtn.setEnabled(false);
        mBinding.recordingTimer.setVisibility(View.VISIBLE);

        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.GONE);
        mBinding.previewFrame.videoRecordingStats.setVisibility(View.VISIBLE);

        mBinding.cameraSettingsBtn.setVisibility(View.GONE);
        toggleCameraSettings(false);

        mRecordStartTime = System.currentTimeMillis();

        // Update recording time
        mRecordingTimer = new Timer();
        mRecordingTimer.scheduleAtFixedRate(new RecordingTimerTask(), 0, 500);
    }

    private void finaliseRawVideo(boolean showProgress) {
        mImageCaptureInProgress.set(false);

        mBinding.switchCameraBtn.setEnabled(true);
        mBinding.recordingTimer.setVisibility(View.GONE);

        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.VISIBLE);
        mBinding.previewFrame.videoRecordingStats.setVisibility(View.GONE);
        mBinding.cameraSettingsBtn.setVisibility(View.VISIBLE);

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
        activateCamera((String) view.getTag());
    }

    private void activateCamera(String cameraId) {
        if(cameraId == null || cameraId.equals(mSelectedCamera.cameraId))
            return;

        Log.d(TAG, "Activating camera " + cameraId);

        // Save settings for current camera
        saveSettings();

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
            activateCamera(mCameraInfos.get(0).cameraId);
        }
        else {
            for (int i = 0; i < mCameraInfos.size(); i++) {
                if (mCameraInfos.get(i).isFrontFacing) {
                    activateCamera(mCameraInfos.get(i).cameraId);
                }
            }
        }
    }

    private void updateVideoUi() {
        String resText = getText(R.string.output).toString();

        if(mNativeCamera != null) {
            Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

            int bin = mSettings.videoBin ? 2 : 1;

            int width = captureOutputSize.getWidth() / bin;
            int height = captureOutputSize.getHeight() / bin;

            width = Math.round(width - (mSettings.widthVideoCrop / 100.0f) * width);
            height = Math.round(height - (mSettings.heightVideoCrop / 100.0f) * height);

            width = width / 4 * 4;
            height = height / 2 * 2;

            mBinding.previewFrame.videoResolution.setText(String.format(Locale.US, "%dx%d\n%s", width, height, resText));
        }

        if(mCaptureMode == CaptureMode.RAW_VIDEO && (mSettings.widthVideoCrop > 0 || mSettings.heightVideoCrop > 0))
            // Swap width/height to match sensor. We are always in portrait mode.
            mBinding.gridLayout.setCropMode(true, mSettings.heightVideoCrop, mSettings.widthVideoCrop);
        else
            mBinding.gridLayout.setCropMode(false, 0, 0);
    }

    private boolean updateFpsToggle(ViewGroup fpsGroup) {
        for(int i = 0; i < fpsGroup.getChildCount(); i++) {
            View fpsToggle = fpsGroup.getChildAt(i);
            String fpsTag = (String) fpsToggle.getTag();

            if(fpsTag != null) {
                try {
                    int fps = Integer.valueOf(fpsTag);
                    if (mSettings.cameraStartupSettings.frameRate == fps) {
                        fpsToggle.setBackgroundColor(getColor(R.color.colorAccent));
                        return true;
                    }
                }
                catch(NumberFormatException e) {
                    Log.e(TAG, "Invalid fps", e);
                }
            }
        }

        return false;
    }

    private void updateCameraSettingsUi() {
        final int seekBarMax = 100;

        int contrastValue = Math.round(mPostProcessSettings.contrast * seekBarMax);
        int colorValue = Math.round(mPostProcessSettings.saturation / 2.0f * seekBarMax);
        int warmthValue = Math.round(((mTemperatureOffset + 1000.0f) / 2000.0f * seekBarMax));
        int tintValue = Math.round(((mTintOffset + 50.0f) / 100.0f * seekBarMax));
        int sharpnessValue = Math.round((mPostProcessSettings.sharpen0 - 1.0f) * 50);
        int detailValue = Math.round((mPostProcessSettings.sharpen1 - 1.0f) * 50);

        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.contrastSeekBar)).setProgress(contrastValue);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.colorSeekBar)).setProgress(colorValue);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.warmthSeekBar)).setProgress(warmthValue);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.tintSeekBar)).setProgress(tintValue);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.sharpnessSeekBar)).setProgress(sharpnessValue);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.detailSeekBar)).setProgress(detailValue);

        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.exposureOverlayBtn)).setChecked(mSettings.exposureOverlay);
        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.saveDngBtn)).setChecked(mSettings.saveDng);
        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.hdrBtn)).setChecked(mSettings.hdr);
        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.oisBtn)).setChecked(mSettings.cameraStartupSettings.ois);

        // Video section
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.widthCropSeekBar)).setProgress(mSettings.widthVideoCrop);
        ((SeekBar) mBinding.cameraSettings.findViewById(R.id.heightCropSeekBar)).setProgress(mSettings.heightVideoCrop);
        ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.pixelBinSwitch)).setChecked(mSettings.videoBin);

        String widthCropAmount = mSettings.widthVideoCrop + "%";
        String heightCropAmount = mSettings.heightVideoCrop + "%";

        ((TextView) mBinding.cameraSettings.findViewById(R.id.widthCropAmount)).setText(widthCropAmount);
        ((TextView) mBinding.cameraSettings.findViewById(R.id.heightCropAmount)).setText(heightCropAmount);

        ViewGroup fpsGroup = mBinding.cameraSettings.findViewById(R.id.fpsGroup);
        ViewGroup unsupportedFpsGroup = mBinding.cameraSettings.findViewById(R.id.unsupportedFpsGroup);

        for(int i = 0; i < fpsGroup.getChildCount(); i++) {
            fpsGroup.getChildAt(i).setBackground(null);
        }

        for(int i = 0; i < unsupportedFpsGroup.getChildCount(); i++) {
            unsupportedFpsGroup.getChildAt(i).setBackground(null);
        }

        // Keep track of whether an unsupported frame rate has been selected
        mUnsupportedFrameRate = false;

        if(!updateFpsToggle(fpsGroup)) {
            updateFpsToggle(unsupportedFpsGroup);
            mUnsupportedFrameRate = true;
        }
    }

    private void setupRawVideoCapture() {
        mBinding.previewFrame.videoRecordingBtns.setVisibility(View.VISIBLE);
        mBinding.previewFrame.settingsLayout.setVisibility(View.GONE);
        mBinding.cameraSettings.findViewById(R.id.cameraVideoSettings).setVisibility(View.VISIBLE);
        mBinding.cameraSettings.findViewById(R.id.cameraPhotoSettings).setVisibility(View.GONE);

        if(mNativeCamera != null) {
            mNativeCamera.setFrameRate(mSettings.cameraStartupSettings.frameRate);
            mNativeCamera.setVideoCropPercentage(mSettings.widthVideoCrop, mSettings.heightVideoCrop);
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

        mNativeCamera.setFrameRate(-1);
        mNativeCamera.adjustMemory(mSettings.memoryUseBytes);

        if(mSettings.useDualExposure) {
            mBinding.rawCameraPreview.setVisibility(View.VISIBLE);
            mBinding.shadowsLayout.setVisibility(View.VISIBLE);

            if(mTextureView != null)
                mTextureView.setAlpha(0);

            mNativeCamera.enableRawPreview(this, mSettings.cameraPreviewQuality, false);
        }

        mBinding.cameraSettings.findViewById(R.id.cameraVideoSettings).setVisibility(View.GONE);
        mBinding.cameraSettings.findViewById(R.id.cameraPhotoSettings).setVisibility(View.VISIBLE);

        mBinding.previewFrame.settingsLayout.setVisibility(View.VISIBLE);
    }

    private void setCaptureMode(CaptureMode captureMode) {
        setCaptureMode(captureMode, false);
    }

    private void setCaptureMode(CaptureMode captureMode, boolean forceUpdate) {
        if(!forceUpdate && (mCaptureMode == captureMode || mImageCaptureInProgress.get()))
            return;

        // Can't use night mode with manual controls
        if(mSettings.cameraStartupSettings.useUserExposureSettings && captureMode == CaptureMode.NIGHT)
            return;

        mBinding.nightModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.zslModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.burstModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.rawVideoModeBtn.setTextColor(getColor(R.color.textColor));

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
        if(mCaptureMode == CaptureMode.RAW_VIDEO && captureMode != CaptureMode.RAW_VIDEO) {
            restoreFromRawVideoCapture();
        }

        mCaptureMode = captureMode;

        updatePreviewSettings();
        updateVideoUi();
    }

    private void setHdr(boolean hdr) {
        mSettings.hdr = hdr;
    }

    private void hideExposureControls() {
        ((ImageView) findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        ((ImageView) findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        findViewById(R.id.manualControlExposure).setVisibility(View.GONE);
        findViewById(R.id.manualControlExposure).post(() -> alignManualControlView(mSensorEventManager.getOrientation(), false));
    }

    private void hideFocusControls() {

        ((ImageView) findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));

        findViewById(R.id.manualControlFocus).setVisibility(View.GONE);
        findViewById(R.id.manualControlFocus).post(() -> alignManualControlView(mSensorEventManager.getOrientation(), false));
    }

    private void onManualControlSettingsChanged(int progress, boolean fromUser) {
        if(mNativeCamera != null && fromUser) {
            int selectionMode = (int) findViewById(R.id.manualControlFocus).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                double p;

                if(progress < 100) {
                    double in = (100.0f - progress) / 100.0f;
                    p = Math.exp(in * (Math.log(mCameraMetadata.minFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)) + Math.log(mCameraMetadata.hyperFocalDistance));
                }
                else {
                    p = 0.0;
                }

                mFocusDistance = (float) p;
                mNativeCamera.setManualFocus(mFocusDistance);

                onFocusStateChanged(NativeCameraSessionBridge.CameraFocusState.FOCUS_LOCKED, mFocusDistance);
            }
        }
    }

    private void toggleFocus() {
        // Hide if shown
        if(findViewById(R.id.manualControlFocus).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControlFocus).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                hideFocusControls();
                return;
            }
        }

        setAfLock(true, false);

        double progress = 100.0 * (
            (Math.log(mFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)) /
            (Math.log(mCameraMetadata.minFocusDistance) - Math.log(mCameraMetadata.hyperFocalDistance)));

        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setMax(100);
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setProgress((int)Math.round(100 - progress));
        ((SeekBar) findViewById(R.id.manualControlSeekBar)).setTickMark(null);

        findViewById(R.id.manualControlFocus).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_FOCUS);
        findViewById(R.id.manualControlFocus).setVisibility(View.VISIBLE);

        ((ImageView) findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        findViewById(R.id.manualControlFocus).post(() -> alignManualControlView(mSensorEventManager.getOrientation(), false));
    }

    private void toggleIso() {
        // Hide if shown
        if(findViewById(R.id.manualControlExposure).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
                hideExposureControls();
                return;
            }
        }

        if(!mSettings.cameraStartupSettings.useUserExposureSettings) {
            mSettings.cameraStartupSettings.iso = mIso;
            mSettings.cameraStartupSettings.exposureTime = mExposureTime;
            mSettings.cameraStartupSettings.useUserExposureSettings = true;

            setAeLock(true);
        }

        CameraManualControl.ISO iso = CameraManualControl.GetClosestIso(mIsoValues, mSettings.cameraStartupSettings.iso);
        CameraManualControl.ISO[] isoValues = CameraManualControl.ISO.values();

        int nextIdx = Math.min(isoValues.length - 1, iso.ordinal() + 1);
        int prevIdx = Math.max(0, iso.ordinal() - 1);

        ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(isoValues[prevIdx]));
        ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(isoValues[nextIdx]));

        findViewById(R.id.manualControlExposure).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_ISO);
        findViewById(R.id.manualControlExposure).setVisibility(View.VISIBLE);

        ((ImageView) findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        findViewById(R.id.manualControlExposure).post(() -> alignManualControlView(mSensorEventManager.getOrientation(), false));
    }

    private void onManualControlPlus() {
        if(mNativeCamera == null)
            return;

        int selectionMode = (int) findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

        if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
            CameraManualControl.ISO iso = CameraManualControl.GetClosestIso(mIsoValues, mSettings.cameraStartupSettings.iso);
            CameraManualControl.ISO[] isoValues = CameraManualControl.ISO.values();

            int nextIdx = Math.min(isoValues.length - 1, iso.ordinal() + 1);
            int nextNextIdx = Math.min(isoValues.length - 1, iso.ordinal() + 2);

            ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(iso));
            ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(isoValues[nextNextIdx]));

            mSettings.cameraStartupSettings.iso = isoValues[nextIdx].getIso();
        }
        else if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
            CameraManualControl.SHUTTER_SPEED shutterSpeed = CameraManualControl.GetClosestShutterSpeed(mSettings.cameraStartupSettings.exposureTime);
            CameraManualControl.SHUTTER_SPEED[] shutterSpeedValues = CameraManualControl.SHUTTER_SPEED.values();

            int nextIdx = Math.min(shutterSpeedValues.length - 1, shutterSpeed.ordinal() + 1);
            int nextNextIdx = Math.min(shutterSpeedValues.length - 1, shutterSpeed.ordinal() + 2);

            ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(shutterSpeed));
            ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(shutterSpeedValues[nextNextIdx]));

            mSettings.cameraStartupSettings.exposureTime = shutterSpeedValues[nextIdx].getExposureTime();
        }

        mNativeCamera.setManualExposureValues(mSettings.cameraStartupSettings.iso, mSettings.cameraStartupSettings.exposureTime);
    }

    private void onManualControlMinus() {
        int selectionMode = (int) findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

        if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
            CameraManualControl.ISO iso = CameraManualControl.GetClosestIso(mIsoValues, mSettings.cameraStartupSettings.iso);
            CameraManualControl.ISO[] isoValues = CameraManualControl.ISO.values();

            int prevIdx = Math.max(0, iso.ordinal() - 1);
            int prevPrevIdx = Math.max(0, iso.ordinal() - 2);

            ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(iso));
            ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(isoValues[prevPrevIdx]));

            mSettings.cameraStartupSettings.iso = isoValues[prevIdx].getIso();
        }
        else if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
            CameraManualControl.SHUTTER_SPEED shutterSpeed = CameraManualControl.GetClosestShutterSpeed(mSettings.cameraStartupSettings.exposureTime);
            CameraManualControl.SHUTTER_SPEED[] shutterSpeedValues = CameraManualControl.SHUTTER_SPEED.values();

            int prevIdx = Math.max(0, shutterSpeed.ordinal() - 1);
            int prevPrevIdx = Math.max(0, shutterSpeed.ordinal() - 2);

            ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(shutterSpeed));
            ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(shutterSpeedValues[prevPrevIdx]));

            mSettings.cameraStartupSettings.exposureTime = shutterSpeedValues[prevIdx].getExposureTime();
        }

        mNativeCamera.setManualExposureValues(mSettings.cameraStartupSettings.iso, mSettings.cameraStartupSettings.exposureTime);
    }

    private void toggleShutterSpeed() {
        // Hide if shown
        if(findViewById(R.id.manualControlExposure).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
                hideExposureControls();
                return;
            }
        }

        if(!mSettings.cameraStartupSettings.useUserExposureSettings) {
            mSettings.cameraStartupSettings.iso = mIso;
            mSettings.cameraStartupSettings.exposureTime = mExposureTime;
            mSettings.cameraStartupSettings.useUserExposureSettings = true;

            setAeLock(true);
        }

        findViewById(R.id.manualControlExposure).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_SHUTTER_SPEED);
        findViewById(R.id.manualControlExposure).setVisibility(View.VISIBLE);

        CameraManualControl.SHUTTER_SPEED shutterSpeed = CameraManualControl.GetClosestShutterSpeed(mSettings.cameraStartupSettings.exposureTime);
        CameraManualControl.SHUTTER_SPEED[] shutterSpeedValues = CameraManualControl.SHUTTER_SPEED.values();

        int prevIdx = Math.max(0, shutterSpeed.ordinal() - 1);
        int nextIdx = Math.min(shutterSpeedValues.length - 1, shutterSpeed.ordinal() + 1);

        ((TextView) findViewById(R.id.manualControlMinusBtn)).setText(String.valueOf(shutterSpeedValues[prevIdx]));
        ((TextView) findViewById(R.id.manualControlPlusBtn)).setText(String.valueOf(shutterSpeedValues[nextIdx]));

        ((ImageView) findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));

        findViewById(R.id.manualControlExposure).post(() -> alignManualControlView(mSensorEventManager.getOrientation(), false));
    }

    private void setAfLock(boolean lock, boolean uiOnly) {
        if(mAfLock == lock)
            return;

        int color = lock ? R.color.colorAccent : R.color.white;
        int drawable = lock ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) findViewById(R.id.afLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(this, drawable));
        ((TextView) findViewById(R.id.afLockBtnText)).setTextColor(getColor(color));

        if(!uiOnly && mNativeCamera != null) {
            mNativeCamera.setManualFocus(lock ? mFocusDistance : -1);
        }

        mAfLock = lock;

        if(!mAfLock) {
            hideFocusControls();
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
            if(mSettings.cameraStartupSettings.useUserExposureSettings && !lock) {
                mNativeCamera.setAutoExposure();
                mSettings.cameraStartupSettings.useUserExposureSettings = false;
            }
            else {
                mNativeCamera.setAELock(lock);
            }
        }

        if(lock && mCaptureMode == CaptureMode.NIGHT)
            setCaptureMode(CaptureMode.ZSL);

        mAeLock = lock;

        if(!mAeLock) {
            hideExposureControls();
        }
    }

    private void setOIS(boolean ois) {
        if(mSettings.cameraStartupSettings.ois == ois)
            return;

        if(mNativeCamera != null) {
            mNativeCamera.setOIS(ois);
        }

        mSettings.cameraStartupSettings.ois = ois;
    }

    private void onWidthCropChanged(int progress, boolean fromUser) {
        mSettings.widthVideoCrop = progress;
        if(mNativeCamera != null)
            mNativeCamera.setVideoCropPercentage(mSettings.widthVideoCrop, mSettings.heightVideoCrop);

        String widthCropAmount = mSettings.widthVideoCrop + "%";
        ((TextView) mBinding.cameraSettings.findViewById(R.id.widthCropAmount)).setText(widthCropAmount);

        updateVideoUi();
    }

    private void onHeightCropChanged(int progress, boolean fromUser) {
        mSettings.heightVideoCrop = progress;
        if(mNativeCamera != null)
            mNativeCamera.setVideoCropPercentage(mSettings.widthVideoCrop, mSettings.heightVideoCrop);

        String heightCropAmount = mSettings.heightVideoCrop + "%";
        ((TextView) mBinding.cameraSettings.findViewById(R.id.heightCropAmount)).setText(heightCropAmount);

        updateVideoUi();
    }

    private void toggleFrameRate(View v) {
        String fpsTag = (String) v.getTag();
        if(fpsTag == null)
            return;

        try {
            mSettings.cameraStartupSettings.frameRate = Integer.valueOf(fpsTag);
        }
        catch(NumberFormatException e) {
            Log.e(TAG, "Invalid FPS value", e);
            return;
        }

        if(mNativeCamera != null)
            mNativeCamera.setFrameRate(mSettings.cameraStartupSettings.frameRate);

        updateCameraSettingsUi();
    }

    private void toggleVideoBin(boolean enabled) {
        mSettings.videoBin = enabled;

        if(mNativeCamera != null)
            mNativeCamera.setVideoBin(mSettings.videoBin);

        updateVideoUi();
    }

    private void setExposureOverlay(boolean enable) {
        mSettings.exposureOverlay = enable;

        if(mOverlayTimer != null) {
            mOverlayTimer.cancel();
            mOverlayTimer = null;
        }

        if(mSettings.exposureOverlay) {
            mOverlayTimer = new Timer();
            mOverlayTimer.scheduleAtFixedRate(new OverlayTimer(), OVERLAY_UPDATE_FREQUENCY_MS, OVERLAY_UPDATE_FREQUENCY_MS);
            mBinding.cameraOverlay.setVisibility(View.VISIBLE);
        }
        else {
            mBinding.cameraOverlay.setVisibility(View.GONE);
        }
    }

    private void setSaveRaw(boolean saveRaw) {
        mPostProcessSettings.dng = saveRaw;
    }

    private void toggleCameraSettings(boolean show) {
        if(show) {
            mBinding.cameraSettingsBtn.setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_up));
            mBinding.cameraSettings.setVisibility(View.VISIBLE);
            alignCameraSettingsView(mSensorEventManager.getOrientation(), false);
        }
        else {
            mBinding.cameraSettings.setVisibility(View.INVISIBLE);
            mBinding.cameraSettingsBtn.setImageDrawable(AppCompatResources.getDrawable(this, R.drawable.chevron_down));
        }
    }

    private void onToggleCameraSettings(View v) {
        if(mBinding.cameraSettings.getVisibility() == View.VISIBLE) {
            toggleCameraSettings(false);
        }
        else {
            toggleCameraSettings(true);
        }
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
                    denoiseSettings.numMergeImages, settings, CameraProfile.generateCaptureFile(this).getPath());
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

        for(String permission : MINIMUM_PERMISSIONS) {
            if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                needPermissions.add(permission);
            }
        }

        if(!needPermissions.isEmpty()) {
            for(String permission : ADDITIONAL_PERMISSIONS) {
                if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                    needPermissions.add(permission);
                }
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
        List<String> minimumPermissions = Arrays.asList(MINIMUM_PERMISSIONS);

        for(int i = 0; i < permissions.length; i++) {
            if(grantResults[i] == PackageManager.PERMISSION_DENIED) {

                if(minimumPermissions.contains(permissions[i])) {
                    runOnUiThread(this::onPermissionsDenied);
                    return;
                }

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

        AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                .setCancelable(false)
                .setTitle(R.string.error)
                .setMessage(R.string.permissions_error)
                .setPositiveButton(R.string.ok, (dialog, which) -> finish());

        dialogBuilder.show();
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

    private View createFpsToggle(int fps) {
        String fpsValue = String.valueOf(fps);
        TextView fpsToggle = new TextView(this);

        fpsToggle.setTextAppearance(R.style.MotionCam_TextAppearance_Small);
        fpsToggle.setGravity(Gravity.CENTER);
        fpsToggle.setText(fpsValue);
        fpsToggle.setTag(fpsValue);
        fpsToggle.setTextColor(getColor(R.color.white));
        fpsToggle.setOnClickListener(v -> toggleFrameRate(v));

        return fpsToggle;
    }

    private void onVideoPresetSelected(View v) {
        if(mNativeCamera == null || mSelectedCamera == null)
            return;

        Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

        // Only doing 4K
        int cropWidth = Math.round(100 * (1.0f - (3840.0f / captureOutputSize.getWidth())));
        int cropHeight = Math.round(100 * (1.0f - (2160.0f / captureOutputSize.getHeight())));

        if(cropWidth < 0 || cropHeight < 0)
            return;

        ((SeekBar)mBinding.cameraSettings.findViewById(R.id.widthCropSeekBar)).setProgress(cropWidth, true);
        ((SeekBar)mBinding.cameraSettings.findViewById(R.id.heightCropSeekBar)).setProgress(cropHeight, true);

        if(v == findViewById(R.id.preset1080P)) {
            ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.pixelBinSwitch)).setChecked(true);
        }
        else if(v == findViewById(R.id.preset4K)) {
            ((SwitchCompat) mBinding.cameraSettings.findViewById(R.id.pixelBinSwitch)).setChecked(false);
        }

        updateVideoUi();
    }

    private void setupFpsSelection() {
        // Supported frame rates
        ViewGroup fpsGroup = mBinding.cameraSettings.findViewById(R.id.fpsGroup);
        fpsGroup.removeAllViews();

        List<Integer> fpsRange = Arrays.stream( mSelectedCamera.fpsRange )
                .boxed()
                .sorted(Collections.reverseOrder())
                .collect(Collectors.toList());

        for(Integer fps : fpsRange) {
            View fpsToggle = createFpsToggle(fps);
            fpsGroup.addView(
                    fpsToggle,
                    new LinearLayout.LayoutParams(
                            Math.round(getResources().getDimension(R.dimen.fps_toggle)),
                            ViewGroup.LayoutParams.MATCH_PARENT));
        }

        // Create unsupported frame rate group
        ViewGroup unsupportedFpsGroup = mBinding.cameraSettings.findViewById(R.id.unsupportedFpsGroup);
        unsupportedFpsGroup.removeAllViews();
        for(int fps : ALL_FRAME_RATE_OPTIONS) {
            View fpsToggle = createFpsToggle(fps);
            unsupportedFpsGroup.addView(
                    fpsToggle,
                    new LinearLayout.LayoutParams(
                            Math.round(getResources().getDimension(R.dimen.fps_toggle)),
                            ViewGroup.LayoutParams.MATCH_PARENT));
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

        SharedPreferences sharedPrefs =
                getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        mCameraSettings.load(sharedPrefs, mSelectedCamera.cameraId);

        setPostProcessingDefaults();
        updateCameraSettingsUi();

        // Exposure compensation frame
        findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

        // Set up camera manual controls
        mCameraMetadata = mNativeCamera.getMetadata(mSelectedCamera);
        Log.d(TAG, "Selected camera metadata: " + mCameraMetadata.toString());

        // Keep range of valid ISO/shutter speeds
        mIsoValues = CameraManualControl.GetIsoValuesInRange(mCameraMetadata.isoMin, mCameraMetadata.isoMax);

        mExposureValues = CameraManualControl.GetExposureValuesInRange(
                mCameraMetadata.exposureTimeMin,
                Math.min(MAX_EXPOSURE_TIME.getExposureTime(), mCameraMetadata.exposureTimeMax));

        if(mCameraMetadata.oisSupport) {
            findViewById(R.id.oisBtn).setEnabled(true);
        }
        else {
            findViewById(R.id.oisBtn).setEnabled(false);
        }

        ((TextView) findViewById(R.id.isoBtn)).setText("-");
        ((TextView) findViewById(R.id.shutterSpeedBtn)).setText("-");
        ((TextView) findViewById(R.id.focusBtn)).setText("-");

        int numEvSteps = mSelectedCamera.exposureCompRangeMax - mSelectedCamera.exposureCompRangeMin;

        mBinding.exposureSeekBar.setMax(numEvSteps);
        mBinding.exposureSeekBar.setProgress(numEvSteps / 2);
        mBinding.shadowsSeekBar.setProgress(50);

        setupFpsSelection();

        // Setup preset toggles
        findViewById(R.id.preset4K).setOnClickListener(v -> onVideoPresetSelected(v));
        findViewById(R.id.preset1080P).setOnClickListener(v -> onVideoPresetSelected(v));

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
        updateCameraSettingsUi();
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
                false,
                mSettings.rawMode == SettingsViewModel.RawMode.RAW12,
                mSettings.rawMode == SettingsViewModel.RawMode.RAW16,
                mSettings.cameraStartupSettings);

        // Update orientation in case we've switched front/back cameras
        NativeCameraBuffer.ScreenOrientation orientation = mSensorEventManager.getOrientation();
        if(orientation != null)
            onOrientationChanged(orientation);

        if(mSettings.useDualExposure && mCaptureMode != CaptureMode.RAW_VIDEO) {
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

        mBinding.cameraFrame
                .animate()
                .cancel();

        mBinding.cameraFrame.setAlpha(1.0f);

        // Start overlay if configured
        setExposureOverlay(mSettings.exposureOverlay);

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
            || mSettings.cameraStartupSettings.useUserExposureSettings
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
    public void onCameraStarted() {
        Log.d(TAG, "onCameraStarted()");

        runOnUiThread(() ->
        {
            // Set up startup stuff
            mBinding.switchCameraBtn.setEnabled(true);

            setCaptureMode(mSettings.captureMode, true);
            setSaveRaw(mSettings.saveDng);
            setHdr(mSettings.hdr);
            setOIS(mSettings.cameraStartupSettings.ois);
            setAfLock(false, true);
            setAeLock(mSettings.cameraStartupSettings.useUserExposureSettings);

            updatePreviewSettings();

            mBinding.manualControlsFrame.setVisibility(View.VISIBLE);
            alignManualControlView(mSensorEventManager.getOrientation(), false);
        });
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

            // Reset shadows and exposure slider
            mBinding.exposureSeekBar.setProgress(mBinding.exposureSeekBar.getMax() / 2);
            mBinding.shadowsSeekBar.setProgress(mBinding.shadowsSeekBar.getMax() / 2);

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

        // Reset shadows and exposure slider
        mBinding.exposureSeekBar.setProgress(mBinding.exposureSeekBar.getMax() / 2);
        mBinding.shadowsSeekBar.setProgress(mBinding.shadowsSeekBar.getMax() / 2);

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

    private void alignCameraSettingsView(NativeCameraBuffer.ScreenOrientation orientation, boolean animate) {
        if(mBinding.cameraSettings.getVisibility() != View.VISIBLE)
            return;

        final int rotation;
        final int translationX;
        final int translationY;

        final int marginTop = getResources().getDimensionPixelSize(R.dimen.camera_settings_margin_top);

        // Update position of manual controls
        if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {
            rotation = 180;
            translationX = 0;
            translationY = (mBinding.cameraFrame.getHeight() - mBinding.cameraSettings.getHeight()) / 2 - marginTop;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
            rotation = 90;
            translationX = mBinding.cameraFrame.getWidth()/2 - mBinding.cameraSettings.getHeight()/2 - marginTop;
            translationY = 0;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE) {
            rotation = -90;
            translationX = -mBinding.cameraFrame.getWidth()/2 + mBinding.cameraSettings.getHeight()/2 + marginTop;
            translationY = 0;
        }
        else {
            // Portrait
            rotation = 0;
            translationX = 0;
            translationY = -mBinding.cameraSettings.getTop() + mBinding.cameraFrame.getTop() + marginTop;
        }

        if(animate) {
            mBinding.cameraSettings.animate()
                    .rotation(rotation)
                    .translationX(translationX)
                    .translationY(translationY)
                    .setDuration(250)
                    .start();
        }
        else {
            mBinding.cameraSettings.setRotation(rotation);
            mBinding.cameraSettings.setTranslationX(translationX);
            mBinding.cameraSettings.setTranslationY(translationY);
            mBinding.cameraSettings.setVisibility(View.VISIBLE);
        }
    }

    private void alignManualControlView(NativeCameraBuffer.ScreenOrientation orientation, boolean animate) {
        if(mBinding.manualControlsFrame.getVisibility() != View.VISIBLE)
            return;

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

        if(animate) {
            mBinding.manualControlsFrame.animate()
                    .rotation(rotation)
                    .translationX(translationX)
                    .translationY(translationY)
                    .setDuration(250)
                    .start();
        }
        else {
            mBinding.manualControlsFrame.setRotation(rotation);
            mBinding.manualControlsFrame.setTranslationX(translationX);
            mBinding.manualControlsFrame.setTranslationY(translationY);
        }
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

        alignManualControlView(orientation, true);
        alignCameraSettingsView(orientation, true);
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

        String focusDistanceMetersText;

        if(focusDistance > 0) {
            float focusDistanceMeters = 1.0f / focusDistance;
            focusDistanceMetersText = String.format(Locale.US, "%.2f M", focusDistanceMeters);
        }
        else {
            focusDistanceMetersText = "-";
        }

        ((TextView) findViewById(R.id.focusBtn)).setText(focusDistanceMetersText);

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

    void enumerateAudioInputs() {
        AudioManager audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        AudioDeviceInfo[] deviceInfoList = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
        RadioGroup audioInputsLayout = mBinding.cameraSettings.findViewById(R.id.audioInputGroup);

        audioInputsLayout.removeAllViews();

        // Add default device
        RadioButton internalMicBtn = new RadioButton(this);

        internalMicBtn.setId(View.generateViewId());
        internalMicBtn.setText(getString(R.string.internal_mic));
        internalMicBtn.setTextColor(getColor(R.color.white));
        internalMicBtn.setTag(-1);
        internalMicBtn.setChecked(true);
        internalMicBtn.setOnClickListener(v -> onAudioInputChanged(v));

        internalMicBtn.setLayoutParams(
                new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        audioInputsLayout.addView(internalMicBtn);

        for(int i = 0; i < deviceInfoList.length; i++) {
            AudioDeviceInfo deviceInfo = deviceInfoList[i];

            // Pick a few types that'll probably work
            if( deviceInfo.getType() == AudioDeviceInfo.TYPE_USB_HEADSET    ||
                deviceInfo.getType() == AudioDeviceInfo.TYPE_WIRED_HEADSET  ||
                deviceInfo.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                deviceInfo.getType() == AudioDeviceInfo.TYPE_USB_ACCESSORY)
            {
                RadioButton audioDeviceBtn = new RadioButton(this);

                audioDeviceBtn.setId(View.generateViewId());
                audioDeviceBtn.setText(deviceInfo.getProductName());
                audioDeviceBtn.setTextColor(getColor(R.color.white));
                audioDeviceBtn.setTag(deviceInfo.getId());
                audioDeviceBtn.setOnClickListener(v -> onAudioInputChanged(v));

                audioDeviceBtn.setLayoutParams(
                        new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

                audioInputsLayout.addView(audioDeviceBtn);
            }
        }
    }

    private void onAudioInputChanged(View v) {
        Object deviceId = v.getTag();
        if(deviceId != null && deviceId instanceof Integer) {
            mAudioInputId = (Integer) deviceId;
        }
    }
}
