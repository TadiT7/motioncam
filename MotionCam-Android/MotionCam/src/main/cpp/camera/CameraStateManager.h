#ifndef MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
#define MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H

#include <memory>
#include <json11/json11.hpp>
#include <motioncam/Util.h>

#include "CameraSessionState.h"

struct ACameraCaptureSession;

namespace motioncam {
    struct CameraCaptureSessionContext;
    struct CameraDescription;

    enum class Action : int {
        NONE,
        REQUEST_USER_TOUCH_FOCUS,
        REQUEST_USER_MANUAL_FOCUS,
        REQUEST_AUTO_FOCUS
    };

    enum class State : int {
        AUTO_FOCUS_WAIT = 0,
        AUTO_FOCUS_ACTIVE,
        USER_FOCUS_WAIT,
        USER_FOCUS_ACTIVE,
        PAUSED
    };

    struct CameraStartupSettings {
        bool useUserExposureSettings;
        int32_t iso;
        int64_t exposureTime;
        int32_t frameRate;
        bool ois;
        bool focusForVideo;

        CameraStartupSettings() :
            useUserExposureSettings(false),
            iso(0),
            exposureTime(0),
            frameRate(-1),
            ois(true),
            focusForVideo(false)
        {
        }

        CameraStartupSettings(const json11::Json& json) {
            useUserExposureSettings = util::GetOptionalSetting(json, "useUserExposureSettings", false);
            iso = util::GetOptionalSetting(json, "iso", 0);
            exposureTime = util::GetOptionalSetting(json, "exposureTime", 0.0);
            frameRate = util::GetOptionalSetting(json, "frameRate", -1);
            ois = util::GetOptionalSetting(json, "ois", false);
            focusForVideo = util::GetOptionalSetting(json, "focusForVideo", false);
        }
    };

    class CameraStateManager {
    public:
        CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription);

        void start(const CameraStartupSettings& startupSettings);

        void requestPause();
        void requestResume();

        void requestUpdatePreview(std::vector<float>&& tonemapPts);
        void requestUserFocus(float x, float y);
        void requestAutoFocus();
        void requestManualFocus(float distance);
        void requestExposureCompensation(int exposureCompensation);
        void requestFrameRate(int frameRate);
        void requestAwbLock(bool lock);
        void requestAELock(bool lock);
        void requestOis(bool ois);
        void requestAperture(float aperture);
        void requestTorch(bool enable);

        void requestUserExposure(int32_t iso, int64_t exposureTime);
        void requestExposureMode(CameraMode mode);

        void requestFocusForVideo(bool focusForVideo);

        void onCameraCaptureSequenceCompleted(const int sequenceId);
        void onCameraSessionStateChanged(const CameraCaptureSessionState state);

        bool activate();

    private:
        void updateCaptureRequestFocus();
        void updateCaptureRequestExposure();

    private:
        const CameraCaptureSessionContext& mSessionContext;
        const CameraDescription& mCameraDescription;

        State mState;
        CameraMode mCameraExposureMode;
        CameraMode mCameraFocusMode;
        int mExposureCompensation;
        int mFrameRate;
        bool mAwbLock;
        bool mAELock;
        bool mOis;
        bool mFocusForVideo;
        bool mTorch;

        float mRequestedFocusX;
        float mRequestedFocusY;
        float mFocusDistance;
        float mRequestedAperture;

        int32_t mUserIso;
        int64_t mUserExposureTime;

        std::vector<float> mTonemapPts;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
