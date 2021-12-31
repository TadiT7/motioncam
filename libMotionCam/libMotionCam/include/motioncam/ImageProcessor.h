#ifndef ImageProcessor_hpp
#define ImageProcessor_hpp

#include "motioncam/RawImageMetadata.h"
#include "motioncam/ImageProcessorProgress.h"

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <HalideBuffer.h>

namespace motioncam {
    const int EXPANDED_RANGE        = 16384;
    const int WAVELET_LEVELS        = 4;
    const int EXTEND_EDGE_AMOUNT    = 6;

    class RawImage;
    class RawContainer;
    class Temperature;
    struct PostProcessSettings;
    struct RawData;
    struct HdrMetadata;
    struct PreviewMetadata;
    
    class ImageProgressHelper {
    public:
        ImageProgressHelper(const ImageProcessorProgress& progressListener, int numImages, int start);

        void nextFusedImage();        
        void denoiseCompleted();
        void postProcessCompleted();
        void imageSaved();
        
    private:
        const ImageProcessorProgress& mProgressListener;
        int mStart;
        int mNumImages;
        double mPerImageIncrement;
        int mCurImage;
    };
    
    class ImageProcessor {
    public:
        static void process(const std::string& inputPath,
                            const std::string& outputPath,
                            const ImageProcessorProgress& progressListener);

        static void process(RawContainer& rawContainer, const std::string& outputPath, const ImageProcessorProgress& progressListener);

        static Halide::Runtime::Buffer<uint8_t> createPreview(const RawImageBuffer& rawBuffer,
                                                              const int downscaleFactor,
                                                              const RawCameraMetadata& cameraMetadata,
                                                              const PostProcessSettings& settings);

        static Halide::Runtime::Buffer<uint8_t> createFastPreview(const RawImageBuffer& rawBuffer,
                                                                  const int sx,
                                                                  const int sy,
                                                                  const RawCameraMetadata& cameraMetadata);

        static Halide::Runtime::Buffer<uint8_t> generateStats(const RawImageBuffer& rawBuffer,
                                                              const int sx,
                                                              const int sy,
                                                              const RawCameraMetadata& cameraMetadata);

        static cv::Mat calcHistogram(const RawCameraMetadata& cameraMetadata,
                                     const RawImageBuffer& reference,
                                     const bool cumulative,
                                     const int downscale);

        static float getShadowKeyValue(float ev, bool nightMode);
        
        static void estimateSettings(const RawImageBuffer& rawBuffer,
                                     const RawCameraMetadata& cameraMetadata,
                                     PostProcessSettings& outSettings);
        
        static void estimateBlackWhitePoint(const RawImageBuffer& rawBuffer,
                                            const RawCameraMetadata& cameraMetadata,
                                            const PostProcessSettings& postProcessSettings,
                                            float& outBlackPoint,
                                            float& outWhitePoint);
        
        static float estimateShadows(const cv::Mat& histogram, float ev, float keyValue=0.22f);
        static void estimateHdr(const cv::Mat& histogram, float& outLows, float& outHighs);
        static float estimateExposureCompensation(const cv::Mat& histogram, float threshold=1e-4f);
        
        static std::vector<float>& estimateDenoiseWeights(const float noise);
        
        static double measureSharpness(const RawImageBuffer& rawBuffer);

        static void measureImage(RawImageBuffer& rawImage, const RawCameraMetadata& cameraMetadata, float& outSceneLuminosity);
        static void measureNoise(const RawImageBuffer& rawBuffer, std::vector<float>& outNoise, std::vector<float>& outSignal, const int patchSize=8);

        static cv::Mat registerImage(const Halide::Runtime::Buffer<uint8_t>& referenceBuffer,
                                     const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer);

        static cv::Mat registerImage2(const Halide::Runtime::Buffer<uint8_t>& referenceBuffer,
                                      const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer);

        static std::shared_ptr<RawData> loadRawImage(const RawImageBuffer& rawImage,
                                                     const RawCameraMetadata& cameraMetadata,
                                                     const bool extendEdges=true,
                                                     const float scalePreview=1.0f);
        
        static void createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                     const RawImageMetadata& rawImageMetadata,
                                     const Temperature& temperature,
                                     cv::Vec3f& cameraWhite,
                                     cv::Mat& outCameraToPcs,
                                     cv::Mat& outPcsToSrgb);

        static void createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                     const RawImageMetadata& rawImageMetadata,
                                     const cv::Vec3f& asShot,
                                     cv::Vec3f& cameraWhite,
                                     cv::Mat& outCameraToPcs,
                                     cv::Mat& outPcsToSrgb);

        static std::vector<Halide::Runtime::Buffer<uint16_t>> denoise(
            std::shared_ptr<RawImageBuffer> referenceRawBuffer,
            std::vector<std::shared_ptr<RawImageBuffer>> buffers,
            const std::vector<float>& denoiseWeights,
            const RawCameraMetadata& cameraMetadata);

        static Halide::Runtime::Buffer<float> denoise(
            std::shared_ptr<RawImageBuffer> referenceRawBuffer,
            std::vector<std::shared_ptr<RawImageBuffer>> buffers,
            const RawCameraMetadata& cameraMetadata);

        static std::vector<Halide::Runtime::Buffer<uint16_t>> denoise(
            RawContainer& rawContainer,
            std::shared_ptr<RawImageBuffer> referenceRawBuffer,
            float* outNoise,
            ImageProgressHelper& progressHelper);

        static void addExifMetadata(const RawImageMetadata& metadata,
                                    const cv::Mat& thumbnail,
                                    const RawCameraMetadata& cameraMetadata,
                                    const PostProcessSettings& settings,
                                    const std::string& inputOutput);

        static cv::Mat postProcess(std::vector<Halide::Runtime::Buffer<uint16_t>>& inputBuffers,
                                   const std::shared_ptr<HdrMetadata>& hdrMetadata,
                                   int offsetX,
                                   int offsetY,
                                   float noiseEstimate,
                                   const RawImageMetadata& metadata,
                                   const RawCameraMetadata& cameraMetadata,
                                   const PostProcessSettings& settings);

        static float testAlignment(std::shared_ptr<RawData> refImage,
                                   std::shared_ptr<RawData> underexposedImage,
                                   const RawCameraMetadata& cameraMetadata,
                                   cv::Mat warpMatrix,
                                   float exposureScale);
        
        static std::shared_ptr<HdrMetadata> prepareHdr(const RawCameraMetadata& cameraMetadata,
                                                       const PostProcessSettings& settings,
                                                       const RawImageBuffer& reference,
                                                       const RawImageBuffer& underexposed);

        static double calcEv(const RawCameraMetadata& cameraMetadata, const RawImageMetadata& metadata);

        static float adjustShadowsForFaces(cv::Mat input, PreviewMetadata& metadata);
        
        static std::vector<cv::Rect2f> detectFaces(const RawImageBuffer& buffer, const RawCameraMetadata& cameraMetadata);
    };
}

#endif /* ImageProcessor_hpp */
