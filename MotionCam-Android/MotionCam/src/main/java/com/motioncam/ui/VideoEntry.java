package com.motioncam.ui;

import android.net.Uri;

import com.motioncam.processor.ContainerMetadata;

import java.util.HashSet;
import java.util.Set;

public class VideoEntry implements Cloneable {
    private final String name;
    private Set<Uri> videoUris;
    private ContainerMetadata metadata;
    private Uri audioUri;
    private boolean alreadyExported;
    private long createdAt;

    public VideoEntry(String name)
    {
        this.name = name;
        this.videoUris = new HashSet<>();
    }

    public void setMetadata(ContainerMetadata metadata) {
        this.metadata = metadata;
    }

    public ContainerMetadata getMetadata() {
        return this.metadata;
    }

    public void addVideoUri(Uri videoUri) {
        this.videoUris.add(videoUri);
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

    public Set<Uri> getVideoUris() {
        return videoUris;
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

    public boolean isVideoSegment(Uri uri) {
        for(Uri otherUri : this.videoUris) {
            if(uri.equals(otherUri))
                return true;
        }

        return false;
    }

    public float getFrameRate() {
        if(this.metadata == null)
            return 0;

        return this.metadata.frameRate;
    }

    public int getNumFrames() {
        if(this.metadata == null)
            return 0;

        return this.metadata.numFrames;
    }

    public int getNumParts() {
        if(this.metadata == null)
            return 0;

        return this.metadata.numSegments;
    }

    @Override
    public boolean equals(Object obj) {
        if (obj == this) {
            return true;
        }

        if (!(obj instanceof VideoEntry)) {
            return false;
        }

        VideoEntry entry = (VideoEntry) obj;

        return entry.getName().equals(this.name);
    }

    @Override
    public VideoEntry clone() {
        VideoEntry entry = new VideoEntry(this.name);

        entry.videoUris = this.videoUris;
        entry.metadata = this.metadata;
        entry.audioUri = this.audioUri;
        entry.alreadyExported = this.alreadyExported;
        entry.createdAt = this.createdAt;

        return entry;
    }
}
