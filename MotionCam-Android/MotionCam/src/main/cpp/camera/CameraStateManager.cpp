#include "CameraStateManager.h"
#include "CameraSessionContext.h"
#include "Logger.h"

#include <camera/NdkCameraDevice.h>

namespace motioncam {

    CameraStateManager::CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription) :
            mSessionContext(context),
            mCameraDescription(cameraDescription),
            mCameraExposureMode(CameraMode::AUTO),
            mCameraFocusMode(CameraMode::AUTO),
            mState(State::PAUSED),
            mExposureCompensation(0),
            mRequestedFocusX(0),
            mRequestedFocusY(0),
            mUserIso(0),
            mUserExposureTime(0),
            mFrameRate(-1),
            mAwbLock(false),
            mAELock(false),
            mOis(true),
            mFocusDistance(-1),
            mFocusForVideo(false),
            mRequestedAperture(-1)
    {
    }

    void CameraStateManager::start(const CameraStartupSettings& startupSettings) {
        mFocusForVideo = startupSettings.focusForVideo;
        //mFrameRate = startupSettings.frameRate; // Don't restore frame rate
        mUserIso = startupSettings.iso;
        mUserExposureTime = startupSettings.exposureTime;
        mOis = startupSettings.ois;
        mCameraExposureMode = startupSettings.useUserExposureSettings ? CameraMode::MANUAL : CameraMode::AUTO;
        mCameraFocusMode = CameraMode::AUTO;
        mRequestedAperture = -1;
        mAELock = false;

        activate();
    }

    void CameraStateManager::requestUserExposure(int32_t iso, int64_t exposureTime) {
        mUserIso = iso;
        mUserExposureTime = exposureTime;
        mCameraExposureMode = CameraMode::MANUAL;
        mAELock = false;
    }

    void CameraStateManager::requestExposureMode(CameraMode mode) {
        mCameraExposureMode = mode;
        mFocusDistance = -1;
    }

    void CameraStateManager::requestPause() {
        LOGD("requestPause()");

        mFocusDistance = -1;
        mCameraExposureMode = CameraMode::AUTO;
        mCameraFocusMode = CameraMode::AUTO;
        mRequestedFocusX = 0.5f;
        mRequestedFocusY = 0.5f;

        ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
    }

    void CameraStateManager::requestResume() {
        LOGD("requestResume()");

        if( mState == State::PAUSED ) {
            activate();
        }
    }

    void CameraStateManager::requestAperture(float aperture) {
        LOGD("requestAperture(%.2f)", aperture);
        mRequestedAperture = aperture;
    }

    void CameraStateManager::requestManualFocus(float distance) {
        LOGD("requestManualFocus(%.2f)", distance);

        mFocusDistance = distance;

        if(distance >= 0) {
            mCameraFocusMode = CameraMode::MANUAL;
        }
        else {
            mCameraFocusMode = CameraMode::AUTO;
        }
    }

    void CameraStateManager::requestUserFocus(float x, float y) {
        LOGD("requestUserFocus(%.2f, %.2f)", x, y);

        mRequestedFocusX = x;
        mRequestedFocusY = y;
        mFocusDistance = -1;
        mCameraFocusMode = CameraMode::AUTO_USER_REGION;
    }

    void CameraStateManager::requestAutoFocus() {
        LOGD("requestAutoFocus()");

        mRequestedFocusX = 0.5f;
        mRequestedFocusY = 0.5f;
        mFocusDistance = -1;
        mCameraFocusMode = CameraMode::AUTO;
    }

    void CameraStateManager::requestExposureCompensation(int exposureCompensation) {
        if(mExposureCompensation == exposureCompensation)
            return;

        LOGD("exposureCompensation(%d)", exposureCompensation);

        mExposureCompensation = exposureCompensation;
    }

    void CameraStateManager::requestAwbLock(bool lock) {
        mAwbLock = lock;

        LOGD("requestAwbLock(%d)", lock);
    }

    void CameraStateManager::requestAELock(bool lock) {
        mAELock = lock;

        LOGD("requestAELock(%d)", lock);
    }

    void CameraStateManager::requestOis(bool ois) {
        mOis = ois;

        LOGD("requestOis(%d)", ois);
    }

    void CameraStateManager::requestFrameRate(int frameRate) {
        if(mFrameRate == frameRate)
            return;

        LOGD("requestFrameRate(%d)", frameRate);

        mFrameRate = frameRate;
    }

    void CameraStateManager::requestFocusForVideo(bool focusForVideo) {
        mFocusForVideo = focusForVideo;

        LOGD("setFocusForVideo(%d)", focusForVideo);
    }

    void CameraStateManager::updateCaptureRequestFocus() {
        uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;

        if(mCameraFocusMode == CameraMode::AUTO_USER_REGION) {
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

            LOGD("mode=USER_REGION, px=%d, py=%d", px, py);
        }
        else if(mCameraFocusMode == CameraMode::AUTO) {
            afMode = mFocusForVideo ? ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO : ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;

            // Set the focus region
            int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * 0.5f);
            int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * 0.5f);

            if(mCameraDescription.maxAfRegions > 0) {
                int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.33);
                int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.33);

                int32_t afRegion[5] = {px - w/2, py - h/2,
                                       px + w/2, py + h/2,
                                       1000 };

                ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);
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

            LOGD("mode=AUTO, afMode=CONTINUOUS");
        }
        else if(mCameraFocusMode == CameraMode::MANUAL) {
            // For sufficiently small enough values, assume infinity
            if(mFocusDistance <= 1e-5f) {
                mFocusDistance = 0.0f;
            }

            ACaptureRequest_setEntry_float(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_LENS_FOCUS_DISTANCE, 1, &mFocusDistance);
            afMode = ACAMERA_CONTROL_AF_MODE_OFF;

            LOGD("mode=MANUAL, afMode=OFF, focusDistance=%.4f", mFocusDistance);
        }

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
    }

    void CameraStateManager::updateCaptureRequestExposure() {
        uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;

        if(mCameraExposureMode == CameraMode::AUTO) {
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

            int32_t frameDuration[2] = { startRange, endRange };
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, &frameDuration[0]);

            for(int i = 0; i < 2; i++)
                ACaptureRequest_setEntry_i32(mSessionContext.hdrCaptureRequests[i]->captureRequest, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, &frameDuration[0]);

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);

            // AE lock
            uint8_t aeLock = mAELock;

            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_LOCK, 1, &aeLock);
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
    }

    bool CameraStateManager::activate() {
        updateCaptureRequestExposure();

        updateCaptureRequestFocus();

        uint8_t mode = ACAMERA_CONTROL_MODE_AUTO;
        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_MODE, 1, &mode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        // Kick off focus trigger if touch to focus
        if(mCameraFocusMode == CameraMode::AUTO || mCameraFocusMode == CameraMode::AUTO_USER_REGION) {
            afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
            ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

            ACameraCaptureSession_setRepeatingRequest(
                    mSessionContext.captureSession.get(),
                    &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                    1,
                    &mSessionContext.repeatCaptureRequest->captureRequest,
                    &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId);

            return true;
        }
        else {
            ACameraCaptureSession_setRepeatingRequest(
                    mSessionContext.captureSession.get(),
                    &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                    1,
                    &mSessionContext.repeatCaptureRequest->captureRequest,
                    &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId);

            return true;
        }
    }

    void CameraStateManager::onCameraSessionStateChanged(const CameraCaptureSessionState state) {
    }

    void CameraStateManager::onCameraCaptureSequenceCompleted(const int sequenceId) {
    }
}
