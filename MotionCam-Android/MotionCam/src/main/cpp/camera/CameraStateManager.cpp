#include "CameraStateManager.h"
#include "CameraSessionContext.h"
#include "Logger.h"

#include <camera/NdkCameraDevice.h>

namespace motioncam {

    static std::string ToString(State state) {
        switch(state) {
            case State::AUTO_FOCUS_WAIT:
                return "AUTO_FOCUS_WAIT";

            case State::AUTO_FOCUS_ACTIVE:
                return "AUTO_FOCUS_ACTIVE";

            case State::USER_FOCUS_WAIT:
                return "USER_FOCUS_WAIT";

            case State::USER_FOCUS_ACTIVE:
                return "USER_FOCUS_ACTIVE";

            case State::PAUSED:
                return "PAUSED";

            default:
                return "UNKNOWN";
        }
    }

    static std::string ToString(Action action) {
        switch(action) {
            case Action::NONE:
                return "NONE";

            case Action::REQUEST_USER_TOUCH_FOCUS:
                return "REQUEST_USER_TOUCH_FOCUS";

            case Action::REQUEST_AUTO_FOCUS:
                return "REQUEST_AUTO_FOCUS";

            default:
                return "UNKNOWN";
        }
    }

    CameraStateManager::CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription) :
            mSessionContext(context),
            mCameraDescription(cameraDescription),
            mCaptureSessionState(CameraCaptureSessionState::ACTIVE),
            mCameraExposureMode(CameraMode::AUTO),
            mCameraFocusMode(CameraMode::AUTO),
            mPendingSequenceId(-1),
            mState(State::PAUSED),
            mRequestedAction(Action::NONE),
            mExposureCompensation(0),
            mRequestedFocusX(0),
            mRequestedFocusY(0),
            mUserIso(0),
            mUserExposureTime(0),
            mFrameRate(-1),
            mAwbLock(false),
            mAeLock(false),
            mOis(true),
            mFocusDistance(-1),
            mFocusForVideo(false),
            mRequestedAperture(-1)
    {
    }

    void CameraStateManager::start(const CameraStartupSettings& startupSettings) {
        mFocusForVideo = startupSettings.focusForVideo;
        mFrameRate = startupSettings.frameRate;
        mUserIso = startupSettings.iso;
        mUserExposureTime = startupSettings.exposureTime;
        mOis = startupSettings.ois;
        mCameraExposureMode = startupSettings.useUserExposureSettings ? CameraMode::MANUAL : CameraMode::AUTO;
        mRequestedAperture = -1;

        setAutoFocus();
    }

    void CameraStateManager::setState(State state, int sequenceId) {
        LOGD("setState(%s -> %s, sequenceId: %d)", ToString(mState).c_str(), ToString(state).c_str(), sequenceId);
        mState = state;
        mPendingSequenceId = sequenceId;
    }

    void CameraStateManager::setNextAction(Action action) {
        LOGD("setNextAction(%s -> %s)", ToString(mRequestedAction).c_str(), ToString(action).c_str());
        mRequestedAction = action;
    }

    void CameraStateManager::requestUserExposure(int32_t iso, int64_t exposureTime) {
        mUserIso = iso;
        mUserExposureTime = exposureTime;
        mCameraExposureMode = CameraMode::MANUAL;
        mAeLock = false;

        updateCamera();
    }

    void CameraStateManager::requestExposureMode(CameraMode mode) {
        mCameraExposureMode = mode;
        mAeLock = false;
        mFocusDistance = -1;

        updateCamera();
    }

    void CameraStateManager::requestPause() {
        LOGD("requestPause()");

        mFocusDistance = -1;
        mAeLock = false;

        setState(State::PAUSED, -1);
        ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
    }

    void CameraStateManager::requestResume() {
        LOGD("requestResume()");

        if( mState == State::PAUSED ) {
            setAutoFocus();
        }
    }

    void CameraStateManager::requestAperture(float aperture) {
        LOGD("requestAperture(%.2f)", aperture);
        mRequestedAperture = aperture;

        updateCamera();
    }

    void CameraStateManager::requestManualFocus(float distance) {
        LOGD("requestManualFocus(%.2f)", distance);

        mFocusDistance = distance;
        mCameraFocusMode = CameraMode::MANUAL;

        updateCamera();
    }

    void CameraStateManager::requestUserFocus(float x, float y) {
        LOGD("requestUserFocus(%.2f, %.2f)", x, y);

        mRequestedFocusX = x;
        mRequestedFocusY = y;
        mFocusDistance = -1;
        mCameraFocusMode = CameraMode::MANUAL;

        updateCamera();
    }

    void CameraStateManager::requestAutoFocus() {
        LOGD("requestAutoFocus()");

        mRequestedFocusX = 0.5f;
        mRequestedFocusY = 0.5f;
        mFocusDistance = -1;
        mCameraFocusMode = CameraMode::AUTO;

        updateCamera();
    }

    void CameraStateManager::requestExposureCompensation(int exposureCompensation) {
        if(mExposureCompensation == exposureCompensation)
            return;

        LOGD("exposureCompensation(%d)", exposureCompensation);

        mExposureCompensation = exposureCompensation;

        updateCamera();
    }

    void CameraStateManager::requestAwbLock(bool lock) {
        mAwbLock = lock;

        LOGD("requestAwbLock(%d)", lock);

        updateCamera();
    }

    void CameraStateManager::requestAeLock(bool lock) {
        mAeLock = lock;

        LOGD("requestAeLock(%d)", lock);

        updateCamera();
    }

    void CameraStateManager::requestOis(bool ois) {
        mOis = ois;

        LOGD("requestOis(%d)", ois);

        updateCamera();
    }

    void CameraStateManager::requestFrameRate(int frameRate) {
        if(mFrameRate == frameRate)
            return;

        LOGD("requestFrameRate(%d)", frameRate);

        mFrameRate = frameRate;

        // Use focus for video when setting fixed frame rate
        if(mFrameRate < 0) {
            mFocusForVideo = false;
        }
        else {
            mFocusForVideo = true;
        }

        updateCamera();
    }

    void CameraStateManager::requestFocusForVideo(bool focusForVideo) {
        mFocusForVideo = focusForVideo;

        LOGD("setFocusForVideo(%d)", focusForVideo);

        updateCamera();
    }

    void CameraStateManager::updateCamera() {
        if(mCameraFocusMode == CameraMode::AUTO) {
            if( mState == State::USER_FOCUS_WAIT ||
                mState == State::AUTO_FOCUS_WAIT)
            {
                setNextAction(Action::REQUEST_AUTO_FOCUS);
            }
            else {
                setAutoFocus();
            }
        }
        else {
            if( mState == State::USER_FOCUS_WAIT ||
                mState == State::AUTO_FOCUS_WAIT)
            {
                if(mFocusDistance < 0)
                    setNextAction(Action::REQUEST_USER_TOUCH_FOCUS);
                else
                    setNextAction(Action::REQUEST_USER_MANUAL_FOCUS);
            }
            else {
                setUserFocus();
            }
        }
    }

    void CameraStateManager::updateCaptureRequestExposure() {
        uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
        uint8_t controlMode = ACAMERA_CONTROL_MODE_AUTO;

        if(mCameraExposureMode == CameraMode::AUTO) {
            uint8_t aeLock = mAeLock ? ACAMERA_CONTROL_AE_LOCK_ON : ACAMERA_CONTROL_AE_LOCK_OFF;

            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_LOCK, 1, &aeLock);
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);
        }
        else if(mCameraExposureMode == CameraMode::MANUAL) {
            aeMode = ACAMERA_CONTROL_AE_MODE_OFF;

            int frameRate = mFrameRate < 0 ? 30 : mFrameRate;
            auto sensorFrameDuration = static_cast<int64_t>((1.0 / frameRate) * 1e9);

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 1, &mUserIso);
            ACaptureRequest_setEntry_i64(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &mUserExposureTime);
            ACaptureRequest_setEntry_i64(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_FRAME_DURATION, 1, &sensorFrameDuration);

            if(mRequestedAperture > 0)
                ACaptureRequest_setEntry_float(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_LENS_APERTURE, 1, &mRequestedAperture);

            LOGD("userIso=%d, userExposure=%.4f", mUserIso, mUserExposureTime/1.0e9f);
        }

        uint8_t awbLock = mAwbLock ? ACAMERA_CONTROL_AWB_LOCK_ON : ACAMERA_CONTROL_AWB_LOCK_OFF;
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AWB_LOCK, 1, &awbLock);

        uint8_t omode = mOis ? ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_ON : ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_OFF;
        for(auto& m : mCameraDescription.oisModes) {
            if(m == omode)
                ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_LENS_OPTICAL_STABILIZATION_MODE, 1, &omode);
        }

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_MODE, 1, &controlMode);
    }

    bool CameraStateManager::setUserFocus() {
        updateCaptureRequestExposure();

        uint8_t afMode = ACAMERA_CONTROL_AF_MODE_AUTO;

        // Adjust focus if manual focus is set
        if(mFocusDistance >= 0) {
            // For sufficiently small enough values, assume infinity
            if(mFocusDistance <= 1e-5f)
                mFocusDistance = 0.0f;

            ACaptureRequest_setEntry_float(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_LENS_FOCUS_DISTANCE, 1, &mFocusDistance);
            afMode = ACAMERA_CONTROL_AF_MODE_OFF;
        }
        else {
            int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.25);
            int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.25);

            int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * mRequestedFocusX);
            int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * mRequestedFocusY);

            int32_t afRegion[5] = { px - w/2, py - h/2,
                                    px + w/2, py + h/2,
                                    1000 };

            // Set the focus region
            if(mCameraDescription.maxAeRegions > 0) {
                ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);
            }

            // Set auto exposure region if supported or use user settings for exposure
            if(mCameraExposureMode == CameraMode::AUTO) {
                if(mCameraDescription.maxAeRegions > 0) {
                    ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afRegion[0]);
                }
            }
        }

        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        // Set up focus region
        int activeSequenceId = mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId;
        setState(State::USER_FOCUS_WAIT, activeSequenceId);

        ACameraCaptureSession_setRepeatingRequest(
            mSessionContext.captureSession.get(),
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
            1,
            &mSessionContext.repeatCaptureRequest->captureRequest,
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId);

        // Kick off focus trigger if touch to focus
        if(afMode == ACAMERA_CONTROL_AF_MODE_AUTO) {
            afTrigger = ACAMERA_CONTROL_AF_TRIGGER_START;
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

            int sequenceId = -1;

            auto result = ACameraCaptureSession_capture(
                    mSessionContext.captureSession.get(),
                    &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                    1,
                    &mSessionContext.repeatCaptureRequest->captureRequest,
                    &sequenceId);

            return result == ACAMERA_OK;
        }
        else {
            return true;
        }
    }

    bool CameraStateManager::setAutoFocus() {
        updateCaptureRequestExposure();

        // Set focus type
        float infiniteFocusDistance = 0;
        ACaptureRequest_setEntry_float(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_LENS_FOCUS_DISTANCE, 1, &infiniteFocusDistance);

        uint8_t afMode = mFocusForVideo ? ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO : ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;

        // Pick default frame rate
        int32_t startRange = mFrameRate;
        int32_t endRange = mFrameRate;

        // Pick the widest range
        if(mFrameRate < 0) {
            int low = 30;

            auto it = mCameraDescription.availableFpsRange.begin();
            while(it != mCameraDescription.availableFpsRange.end()) {
                if(it->second == 30) {
                    if(it->first < low) {
                        low = it->first;
                    }
                }
                ++it;
            }

            startRange = low;
            endRange = 30;
        }

        // Set the focus region
        int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * 0.5f);
        int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * 0.5f);

        if(mCameraDescription.maxAfRegions > 0) {
            int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.25);
            int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.25);

            int32_t afRegion[5] = {px - w/2, py - h/2,
                                   px + w/2, py + h/2,
                                   1000 };

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);
            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        }

        // Set auto exposure region if supported
        if(mCameraExposureMode == CameraMode::AUTO && mCameraDescription.maxAeRegions > 0) {
            int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.75);
            int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.75);

            int32_t aeRegion[5] = { px - w/2, py - h/2,
                                    px + w/2, py + h/2,
                                    1000 };

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &aeRegion[0]);
        }

        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        int32_t frameDuration[2] = { startRange, endRange };

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);
        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, &frameDuration[0]);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);

        LOGD("setAutoFocus(%d,%d)", startRange, endRange);

        int activeSequenceId = mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId;
        setState(State::AUTO_FOCUS_WAIT, activeSequenceId);

        return ACameraCaptureSession_setRepeatingRequest(
                mSessionContext.captureSession.get(),
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                1,
                &mSessionContext.repeatCaptureRequest->captureRequest,
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    void CameraStateManager::nextAction() {
        LOGD("nextAction(%s)", ToString(mRequestedAction).c_str());

        if(mRequestedAction == Action::REQUEST_AUTO_FOCUS) {
            requestAutoFocus();
        }
        else if(mRequestedAction == Action::REQUEST_USER_TOUCH_FOCUS) {
            requestUserFocus(mRequestedFocusX, mRequestedFocusY);
        }
        else if(mRequestedAction == Action::REQUEST_USER_MANUAL_FOCUS) {
            requestManualFocus(mFocusDistance);
        }

        setNextAction(Action::NONE);
    }

    void CameraStateManager::nextState(CameraCaptureSessionState state) {
        if(mState == State::AUTO_FOCUS_WAIT) {
            LOGD("AUTO_FOCUS_WAIT completed");
            setState(State::AUTO_FOCUS_ACTIVE, -1);
        }
        else if(mState == State::USER_FOCUS_WAIT) {
            LOGD("USER_FOCUS_WAIT completed");
            setState(State::USER_FOCUS_ACTIVE, -1);
        }
    }

    void CameraStateManager::onCameraSessionStateChanged(const CameraCaptureSessionState state) {
        LOGD("onCameraSessionStateChanged(%d)", state);

        mCaptureSessionState = state;

        if(state == CameraCaptureSessionState::ACTIVE) {
            nextState(mCaptureSessionState);
        }
    }

    void CameraStateManager::onCameraCaptureSequenceCompleted(const int sequenceId) {
        LOGD("onCameraCaptureSequenceCompleted(%d)", sequenceId);

        if(mPendingSequenceId == sequenceId) {
            nextState(mCaptureSessionState);
            nextAction();
        }
    }
}
