package com.motioncam.worker;

public class State {
    public static final String PROGRESS_MODE_KEY        = "mode";
    public static final String PROGRESS_STATE_KEY       = "state";
    public static final String PROGRESS_PROGRESS_KEY    = "progress";
    public static final String PROGRESS_DELETED         = "isDeleted";
    public static final String PROGRESS_ERROR_KEY       = "errorMessage";
    public static final String PROGRESS_URI_KEY         = "uri";
    public static final String PROGRESS_NAME_KEY        = "name";
    public static final String PROGRESS_IMAGE_PATH      = "imagePath";
    public static final String PROGRESS_PREVIEW_PATH    = "previewPath";

    public static final int STATE_PREVIEW_CREATED   = 1000;
    public static final int STATE_PROCESSING        = 1001;
    public static final int STATE_COMPLETED         = 1002;
    public static final int STATE_ERROR             = 1003;
}
