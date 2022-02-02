package com.motioncam;

import android.content.SharedPreferences;
import android.net.Uri;

import com.motioncam.camera.CameraStartupSettings;
import com.motioncam.model.CameraProfile;
import com.motioncam.model.SettingsViewModel;

public class Settings {
    public enum CaptureMode {
        NIGHT,
        ZSL,
        BURST,
        RAW_VIDEO
    }

    static public class CameraSettings {
        float contrast;
        float saturation;
        float temperatureOffset;
        float tintOffset;
        float sharpness;
        float detail;

        static private String CameraKey(String key, String cameraId) {
            return key + "_" + cameraId;
        }

        public void load(SharedPreferences prefs, String cameraId) {
            this.contrast = prefs.getFloat(
                    CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, cameraId), CameraProfile.DEFAULT_CONTRAST / 100.0f);

            this.saturation =
                    prefs.getFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, cameraId), 1.00f);

            this.sharpness = prefs.getFloat(
                    CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_SHARPNESS, cameraId), 1.0f + (CameraProfile.DEFAULT_SHARPNESS / 50.0f));

            this.detail = prefs.getFloat(
                    CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_DETAIL, cameraId), 1.0f + (CameraProfile.DEFAULT_DETAIL / 50.0f));

            this.temperatureOffset =
                    prefs.getFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, cameraId), 0);

            this.tintOffset =
                    prefs.getFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, cameraId), 0);
        }

        public void save(SharedPreferences prefs, String cameraId) {
            prefs.edit()
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, cameraId), this.contrast)
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, cameraId), this.saturation)
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_SHARPNESS, cameraId), this.sharpness)
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_DETAIL, cameraId), this.detail)
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, cameraId), this.temperatureOffset)
                    .putFloat(CameraKey(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, cameraId), this.tintOffset)
                    .apply();
        }

        @Override
        public String toString() {
            return "CameraSettings{" +
                    "contrast=" + contrast +
                    ", saturation=" + saturation +
                    ", temperatureOffset=" + temperatureOffset +
                    ", tintOffset=" + tintOffset +
                    ", sharpness=" + sharpness +
                    ", detail=" + detail +
                    '}';
        }
    }

    boolean useDualExposure;
    boolean exposureOverlay;
    boolean rawVideoToDng;
    boolean saveDng;
    boolean autoNightMode;
    boolean hdr;
    CameraStartupSettings cameraStartupSettings;
    int jpegQuality;
    long memoryUseBytes;
    long rawVideoMemoryUseBytes;
    CaptureMode captureMode;
    SettingsViewModel.RawMode rawMode;
    int cameraPreviewQuality;
    int widthVideoCrop;
    int heightVideoCrop;
    boolean videoBin;
    Uri rawVideoExportUri;
    boolean useSecondaryRawVideoStorage;
    Uri rawVideoRecordingTempUri;
    Uri rawVideoRecordingTempUri2;
    boolean enableRawVideoCompression;
    int numRawVideoCompressionThreads;

    private CameraStartupSettings loadCameraStartupSettings(SharedPreferences prefs) {
        boolean enableUserExposure = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USE_USER_EXPOSURE, false);
        int userIso = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USER_ISO, 0);
        long userExposureTime = prefs.getLong(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USER_EXPOSURE_TIME, 0);
        int userFrameRate = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_FRAME_RATE, -1);
        boolean ois = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_OIS, true);

        return new CameraStartupSettings(enableUserExposure, userIso, userExposureTime, userFrameRate, ois, false);
    }

    private void saveCameraStartupSettings(SharedPreferences prefs, CameraStartupSettings cameraStartupSettings) {
        prefs.edit()
            .putBoolean(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USE_USER_EXPOSURE, cameraStartupSettings.useUserExposureSettings)
            .putInt(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USER_ISO, cameraStartupSettings.iso)
            .putLong(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_USER_EXPOSURE_TIME, cameraStartupSettings.exposureTime)
            .putInt(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_FRAME_RATE, cameraStartupSettings.frameRate)
            .putBoolean(SettingsViewModel.PREFS_KEY_UI_CAMERA_STARTUP_OIS, cameraStartupSettings.ois)
            .apply();
    }

    void load(SharedPreferences prefs) {
        this.jpegQuality = prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY);
        this.saveDng = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, false);
        this.exposureOverlay = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_EXPOSURE_OVERLAY, false);
        this.autoNightMode = prefs.getBoolean(SettingsViewModel.PREFS_KEY_AUTO_NIGHT_MODE, true);
        this.hdr = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, true);
        this.widthVideoCrop = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_WIDTH_VIDEO_CROP, 0);
        this.heightVideoCrop = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_HEIGHT_VIDEO_CROP, 0);
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

        // Get temp recording uris
        this.useSecondaryRawVideoStorage = prefs.getBoolean(SettingsViewModel.PREFS_KEY_SPLIT_RAW_VIDEO_WRITES, false);

        String rawVideoRecordingTempUri = prefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, null);
        if (rawVideoRecordingTempUri != null && !rawVideoRecordingTempUri.isEmpty())
            this.rawVideoRecordingTempUri = Uri.parse(rawVideoRecordingTempUri);

        String rawVideoRecordingTempUri2 = prefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI_2, null);
        if (rawVideoRecordingTempUri2 != null && !rawVideoRecordingTempUri2.isEmpty())
            this.rawVideoRecordingTempUri2 = Uri.parse(rawVideoRecordingTempUri2);

        String exportUriString = prefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, null);
        if (exportUriString != null && !exportUriString.isEmpty())
            this.rawVideoExportUri = Uri.parse(exportUriString);

        this.enableRawVideoCompression = prefs.getBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_COMPRESSION, true);
        this.numRawVideoCompressionThreads = prefs.getInt(SettingsViewModel.PREFS_KEY_RAW_VIDEO_COMPRESSION_THREADS, 2);
        this.cameraStartupSettings = loadCameraStartupSettings(prefs);
    }

    void save(SharedPreferences prefs) {
        saveCameraStartupSettings(prefs, this.cameraStartupSettings);

        if(this.captureMode == null)
            this.captureMode = CaptureMode.ZSL;

        prefs.edit()
                .putBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, this.saveDng)
                .putBoolean(SettingsViewModel.PREFS_KEY_UI_EXPOSURE_OVERLAY, this.exposureOverlay)
                .putBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, this.hdr)
                .putString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, this.captureMode.name())
                .putInt(SettingsViewModel.PREFS_KEY_UI_WIDTH_VIDEO_CROP, this.widthVideoCrop)
                .putInt(SettingsViewModel.PREFS_KEY_UI_HEIGHT_VIDEO_CROP, this.heightVideoCrop)
                .putBoolean(SettingsViewModel.PREFS_KEY_UI_VIDEO_BIN, this.videoBin)
                .apply();
    }

    @Override
    public String toString() {
        return "Settings{" +
                "useDualExposure=" + useDualExposure +
                ", exposureOverlay=" + exposureOverlay +
                ", rawVideoToDng=" + rawVideoToDng +
                ", saveDng=" + saveDng +
                ", autoNightMode=" + autoNightMode +
                ", hdr=" + hdr +
                ", cameraStartupSettings=" + cameraStartupSettings +
                ", jpegQuality=" + jpegQuality +
                ", memoryUseBytes=" + memoryUseBytes +
                ", rawVideoMemoryUseBytes=" + rawVideoMemoryUseBytes +
                ", captureMode=" + captureMode +
                ", rawMode=" + rawMode +
                ", cameraPreviewQuality=" + cameraPreviewQuality +
                ", widthVideoCrop=" + widthVideoCrop +
                ", heightVideoCrop=" + heightVideoCrop +
                ", videoBin=" + videoBin +
                ", rawVideoExportUri=" + rawVideoExportUri +
                ", useSecondaryRawVideoStorage=" + useSecondaryRawVideoStorage +
                ", rawVideoRecordingTempUri=" + rawVideoRecordingTempUri +
                ", rawVideoRecordingTempUri2=" + rawVideoRecordingTempUri2 +
                ", enableRawVideoCompression=" + enableRawVideoCompression +
                ", numRawVideoCompressionThreads=" + numRawVideoCompressionThreads +
                '}';
    }
}
