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
        CaptureSessionManager();

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

        std::shared_ptr<ACameraManager> cameraManager() const;

    private:
        static bool isCameraSupported(const CameraDescription& cameraDescription);
        void enumerateCameras();
        static void updateCameraMetadata(const std::shared_ptr<ACameraMetadata>& cameraChars, CameraDescription& cameraDescription);

    private:
        std::shared_ptr<ACameraManager> mCameraManager;
        std::vector<std::shared_ptr<CameraDescription>> mCameras;
        std::map<std::string, std::shared_ptr<CameraDescription>> mSupportedCameras;
    };
}

#endif //MOTIONCAM_ANDROID_CAPTURESESSIONMANAGER_H
