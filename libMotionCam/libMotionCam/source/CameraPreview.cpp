#include "motioncam/CameraPreview.h"
#include "motioncam/RawCameraMetadata.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/RawImageBuffer.h"


#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/ImageProcessor.h"

#include "camera_video_preview2_raw10.h"
#include "camera_video_preview3_raw10.h"
#include "camera_video_preview4_raw10.h"

#include "camera_video_preview2_raw12.h"
#include "camera_video_preview3_raw12.h"
#include "camera_video_preview4_raw12.h"

#include "camera_video_preview2_raw16.h"
#include "camera_video_preview3_raw16.h"
#include "camera_video_preview4_raw16.h"

#include "camera_preview2_raw10.h"
#include "camera_preview3_raw10.h"
#include "camera_preview4_raw10.h"

#include "camera_preview2_raw12.h"
#include "camera_preview3_raw12.h"
#include "camera_preview4_raw12.h"

#include "camera_preview2_raw16.h"
#include "camera_preview3_raw16.h"
#include "camera_preview4_raw16.h"

namespace motioncam {

    void CameraPreview::generate(const RawImageBuffer& rawBuffer,
                                 const RawCameraMetadata& cameraMetadata,
                                 const int downscaleFactor,
                                 const float shadingMapCorrection,
                                 Halide::Runtime::Buffer<uint8_t>& inputBuffer,
                                 Halide::Runtime::Buffer<uint8_t>& outputBuffer)
    {
        int width = rawBuffer.width / 2 / downscaleFactor;
        int height = rawBuffer.height / 2 / downscaleFactor;

        // Setup buffers
        std::vector<Halide::Runtime::Buffer<float>> shadingMapBuffer;
        std::vector<float> shadingMapScale;
//        float shadingMapMaxScale;
//        
//        ImageProcessor::getNormalisedShadingMap(rawBuffer.metadata,
//                                                shadingMapCorrection,
//                                                shadingMapBuffer,
//                                                shadingMapScale,
//                                                shadingMapMaxScale);

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i].set_host_dirty();
        }
        
        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;

        cv::Vec3f cameraWhite;
        
        // Use user tint/temperature offsets
        CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
        Temperature temperature;
        
        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);
        
        Temperature userTemperature(temperature.temperature(), temperature.tint());
        
        ImageProcessor::createSrgbMatrix(cameraMetadata, rawBuffer.metadata, userTemperature, cameraWhite, cameraToPcs, pcsToSrgb);
        
        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;

        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = Halide::Runtime::Buffer<float>(
            (float*) cameraToSrgb.data, cameraToSrgb.cols, cameraToSrgb.rows);

        cameraToSrgbBuffer.set_host_dirty();

        auto camera_preview = &camera_video_preview2_raw10;
        
        if(rawBuffer.pixelFormat == PixelFormat::RAW10) {
            if(downscaleFactor == 2)
                camera_preview = &camera_video_preview2_raw10;
            else if(downscaleFactor == 3)
                camera_preview = &camera_video_preview3_raw10;
            else if(downscaleFactor == 4)
                camera_preview = &camera_video_preview4_raw10;
            else
                return;
        }
        else if(rawBuffer.pixelFormat == PixelFormat::RAW12) {
            if(downscaleFactor == 2)
                camera_preview = &camera_video_preview2_raw12;
            else if(downscaleFactor == 3)
                camera_preview = &camera_video_preview3_raw12;
            else if(downscaleFactor == 4)
                camera_preview = &camera_video_preview4_raw12;
            else
                return;
        }
        else if(rawBuffer.pixelFormat == PixelFormat::RAW16) {
            if(downscaleFactor == 2)
                camera_preview = &camera_video_preview2_raw16;
            else if(downscaleFactor == 3)
                camera_preview = &camera_video_preview3_raw16;
            else if(downscaleFactor == 4)
                camera_preview = &camera_video_preview4_raw16;
            else
                return;
        }
        else
            return;

        const auto& whiteLevel = cameraMetadata.getWhiteLevel(rawBuffer.metadata);
        const auto& blackLevel = cameraMetadata.getBlackLevel(rawBuffer.metadata);
        
        camera_preview(inputBuffer,
                       rawBuffer.rowStride,
                       rawBuffer.metadata.asShot[0],
                       rawBuffer.metadata.asShot[1],
                       rawBuffer.metadata.asShot[2],
                       shadingMapScale[0],
                       shadingMapScale[1],
                       shadingMapScale[2],
                       cameraToSrgbBuffer,
                       width,
                       height,
                       blackLevel[0],
                       blackLevel[1],
                       blackLevel[2],
                       blackLevel[3],
                       whiteLevel,
                       shadingMapBuffer[0],
                       shadingMapBuffer[1],
                       shadingMapBuffer[2],
                       shadingMapBuffer[3],
                       static_cast<int>(cameraMetadata.sensorArrangment),
                       2.2f,
                       outputBuffer);
        
        outputBuffer.device_sync();
    }

    void CameraPreview::generate(const RawImageBuffer& rawBuffer,
                                 const RawCameraMetadata& cameraMetadata,
                                 const int downscaleFactor,
                                 const float shadingMapCorrection,
                                 const bool flipped,
                                 const float shadows,
                                 const float contrast,
                                 const float saturation,
                                 const float blacks,
                                 const float whitePoint,
                                 const float temperatureOffset,
                                 const float tintOffset,
                                 const float tonemapVariance,
                                 Halide::Runtime::Buffer<uint8_t>& inputBuffer,
                                 Halide::Runtime::Buffer<uint8_t>& outputBuffer)
    {
        ///Measure measure("cameraPreview()");
        
        int width = rawBuffer.width / 2 / downscaleFactor;
        int height = rawBuffer.height / 2 / downscaleFactor;

        // Setup buffers
        std::vector<Halide::Runtime::Buffer<float>> shadingMapBuffer;
        std::vector<float> shadingMapScale;
//        float shadingMapMaxScale;
//
//        ImageProcessor::getNormalisedShadingMap(rawBuffer.metadata,
//                                                shadingMapCorrection,
//                                                shadingMapBuffer,
//                                                shadingMapScale,
//                                                shadingMapMaxScale);

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i].set_host_dirty();
        }

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;

        cv::Vec3f cameraWhite;
        
        // Use user tint/temperature offsets
        CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
        Temperature temperature;
        
        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);
        
        Temperature userTemperature(temperature.temperature() + temperatureOffset, temperature.tint() + tintOffset);
        
        ImageProcessor::createSrgbMatrix(cameraMetadata, rawBuffer.metadata, userTemperature, cameraWhite, cameraToPcs, pcsToSrgb);
        
        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;

        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = Halide::Runtime::Buffer<float>(
            (float*) cameraToSrgb.data, cameraToSrgb.cols, cameraToSrgb.rows);

        cameraToSrgbBuffer.set_host_dirty();

        auto camera_preview = &camera_preview4_raw10;
        
        if(rawBuffer.pixelFormat == PixelFormat::RAW10) {
            if(downscaleFactor == 2)
                camera_preview = &camera_preview2_raw10;
            else if(downscaleFactor == 3)
                camera_preview = &camera_preview3_raw10;
            else if(downscaleFactor == 4)
                camera_preview = &camera_preview4_raw10;
            else
                return;
        }
        else if(rawBuffer.pixelFormat == PixelFormat::RAW12) {
            if(downscaleFactor == 2)
                camera_preview = &camera_preview2_raw12;
            else if(downscaleFactor == 3)
                camera_preview = &camera_preview3_raw12;
            else if(downscaleFactor == 4)
                camera_preview = &camera_preview4_raw12;
            else
                return;
        }
        else if(rawBuffer.pixelFormat == PixelFormat::RAW16) {
            if(downscaleFactor == 2)
                camera_preview = &camera_preview2_raw16;
            else if(downscaleFactor == 3)
                camera_preview = &camera_preview3_raw16;
            else if(downscaleFactor == 4)
                camera_preview = &camera_preview4_raw16;
            else
                return;
        }
        else
            return;
                
        const auto& whiteLevel = cameraMetadata.getWhiteLevel(rawBuffer.metadata);
        const auto& blackLevel = cameraMetadata.getBlackLevel(rawBuffer.metadata);

        camera_preview(inputBuffer,
                       rawBuffer.rowStride,
                       rawBuffer.metadata.asShot[0],
                       rawBuffer.metadata.asShot[1],
                       rawBuffer.metadata.asShot[2],
                       shadingMapScale[0],
                       shadingMapScale[1],
                       shadingMapScale[2],
                       cameraToSrgbBuffer,
                       flipped,
                       width,
                       height,
                       blackLevel[0],
                       blackLevel[1],
                       blackLevel[2],
                       blackLevel[3],
                       whiteLevel,
                       shadingMapBuffer[0],
                       shadingMapBuffer[1],
                       shadingMapBuffer[2],
                       shadingMapBuffer[3],
                       static_cast<int>(cameraMetadata.sensorArrangment),
                       tonemapVariance,
                       2.2f,
                       shadows,
                       blacks,
                       whitePoint,
                       contrast,
                       saturation,
                       outputBuffer);
        
        outputBuffer.device_sync();
    }
}
