package com.motioncam.camera;

public class CameraStartupSettings {
    public boolean useUserExposureSettings;
    public int iso;
    public long exposureTime;
    public int frameRate;
    public boolean ois;
    public boolean focusForVideo;

    public CameraStartupSettings(
        boolean useUserExposureSettings,
        int iso,
        long exposureTime,
        int frameRate,
        boolean ois,
        boolean focusForVideo)
    {
        this.useUserExposureSettings = useUserExposureSettings;
        this.iso = iso;
        this.exposureTime = exposureTime;
        this.frameRate = frameRate;
        this.ois = ois;
        this.focusForVideo = focusForVideo;
    }

    @Override
    public String toString() {
        return "CameraStartupSettings{" +
                "useUserExposureSettings=" + useUserExposureSettings +
                ", iso=" + iso +
                ", exposureTime=" + exposureTime +
                ", frameRate=" + frameRate +
                ", ois=" + ois +
                ", focusForVideo=" + focusForVideo +
                '}';
    }
}
