#ifndef MOTIONCAM_ANDROID_CAPTURESESSIONMANAGER_H
#define MOTIONCAM_ANDROID_CAPTURESESSIONMANAGER_H

#include <camera/NdkCameraManager.h>
#include <motioncam/RawImageMetadata.h>

#include <vector>
#include <memory>

namespace motioncam {
    struct CameraDescription;
    struct RawImageBuffer;
    class CameraSession;
    class RawImageConsumer;
    class DisplayDimension;
    class OutputConfiguration;
    class CameraSessionListener;
    class RawPreviewListener;

    class CaptureSessionManager {
    public:
        CaptureSessionManager(size_t maxMemoryUsageBytes);

        static bool getPreviewConfiguration(
                const CameraDescription& cameraDesc,
                const DisplayDimension& captureSize,
                const DisplayDimension& displaySize,
                OutputConfiguration& outputConfiguration);

        static bool getRawConfiguration(
                const CameraDescription& cameraDesc,
                const bool preferRaw12,
                const bool preferRaw16,
                OutputConfiguration& rawConfiguration);

        std::shared_ptr<CameraDescription> getCameraDescription(const std::string& cameraId) const;
        std::vector<std::string> getSupportedCameras() const;
        std::string getSelectedCameraId() const;

        void startCamera(
                const std::string& cameraId,
                std::shared_ptr<CameraSessionListener> listener,
                std::shared_ptr<ANativeWindow> previewOutputWindow,
                bool setupForRawPreview,
                bool preferRaw12,
                bool preferRaw16,
                const json11::Json& cameraStartupSettings);

        void pauseCamera(bool pause);
        void stopCamera();

        void setManualExposure(int32_t iso, int64_t exposureTime);
        void setAutoExposure();
        void setExposureCompensation(float value);
        void setFrameRate(int frameRate);
        void setAWBLock(bool lock);
        void setOIS(bool on);
        void setFocusDistance(float focusDistance);
        void setFocusForVideo(bool focusForVideo);
        void setAELock(bool lock);
        void setLensAperture(float lensAperture);

        void setFocusPoint(float focusX, float focusY, float exposureX, float exposureY);
        void setAutoFocus();

        void enableRawPreview(std::shared_ptr<RawPreviewListener> listener, const int previewQuality, bool overrideWb);
        void updateRawPreviewSettings(
                float shadows,
                float contrast,
                float saturation,
                float blacks,
                float whitePoint,
                float tempOffset,
                float tintOffset,
                bool useVideoPreview);

        void disableRawPreview();
        void getEstimatedPostProcessSettings(PostProcessSettings& outSettings);

        void updateOrientation(ScreenOrientation orientation);

        void captureHdrImage(
            const int numImages,
            const int baseIso,
            const int64_t baseExposure,
            const int hdrIso,
            const int64_t hdrExposure,
            const motioncam::PostProcessSettings& settings,
            const std::string& outputPath);

        void captureHdrImage(
                const int numImages,
                const motioncam::PostProcessSettings& settings,
                const std::string& outputPath);

        void prepareHdrCapture(const int iso, const int64_t exposure);

        void grow(size_t maxMemoryBytes);

    private:
        static bool isCameraSupported(const CameraDescription& cameraDescription);
        void enumerateCameras();
        static void updateCameraMetadata(const std::shared_ptr<ACameraMetadata>& cameraChars, CameraDescription& cameraDescription);

    private:
        std::shared_ptr<ACameraManager> mCameraManager;
        size_t mMaxMemoryUsageBytes;
        std::vector<std::shared_ptr<CameraDescription>> mCameras;
        std::map<std::string, std::shared_ptr<CameraDescription>> mSupportedCameras;
        std::shared_ptr<RawImageConsumer> mImageConsumer;

        std::shared_ptr<CameraSession> mCameraSession;
        std::string mSelectedCameraId;
    };
}

#endif //MOTIONCAM_ANDROID_CAPTURESESSIONMANAGER_H
