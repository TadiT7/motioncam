package com.motioncam.ui;

import android.net.Uri;

public class VideoEntry {
    private final String name;
    private Uri videoUri;
    private Uri audioUri;
    private boolean alreadyExported;
    private long createdAt;

    public VideoEntry(String name)
    {
        this.name = name;
    }

    public void setVideoUri(Uri videoUri) {
        this.videoUri = videoUri;
    }

    public void setAudioUri(Uri audioUri) {
        this.audioUri = audioUri;
    }

    public void setAlreadyExported(boolean alreadyExported) {
        this.alreadyExported = alreadyExported;
    }

    public void setCreatedAt(long createdAt) {
        this.createdAt = createdAt;
    }

    public String getName()
    {
        return name;
    }

    public Uri getVideoUri() {
        return videoUri;
    }

    public Uri getAudioUri() {
        return audioUri;
    }

    public boolean isAlreadyExported() {
        return alreadyExported;
    }

    public long getCreatedAt() {
        return createdAt;
    }
}
