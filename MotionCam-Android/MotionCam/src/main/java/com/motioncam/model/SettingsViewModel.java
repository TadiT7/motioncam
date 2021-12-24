package com.motioncam.model;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class SettingsViewModel extends ViewModel {
    public static final String CAMERA_SHARED_PREFS          = "camera_prefs";

    public static final int MINIMUM_MEMORY_USE_MB           = 512;
    public static final int MAXIMUM_MEMORY_USE_MB           = 3072;

    public static final String PREFS_KEY_MEMORY_USE_MBYTES          = "memory_use_megabytes";
    public static final String PREFS_KEY_RAW_VIDEO_MEMORY_USE_MBYTES = "raw_video_memory_use_megabytes";
    public static final String PREFS_KEY_JPEG_QUALITY               = "jpeg_quality";
    public static final String PREFS_KEY_SAVE_DNG                   = "save_dng";
    public static final String PREFS_KEY_CAMERA_PREVIEW_QUALITY     = "camera_preview_quality";
    public static final String PREFS_KEY_AUTO_NIGHT_MODE            = "auto_night_mode";
    public static final String PREFS_KEY_DUAL_EXPOSURE_CONTROLS     = "dual_exposure_controls";
    public static final String PREFS_KEY_CAPTURE_MODE               = "capture_mode";
    public static final String PREFS_KEY_RAW_VIDEO_TO_DNG           = "raw_video_to_dng";
    public static final String PREFS_KEY_SPLIT_RAW_VIDEO_WRITES     = "split_raw_video_writes";

    public static final String PREFS_KEY_IGNORE_CAMERA_IDS                  = "ignore_camera_ids";
    public static final String PREFS_KEY_UI_PREVIEW_CONTRAST                = "ui_preview_contrast_amount";
    public static final String PREFS_KEY_UI_PREVIEW_COLOUR                  = "ui_preview_colour";
    public static final String PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET      = "ui_preview_temperature_offset";
    public static final String PREFS_KEY_UI_PREVIEW_TINT_OFFSET             = "ui_preview_tint_offset";
    public static final String PREFS_KEY_UI_PREVIEW_SHARPNESS               = "ui_preview_sharpness";
    public static final String PREFS_KEY_UI_PREVIEW_DETAIL                  = "ui_preview_detail";
    public static final String PREFS_KEY_UI_CAPTURE_MODE                    = "ui_capture_mode";
    public static final String PREFS_KEY_UI_SAVE_RAW                        = "ui_save_raw";
    public static final String PREFS_KEY_UI_EXPOSURE_OVERLAY                = "ui_exposure_overlay";
    public static final String PREFS_KEY_UI_HDR                             = "ui_hdr";
    public static final String PREFS_KEY_UI_WIDTH_VIDEO_CROP                = "ui_width_video_crop";
    public static final String PREFS_KEY_UI_HEIGHT_VIDEO_CROP               = "ui_height_video_crop";
    public static final String PREFS_KEY_UI_FRAME_RATE                      = "ui_frame_rate";
    public static final String PREFS_KEY_UI_VIDEO_BIN                       = "ui_video_bin";
    public static final String PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI          = "raw_video_temp_output_uri";
    public static final String PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI_2        = "raw_video_temp_output_uri_2";
    public static final String PREFS_KEY_RAW_VIDEO_EXPORT_URI               = "raw_video_output_uri";
    public static final String PREFS_KEY_RAW_VIDEO_DELETE_AFTER_EXPORT      = "raw_video_delete_after_export";
    public static final String PREFS_KEY_RAW_VIDEO_MERGE_FRAMES             = "raw_video_merge_frames";

    public enum RawMode {
        RAW10,
        RAW12,
        RAW16
    }

    final public MutableLiveData<Integer> memoryUseMb = new MutableLiveData<>();
    final public MutableLiveData<Integer> rawVideoMemoryUseMb = new MutableLiveData<>();
    final public MutableLiveData<Integer> cameraPreviewQuality = new MutableLiveData<>();
    final public MutableLiveData<Boolean> raw10 = new MutableLiveData<>();
    final public MutableLiveData<Boolean> raw12 = new MutableLiveData<>();
    final public MutableLiveData<Boolean> raw16 = new MutableLiveData<>();
    final public MutableLiveData<Integer> jpegQuality = new MutableLiveData<>();
    final public MutableLiveData<Boolean> autoNightMode = new MutableLiveData<>();
    final public MutableLiveData<Boolean> dualExposureControls = new MutableLiveData<>();
    final public MutableLiveData<Boolean> rawVideoToDng = new MutableLiveData<>();
    final public MutableLiveData<String> rawVideoTempStorageFolder = new MutableLiveData<>();
    final public MutableLiveData<String> rawVideoTempStorageFolder2 = new MutableLiveData<>();
    final public MutableLiveData<Boolean> splitRawVideoStorage = new MutableLiveData<>();

    public void load(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        memoryUseMb.setValue(prefs.getInt(PREFS_KEY_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB) - MINIMUM_MEMORY_USE_MB);
        rawVideoMemoryUseMb.setValue(prefs.getInt(PREFS_KEY_RAW_VIDEO_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB*2) - MINIMUM_MEMORY_USE_MB);
        cameraPreviewQuality.setValue(prefs.getInt(PREFS_KEY_CAMERA_PREVIEW_QUALITY, 0));
        jpegQuality.setValue(prefs.getInt(PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY));
        autoNightMode.setValue(prefs.getBoolean(PREFS_KEY_AUTO_NIGHT_MODE, true));
        dualExposureControls.setValue(prefs.getBoolean(PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false));
        rawVideoToDng.setValue(prefs.getBoolean(PREFS_KEY_RAW_VIDEO_TO_DNG, true));
        rawVideoTempStorageFolder.setValue(prefs.getString(PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, null));
        rawVideoTempStorageFolder2.setValue(prefs.getString(PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI_2, null));
        splitRawVideoStorage.setValue(prefs.getBoolean(PREFS_KEY_SPLIT_RAW_VIDEO_WRITES, false));

        // Capture mode
        String rawModeStr = prefs.getString(PREFS_KEY_CAPTURE_MODE, RawMode.RAW10.name());
        RawMode rawMode = RawMode.valueOf(rawModeStr);

        raw10.setValue(false);
        raw12.setValue(false);
        raw16.setValue(false);

        if(rawMode == RawMode.RAW10)
            raw10.setValue(true);
        else if(rawMode == RawMode.RAW12)
            raw12.setValue(true);
        else if(rawMode == RawMode.RAW16)
            raw16.setValue(true);
    }

    private <T> T getSetting(LiveData<T> setting, T defaultValue) {
        return setting.getValue() == null ? defaultValue : setting.getValue();
    }

    public void save(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = prefs.edit();

        editor.putInt(PREFS_KEY_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB + getSetting(memoryUseMb, MINIMUM_MEMORY_USE_MB));
        editor.putInt(PREFS_KEY_RAW_VIDEO_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB + getSetting(rawVideoMemoryUseMb, MINIMUM_MEMORY_USE_MB));
        editor.putInt(PREFS_KEY_CAMERA_PREVIEW_QUALITY, getSetting(cameraPreviewQuality, 0));
        editor.putInt(PREFS_KEY_JPEG_QUALITY, getSetting(jpegQuality, 95));
        editor.putBoolean(PREFS_KEY_AUTO_NIGHT_MODE, getSetting(autoNightMode, true));
        editor.putBoolean(PREFS_KEY_DUAL_EXPOSURE_CONTROLS, getSetting(dualExposureControls, false));
        editor.putBoolean(PREFS_KEY_RAW_VIDEO_TO_DNG, getSetting(rawVideoToDng, true));
        editor.putString(PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, getSetting(rawVideoTempStorageFolder, null));
        editor.putString(PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI_2, getSetting(rawVideoTempStorageFolder2, null));
        editor.putBoolean(PREFS_KEY_SPLIT_RAW_VIDEO_WRITES, getSetting(splitRawVideoStorage, false));

        // Capture mode
        RawMode rawMode = RawMode.RAW10;

        if(raw10.getValue())
            rawMode = RawMode.RAW10;
        else if(raw12.getValue())
            rawMode = RawMode.RAW12;
        else if(raw16.getValue())
            rawMode = RawMode.RAW16;

        editor.putString(PREFS_KEY_CAPTURE_MODE, rawMode.name());

        editor.apply();
    }
}
