#include "motioncam/ImageProcessor.h"
#include "motioncam/RawContainer.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"
#include "motioncam/Settings.h"
#include "motioncam/ImageOps.h"
#include "motioncam/BlueNoiseLUT.h"
#include "motioncam/FaceClassifier.h"

// Halide
#include "generate_edges.h"
#include "measure_image.h"
#include "deinterleave_raw.h"
#include "forward_transform.h"
#include "inverse_transform.h"
#include "fuse_image.h"
#include "fuse_denoise_3x3.h"
#include "fuse_denoise_5x5.h"
#include "fuse_denoise_7x7.h"
#include "fast_preview.h"
#include "measure_noise.h"

#include "linear_image.h"
#include "hdr_mask.h"

#include "preview_landscape2.h"
#include "preview_portrait2.h"
#include "preview_reverse_portrait2.h"
#include "preview_reverse_landscape2.h"
#include "preview_landscape4.h"
#include "preview_portrait4.h"
#include "preview_reverse_portrait4.h"
#include "preview_reverse_landscape4.h"
#include "preview_landscape8.h"
#include "preview_portrait8.h"
#include "preview_reverse_portrait8.h"
#include "preview_reverse_landscape8.h"

#include "postprocess.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>
#include <numeric>
#include <sys/stat.h>
#include <fcntl.h>
#include <exiv2/exiv2.hpp>
#include <opencv2/core/ocl.hpp>

using std::ios;
using std::string;
using std::shared_ptr;
using std::vector;
using std::to_string;
using std::pair;

const int WAVELET_LEVELS     = 4;
const int EXTEND_EDGE_AMOUNT = 6;

extern "C" int extern_defringe(halide_buffer_t *in, int32_t width, int32_t height, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        std::memcpy(&in->dim, &out->dim, out->dimensions * sizeof(halide_dimension_t));
    }
    else {
        Halide::Runtime::Buffer<uint16_t> inBuf(*in);
        Halide::Runtime::Buffer<uint16_t> outBuf(*out);
        
        outBuf.copy_from(inBuf);

        // Disable for now. Causes too many artifacts.
//        motioncam::defringe(outBuf, inBuf);
    }
    
    return 0;
}

static std::vector<Halide::Runtime::Buffer<float>> createWaveletBuffers(int width, int height) {
    std::vector<Halide::Runtime::Buffer<float>> buffers;
    
    for(int level = 0; level < WAVELET_LEVELS; level++) {
        width = width / 2;
        height = height / 2;
        
        buffers.emplace_back(width, height, 4, 4);
    }
    
    return buffers;
}

namespace motioncam {
    const int EXPANDED_RANGE            = 16384;
    const float MAX_HDR_ERROR           = 0.0001f;
    const float SHADOW_BIAS             = 12.0f;

    typedef Halide::Runtime::Buffer<float> WaveletBuffer;

    struct RawData {
        Halide::Runtime::Buffer<uint16_t> rawBuffer;
        Halide::Runtime::Buffer<uint8_t> previewBuffer;
        RawImageMetadata metadata;
    };

    struct HdrMetadata {
        float exposureScale;
        float gain;
        float error;
        Halide::Runtime::Buffer<uint16_t> hdrInput;
    };

    struct PreviewMetadata {
        std::vector<cv::Rect> faces;
        
        PreviewMetadata(const std::string& metadata) {
            std::string err;
            json11::Json metadataJson = json11::Json::parse(metadata, err);

            if(!err.empty()) {
                return;
            }

            auto facesIt = metadataJson.object_items().find("faces");
            if(facesIt != metadataJson.object_items().end()) {
                auto facesArray = facesIt->second.array_items();
                for(auto& f : facesArray) {
                    auto data = f.object_items();
                    
                    int left = data["left"].int_value();
                    int top = data["top"].int_value();
                    int right = data["right"].int_value();
                    int bottom = data["bottom"].int_value();
                    
                    auto boundingBox = cv::Rect(left, top, right - left, bottom - top);
                    
                    faces.push_back(boundingBox);
                }
            }
        }
    };

    // https://exiv2.org/doc/geotag_8cpp-example.html
    static std::string toExifString(double d, bool isRational, bool isLatitude)
    {
        const char* NS   = d>=0.0?"N":"S";
        const char* EW   = d>=0.0?"E":"W";
        const char* NSEW = isLatitude  ? NS: EW;
        
        if ( d < 0 ) d = -d;
        
        int deg = (int) d;
            d  -= deg;
            d  *= 60;
        
        int min = (int) d ;
            d  -= min;
            d  *= 60;
        
        int sec = (int)d;
        
        char result[200];
        
        if (isRational)
            sprintf(result,"%d/1 %d/1 %d/1",deg, min, sec);
        else
            sprintf(result,"%03d%s%02d'%02d\"%s", deg, "deg", min, sec, NSEW);
        
        return std::string(result);
    }

    static std::string toExifString(double d)
    {
        char result[200];
        d *= 100;
        sprintf(result,"%d/100",abs((int)d));
        
        return std::string(result);
    }

    template<typename T>
    static Halide::Runtime::Buffer<T> ToHalideBuffer(const cv::Mat& input) {
        if(input.channels() > 1)
            return Halide::Runtime::Buffer<T>((T*) input.data, input.cols, input.rows, input.channels());
        else
            return Halide::Runtime::Buffer<T>((T*) input.data, input.cols, input.rows);
    }

    struct NativeBufferContext {
        NativeBufferContext(NativeBuffer& buffer, bool write) : nativeBuffer(buffer)
        {
            nativeBufferData = nativeBuffer.lock(write);
        }
        
        Halide::Runtime::Buffer<uint8_t> getHalideBuffer() {
            return Halide::Runtime::Buffer<uint8_t>(nativeBufferData, (int) nativeBuffer.len());
        }
        
        ~NativeBufferContext() {
            nativeBuffer.unlock();
        }
        
    private:
        NativeBuffer& nativeBuffer;
        uint8_t* nativeBufferData;
    };

    ImageProgressHelper::ImageProgressHelper(const ImageProcessorProgress& progressListener, int numImages, int start) :
        mStart(start), mProgressListener(progressListener), mNumImages(numImages), mCurImage(0)
    {
        // Per fused image increment is numImages over a 75% progress amount.
        mPerImageIncrement = 75.0 / numImages;
    }
    
    void ImageProgressHelper::postProcessCompleted() {
        mProgressListener.onProgressUpdate(mStart + 95);
    }
    
    void ImageProgressHelper::denoiseCompleted() {
        // Starting point is mStart, denoising takes 50%, progress should now be mStart + 50%
        mProgressListener.onProgressUpdate(mStart + 75);
    }

    void ImageProgressHelper::nextFusedImage() {
        ++mCurImage;
        mProgressListener.onProgressUpdate(static_cast<int>(mStart + (mPerImageIncrement * mCurImage)));
    }

    void ImageProgressHelper::imageSaved() {
        mProgressListener.onProgressUpdate(100);
        mProgressListener.onCompleted();
    }

    double ImageProcessor::calcEv(const RawCameraMetadata& cameraMetadata, const RawImageMetadata& metadata) {
        double a = 1.8;
        if(!cameraMetadata.apertures.empty())
            a = cameraMetadata.apertures[0];

        double s = a*a;
        return std::log2(s / (metadata.exposureTime / (1.0e9))) - std::log2(metadata.iso / 100.0);
    }


    cv::Mat ImageProcessor::postProcess(std::vector<Halide::Runtime::Buffer<uint16_t>>& inputBuffers,
                                        const shared_ptr<HdrMetadata>& hdrMetadata,
                                        int offsetX,
                                        int offsetY,
                                        const float noiseEstimate,
                                        const RawImageMetadata& metadata,
                                        const RawCameraMetadata& cameraMetadata,
                                        const PostProcessSettings& settings)
    {
        Measure measure("postProcess");

        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(metadata.lensShadingMap[i]);
        }

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;

        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, metadata, metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }

        // Get blue noise buffer
        auto noise = cv::imdecode(BLUE_NOISE_PNG, cv::IMREAD_UNCHANGED);
                
        Halide::Runtime::Buffer<uint8_t> noiseBuffer =
            Halide::Runtime::Buffer<uint8_t>::make_interleaved((uint8_t*) noise.data, noise.cols, noise.rows, 4);
        
        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;
        
        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);
        
        cv::Mat output((inputBuffers[0].height() - offsetY)*2, (inputBuffers[0].width() - offsetX)*2, CV_8UC3);
        
        Halide::Runtime::Buffer<uint8_t> outputBuffer(
            Halide::Runtime::Buffer<uint8_t>::make_interleaved(output.data, output.cols, output.rows, 3));

        // Edges are garbage, don't process them
        outputBuffer.translate(0, offsetX);
        outputBuffer.translate(1, offsetY);

        for(int i = 0; i < 4; i++) {
            inputBuffers[i].set_host_dirty();
            shadingMapBuffer[i].set_host_dirty();
        }
        
        Halide::Runtime::Buffer<uint16_t> hdrInput;
        
        float shadows = settings.shadows;
        
        float tonemapVariance = 0.25f;
        bool useHdr = false;
        float hdrInputGain = 1.0f;
        float hdrScale = 1.0f;
        
        if(hdrMetadata) {
            hdrInput = hdrMetadata->hdrInput;
            hdrInputGain = hdrMetadata->gain;
            hdrScale = 1.0f / hdrMetadata->exposureScale;
            useHdr = true;
            tonemapVariance = 0.22f;
        }
        else {
            // Don't apply underexposed image when error is too high
            logger::log("Not using HDR image");
            
            hdrInput = Halide::Runtime::Buffer<uint16_t>(inputBuffers[0].width()*2, inputBuffers[0].height()*2, 3);
            
            useHdr = false;
        }
        
        postprocess(inputBuffers[0],
                    inputBuffers[1],
                    inputBuffers[2],
                    inputBuffers[3],
                    noiseBuffer,
                    hdrInput,
                    useHdr,
                    metadata.asShot[0],
                    metadata.asShot[1],
                    metadata.asShot[2],
                    cameraToSrgbBuffer,
                    shadingMapBuffer[0],
                    shadingMapBuffer[1],
                    shadingMapBuffer[2],
                    shadingMapBuffer[3],
                    EXPANDED_RANGE,
                    static_cast<int>(cameraMetadata.sensorArrangment),
                    shadows,
                    hdrInputGain,
                    hdrScale,
                    tonemapVariance,
                    settings.blacks,
                    settings.exposure,
                    settings.whitePoint,
                    settings.contrast,
                    settings.brightness,
                    settings.blues,
                    settings.greens,
                    settings.saturation,
                    settings.sharpen0,
                    settings.sharpen1,
                    settings.pop,
                    128.0f,
                    7.0f,
                    std::min(0.015f, std::max(0.005f, noiseEstimate / 2.0f)),
                    outputBuffer);

        outputBuffer.device_sync();
        outputBuffer.copy_to_host();
        
        return output;
    }

    void ImageProcessor::estimateBlackWhitePoint(const RawImageBuffer& rawBuffer,
                                                 const RawCameraMetadata& cameraMetadata,
                                                 const PostProcessSettings& postProcessSettings,
                                                 float& outBlackPoint,
                                                 float& outWhitePoint)
    {
        PostProcessSettings settings = postProcessSettings;
            
        settings.blacks     = 0.0f;
        settings.whitePoint = 1.0f;
        
        Halide::Runtime::Buffer<uint8_t> previewBuffer;
        
        previewBuffer = createPreview(rawBuffer, 2, cameraMetadata, settings);

        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        cv::Mat histogram;
                
        cv::cvtColor(preview, preview, cv::COLOR_RGBA2GRAY);
        
        vector<cv::Mat> inputImages     = { preview };
        const vector<int> channels      = { 0 };
        const vector<int> histBins      = { 256 };
        const vector<float> histRange   = { 0, 255 };

        cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);
        
        histogram = histogram / (preview.rows * preview.cols);
                
        // Cumulative histogram
        for(int i = 1; i < histogram.rows; i++) {
            histogram.at<float>(i) += histogram.at<float>(i - 1);
        }
                
        // Estimate black point
        const int maxBlackPointBin = 0.07f * histBins[0] + 0.5f;

        int endBin = 1;
        
        for(; endBin < maxBlackPointBin; endBin++) {
            float p0 = histogram.at<float>(endBin);
            float p1 = histogram.at<float>(endBin + 1);
                        
            if(p1 - p0 > 0.001f)
                break;
        }
                
        outBlackPoint = (endBin - 1) / (float) (histogram.rows - 1);
        
        // Estimate white point
        const int maxWhitePointBin = 0.75f * histBins[0] + 0.5f;
        const float whitePoint = 0.995f;
        
        for(endBin = histogram.rows - 2; endBin >= maxWhitePointBin; endBin--) {
            float p = histogram.at<float>(endBin);

            if(p > whitePoint)
                break;
        }
        
        outWhitePoint = static_cast<float>(endBin + 1) / ((float) histogram.rows);
    }

    float ImageProcessor::estimateShadows(const cv::Mat& histogram, float keyValue) {
        float avgLuminance = 0.0f;
        float totalPixels = 0;
        
        int lowerBound = 0;
        int upperBound = histogram.cols;
        
        for(int i = lowerBound; i < upperBound; i++) {
            avgLuminance += histogram.at<float>(i) * log(1e-5 + i / 255.0f);
            totalPixels += histogram.at<float>(i);
        }
        
        avgLuminance = exp(avgLuminance / (totalPixels + 1e-5f));
        
        return std::max(1.0f, std::min(keyValue / avgLuminance, 24.0f));
    }

    void ImageProcessor::estimateHdr(const cv::Mat& histogram, float& outLows, float& outHighs) {
        outLows = histogram.at<float>(0);
        outHighs = histogram.at<float>(histogram.cols - 1);
    }

    float ImageProcessor::estimateExposureCompensation(const cv::Mat& histogram, float threshold) {
        int bin = 0;
        float total = 0.0f;
        
        // Exposure compensation
        for(int i = histogram.cols - 1; i >= 0; i--) {
            float p = total + histogram.at<float>(i);
        
            if(p - total < 0.0001f) {
                total = p;
                continue;
            }
        
            if(p >= threshold) {
                bin = i;
                break;
            }
            
            total = p;
        }
        
        double m = histogram.cols / static_cast<double>(bin + 1);
        return std::log2(m);
    }

    std::vector<float>& ImageProcessor::estimateDenoiseWeights(const float noise) {
        const float NOISE_MAP[] = {
            0.001f,
            0.005f,
            0.01f,
            0.03f,
            0.05f,
            0.07f,
        };
        
        static std::vector<std::vector<float>> WEIGHTS = {
            { 12, 4,   2,   1  },
            { 8,  4,   2,   1  },
            { 6,  4,   1,   1  },
            { 4,  2,   1,   0  },
            { 2,  1,   0.5, 0  },
            { 1,  1,   0,   0  }
        };
        
        float minDiff = 1e5f;
        int w = (int) WEIGHTS.size()-1;
        
        for(int i = 0; i < WEIGHTS.size(); i++) {
            float diff = abs(noise  - NOISE_MAP[i]);
            
            if(diff < minDiff) {
                minDiff = diff;
                w = i;
            }
        }
        
        return WEIGHTS[w];
    }

    float ImageProcessor::getShadowKeyValue(const RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, bool nightMode) {
        float ev = calcEv(cameraMetadata, rawBuffer.metadata);
        float minKv = 1.03f;
        
        if(nightMode)
            minKv = 1.07f;
        
        return minKv - SHADOW_BIAS / (SHADOW_BIAS + std::log10(std::pow(10.0f, ev) + 1));
    }

    void ImageProcessor::estimateSettings(const RawImageBuffer& rawBuffer,
                                          const RawCameraMetadata& cameraMetadata,
                                          PostProcessSettings& outSettings)
    {
//        Measure measure("estimateSettings()");
        float keyValue = getShadowKeyValue(rawBuffer, cameraMetadata, false);

        // Start with basic initial values
        CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
        Temperature temperature;

        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);

        cv::Mat histogram = calcHistogram(cameraMetadata, rawBuffer, false, 4);

        outSettings.temperature    = static_cast<float>(temperature.temperature());
        outSettings.tint           = static_cast<float>(temperature.tint());
        outSettings.shadows        = estimateShadows(histogram, keyValue);
        outSettings.exposure       = estimateExposureCompensation(histogram, 1e-3f);

        estimateHdr(histogram, outSettings.clippedLows, outSettings.clippedHighs);
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const Temperature& temperature,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const cv::Vec3f& asShot,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);
        Temperature temperature;

        cv::Vec3f asShotVector = asShot;
        float max = math::max(asShotVector);
        
        if(max > 0) {
            asShotVector[0] = asShotVector[0] * (1.0f / max);
            asShotVector[1] = asShotVector[1] * (1.0f / max);
            asShotVector[2] = asShotVector[2] * (1.0f / max);
        }
        else {
            throw InvalidState("Camera white balance vector is zero");
        }

        cameraProfile.temperatureFromVector(asShotVector, temperature);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    Halide::Runtime::Buffer<uint8_t> ImageProcessor::createPreview(const RawImageBuffer& rawBuffer,
                                                                   const int downscaleFactor,
                                                                   const RawCameraMetadata& cameraMetadata,
                                                                   const PostProcessSettings& settings)
    {
        //Measure measure("createPreview()");
        
        if(downscaleFactor != 2 && downscaleFactor != 4 && downscaleFactor != 8) {
            throw InvalidState("Invalid downscale factor");
        }
        
        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, rawBuffer.metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, rawBuffer.metadata, rawBuffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }

        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;
        
        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(rawBuffer.metadata.lensShadingMap[i]);
        }

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);

        // Set up rotation based on orientation of image
        int width = rawBuffer.width / 2 / downscaleFactor; // Divide by 2 because we are not demosaicing the RAW data
        int height = rawBuffer.height / 2 / downscaleFactor;
        
        auto method = &preview_landscape2;
        
        switch(rawBuffer.metadata.screenOrientation) {
            case ScreenOrientation::REVERSE_PORTRAIT:
                if(downscaleFactor == 2)
                    method = &preview_reverse_portrait2;
                else if(downscaleFactor == 4)
                    method = &preview_reverse_portrait4;
                else
                    method = &preview_reverse_portrait8;

                std::swap(width, height);
                break;

            case ScreenOrientation::REVERSE_LANDSCAPE:
                if(downscaleFactor == 2)
                    method = &preview_reverse_landscape2;
                else if(downscaleFactor == 4)
                    method = &preview_reverse_landscape4;
                else
                    method = &preview_reverse_landscape8;

                break;

            case ScreenOrientation::PORTRAIT:
                if(downscaleFactor == 2)
                    method = &preview_portrait2;
                else if(downscaleFactor == 4)
                    method = &preview_portrait4;
                else
                    method = &preview_portrait8;

                std::swap(width, height);
                break;

            default:
            case ScreenOrientation::LANDSCAPE:
                if(downscaleFactor == 2)
                    method = &preview_landscape2;
                else if(downscaleFactor == 4)
                    method = &preview_landscape4;
                else
                    method = &preview_landscape8;
                break;
        }
       
        Halide::Runtime::Buffer<uint8_t> outputBuffer =
            Halide::Runtime::Buffer<uint8_t>::make_interleaved(width, height, 4);
        
        method(
            inputBufferContext.getHalideBuffer(),
            shadingMapBuffer[0],
            shadingMapBuffer[1],
            shadingMapBuffer[2],
            shadingMapBuffer[3],
            rawBuffer.metadata.asShot[0],
            rawBuffer.metadata.asShot[1],
            rawBuffer.metadata.asShot[2],
            cameraToSrgbBuffer,
            rawBuffer.width / 2 / downscaleFactor,
            rawBuffer.height / 2 / downscaleFactor,
            rawBuffer.rowStride,
            static_cast<int>(rawBuffer.pixelFormat),
            static_cast<int>(cameraMetadata.sensorArrangment),
            cameraMetadata.blackLevel[0],
            cameraMetadata.blackLevel[1],
            cameraMetadata.blackLevel[2],
            cameraMetadata.blackLevel[3],
            static_cast<uint16_t>(cameraMetadata.whiteLevel),
            settings.shadows,
            settings.whitePoint,
            0.25f,
            settings.blacks,
            settings.exposure,
            settings.contrast,
            settings.brightness,
            settings.blues,
            settings.greens,
            settings.saturation,
            settings.sharpen0,
            settings.sharpen1,
            settings.pop,
            settings.flipped,
            outputBuffer);

        outputBuffer.device_sync();
        outputBuffer.copy_to_host();

        return outputBuffer;
    }
    
    std::shared_ptr<RawData> ImageProcessor::loadRawImage(const RawImageBuffer& rawBuffer,
                                                          const RawCameraMetadata& cameraMetadata,
                                                          const bool extendEdges,
                                                          const float scalePreview)
    {
        // Extend the image so it can be downscaled by 'LEVELS' for the denoising step
        int extendX = 0;
        int extendY = 0;

        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        if(extendEdges) {
            const int T = pow(2, EXTEND_EDGE_AMOUNT);

            extendX = static_cast<int>(T * ceil(halfWidth / (double) T) - halfWidth);
            extendY = static_cast<int>(T * ceil(halfHeight / (double) T) - halfHeight);
        }
        
        auto rawData = std::make_shared<RawData>();

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        
        rawData->previewBuffer  = Halide::Runtime::Buffer<uint8_t>(halfWidth + extendX, halfHeight + extendY);
        rawData->rawBuffer      = Halide::Runtime::Buffer<uint16_t>(halfWidth + extendX, halfHeight + extendY, 4);
        rawData->metadata       = rawBuffer.metadata;
                
        deinterleave_raw(inputBufferContext.getHalideBuffer(),
                         rawBuffer.rowStride,
                         static_cast<int>(rawBuffer.pixelFormat),
                         static_cast<int>(cameraMetadata.sensorArrangment),
                         halfWidth,
                         halfHeight,
                         extendX / 2,
                         extendY / 2,
                         cameraMetadata.whiteLevel,
                         cameraMetadata.blackLevel[0],
                         cameraMetadata.blackLevel[1],
                         cameraMetadata.blackLevel[2],
                         cameraMetadata.blackLevel[3],
                         scalePreview,
                         rawData->rawBuffer,
                         rawData->previewBuffer);
                        
        return rawData;
    }

    void ImageProcessor::measureImage(RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, float& outSceneLuminosity)
    {
//        Measure measure("measureImage");

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        createSrgbMatrix(cameraMetadata, rawBuffer.metadata, rawBuffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);

        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;
        
        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(rawBuffer.metadata.lensShadingMap[i]);
        }

        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        const int downscale = 4;

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        Halide::Runtime::Buffer<uint32_t> histogramBuffer(2u << 7u);

        measure_image(inputBufferContext.getHalideBuffer(),
                      rawBuffer.rowStride,
                      static_cast<int>(rawBuffer.pixelFormat),
                      halfWidth,
                      halfHeight,
                      downscale,
                      cameraMetadata.blackLevel[0],
                      cameraMetadata.blackLevel[1],
                      cameraMetadata.blackLevel[2],
                      cameraMetadata.blackLevel[3],
                      cameraMetadata.whiteLevel,
                      cameraWhite[0],
                      cameraWhite[1],
                      cameraWhite[2],
                      cameraToSrgbBuffer,
                      shadingMapBuffer[0],
                      shadingMapBuffer[1],
                      shadingMapBuffer[2],
                      shadingMapBuffer[3],
                      static_cast<int>(cameraMetadata.sensorArrangment),
                      histogramBuffer);

        histogramBuffer.device_sync();
        histogramBuffer.copy_to_host();

        cv::Mat histogram(histogramBuffer.height(), histogramBuffer.width(), CV_32S, histogramBuffer.data());

        // Normalize
        histogram.convertTo(histogram, CV_32F, 1.0 / (halfWidth/downscale * halfHeight/downscale));

        outSceneLuminosity = 0;
    }

    void ImageProcessor::measureNoise(const RawImageBuffer& rawBuffer, std::vector<float>& outNoise, std::vector<float>& outSignal, const int patchSize) {
        NativeBufferContext context(*rawBuffer.data, false);

        Halide::Runtime::Buffer<float> noiseBuffer(rawBuffer.width / 2 / patchSize, rawBuffer.height / 2 / patchSize, 4);
        Halide::Runtime::Buffer<float> signalBuffer(rawBuffer.width / 2 / patchSize, rawBuffer.height / 2 / patchSize, 4);
        
        measure_noise(context.getHalideBuffer(),
                      rawBuffer.width/2,
                      rawBuffer.height/2,
                      rawBuffer.rowStride,
                      static_cast<int>(rawBuffer.pixelFormat),
                      patchSize,
                      noiseBuffer,
                      signalBuffer);
        

        for(int c = 0; c < 4; c++) {
            cv::Mat noiseImage(noiseBuffer.height(), noiseBuffer.width(), CV_32F, noiseBuffer.data() + c*noiseBuffer.stride(2));
            cv::Mat signalImage(signalBuffer.height(), signalBuffer.width(), CV_32F, signalBuffer.data() + c*signalBuffer.stride(2));
            
            float noise = findMedian(noiseImage);
            float signal = findMedian(signalImage) / (rawBuffer.metadata.iso / 100.0f);
            
            outNoise.push_back(noise);
            outSignal.push_back(signal);
        }
    }

    cv::Mat ImageProcessor::registerImage2(
        const Halide::Runtime::Buffer<uint8_t>& referenceBuffer, const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer)
    {
        Measure measure("registerImage2()");

        static const cv::TermCriteria termCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 50, 0.001f);

        static const float scaleWarpMatrix[] = {
           1,   1,   2,
           1,   1,   2,
           0.5, 0.5, 1 };

        static const cv::Mat s(3, 3, CV_32F, (void*) &scaleWarpMatrix);

        cv::Mat referenceImage(referenceBuffer.height(), referenceBuffer.width(), CV_8U, (void*) referenceBuffer.data());
        cv::Mat toAlignImage(toAlignBuffer.height(), toAlignBuffer.width(), CV_8U, (void*) toAlignBuffer.data());

        const int ALIGN_PYRAMID_LEVELS = 5;
    
        // Create pyramid from reference
        vector<cv::Mat> refPyramid;
        vector<cv::Mat> curPyramid;

        cv::buildPyramid(referenceImage, refPyramid, ALIGN_PYRAMID_LEVELS);
        cv::buildPyramid(toAlignImage, curPyramid, ALIGN_PYRAMID_LEVELS);

        cv::Mat warpMatrix = cv::Mat::eye(3, 3, CV_32F);

        //
        // Align image using the pyramid to speed things up. Skip the original size image and use estimate from
        // second largest image.
        //

        for(int i = (int) curPyramid.size() - 1; i > 0; i--) {
            try {
                cv::findTransformECC(curPyramid[i], refPyramid[i], warpMatrix, cv::MOTION_HOMOGRAPHY, termCriteria);
            }
            catch(cv::Exception& e) {
                return cv::Mat();
            }
            
            if(i > 0)
                warpMatrix = warpMatrix.mul(s);
        }
        
        return warpMatrix;
    }

    cv::Mat ImageProcessor::registerImage(
        const Halide::Runtime::Buffer<uint8_t>& referenceBuffer, const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer)
    {
        //Measure measure("registerImage()");

//        cv::Mat referenceImage(referenceBuffer.height(), referenceBuffer.width(), CV_8U, (void*) referenceBuffer.data());
//        cv::Mat toAlignImage(toAlignBuffer.height(), toAlignBuffer.width(), CV_8U, (void*) toAlignBuffer.data());
//
//        auto detector = cv::ORB::create();
//
//        std::vector<cv::KeyPoint> keypoints1, keypoints2;
//        cv::Mat descriptors1, descriptors2;
//        auto extractor = cv::xfeatures2d::BriefDescriptorExtractor::create();
//
//        detector->detect(referenceImage, keypoints1);
//        detector->detect(toAlignImage, keypoints2);
//
//        extractor->compute(referenceImage, keypoints1, descriptors1);
//        extractor->compute(toAlignImage, keypoints2, descriptors2);
//
//        auto matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
//        std::vector< std::vector<cv::DMatch> > knn_matches;
//
//        matcher->knnMatch( descriptors1, descriptors2, knn_matches, 2 );
//
//        // Filter matches using the Lowe's ratio test
//        const float ratioThresh = 0.75f;
//        std::vector<cv::DMatch> goodMatches;
//
//        for (auto& m : knn_matches)
//        {
//            if (m[0].distance < ratioThresh * m[1].distance)
//                goodMatches.push_back(m[0]);
//        }
//
//        std::vector<cv::Point2f> obj;
//        std::vector<cv::Point2f> scene;
//
//        for(auto& m : goodMatches)
//        {
//            obj.push_back( keypoints1[ m.queryIdx ].pt );
//            scene.push_back( keypoints2[ m.trainIdx ].pt );
//        }
//
//        if(obj.size() < 4 || scene.size() < 4) {
//            return cv::Mat();
//        }
//
//        return findHomography( scene, obj, cv::RANSAC );
        
        return cv::Mat();
    }

    cv::Mat ImageProcessor::calcHistogram(const RawCameraMetadata& cameraMetadata,
                                          const RawImageBuffer& buffer,
                                          const bool cumulative,
                                          const int downscale)
    {
        //Measure measure("calcHistogram()");
        const int halfWidth  = buffer.width / 2;
        const int halfHeight = buffer.height / 2;

        NativeBufferContext inputBufferContext(*buffer.data, false);
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        Halide::Runtime::Buffer<uint32_t> histogramBuffer(2u << 7u);

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        createSrgbMatrix(cameraMetadata, buffer.metadata, buffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);

        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;

        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(buffer.metadata.lensShadingMap[i]);
        }

        measure_image(inputBufferContext.getHalideBuffer(),
                      buffer.rowStride,
                      static_cast<int>(buffer.pixelFormat),
                      halfWidth,
                      halfHeight,
                      downscale,
                      cameraMetadata.blackLevel[0],
                      cameraMetadata.blackLevel[1],
                      cameraMetadata.blackLevel[2],
                      cameraMetadata.blackLevel[3],
                      cameraMetadata.whiteLevel,
                      cameraWhite[0],
                      cameraWhite[1],
                      cameraWhite[2],
                      cameraToSrgbBuffer,
                      shadingMapBuffer[0],
                      shadingMapBuffer[1],
                      shadingMapBuffer[2],
                      shadingMapBuffer[3],
                      static_cast<int>(cameraMetadata.sensorArrangment),
                      histogramBuffer);
        
        cv::Mat histogram(histogramBuffer.height(), histogramBuffer.width(), CV_32S, histogramBuffer.data());
        
        histogram.convertTo(histogram, CV_32F);
        
        if(cumulative) {
            for(int i = 1; i < histogram.cols; i++) {
                histogram.at<float>(i) += histogram.at<float>(i - 1);
            }
            
            histogram /= histogram.at<float>(histogram.cols - 1);
        }
        else {
            for(int i = 0; i < histogram.cols; i++) {
                histogram.at<float>(i) /= (halfWidth/downscale * halfHeight/downscale);
            }
        }
        
        return histogram;
    }

    void ImageProcessor::process(RawContainer& rawContainer, const std::string& outputPath, const ImageProcessorProgress& progressListener)
    {
        cv::ocl::setUseOpenCL(false);
                
        // If this is a HDR capture then find the underexposed images.
        std::vector<std::shared_ptr<RawImageBuffer>> underexposedImages;
        
        // Started
        progressListener.onProgressUpdate(0);
        
        auto referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
        PostProcessSettings settings = rawContainer.getPostProcessSettings();
        
        // Remove all underexposed images
        if(rawContainer.isHdr()) {
            auto refEv = calcEv(rawContainer.getCameraMetadata(), referenceRawBuffer->metadata);
            
            for(auto frameName : rawContainer.getFrames()) {
                auto frame = rawContainer.getFrame(frameName);
                auto ev = calcEv(rawContainer.getCameraMetadata(), frame->metadata);
                        
                if(ev - refEv > 0.25f) {
                    // Load the frame since we intend to remove it from the container
                    auto raw = rawContainer.loadFrame(frameName);
                    underexposedImages.push_back(raw);
                    
                    rawContainer.removeFrame(frameName);
                }
            }
        }
                        
        // Estimate shadows if not set
        if(settings.shadows < 0) {
            float keyValue = getShadowKeyValue(*referenceRawBuffer, rawContainer.getCameraMetadata(), settings.captureMode == "NIGHT");
            cv::Mat histogram = calcHistogram(rawContainer.getCameraMetadata(), *referenceRawBuffer, false, 4);
            
            settings.shadows = estimateShadows(histogram, keyValue);
        }
        
        if(settings.blacks < 0 || settings.whitePoint < 0) {
            estimateBlackWhitePoint(*referenceRawBuffer, rawContainer.getCameraMetadata(), settings, settings.blacks, settings.whitePoint);
        }
        
        //
        // Save preview
        //
        
        Halide::Runtime::Buffer<uint8_t> preview;
        PostProcessSettings previewSettings = settings;
        
        preview = createPreview(*referenceRawBuffer, 2, rawContainer.getCameraMetadata(), previewSettings);
        
        std::string basePath, filename;

        cv::Mat previewImage(preview.height(), preview.width(), CV_8UC4, preview.data());
                
        util::GetBasePath(outputPath, basePath, filename);
        std::string previewPath = basePath + "/PREVIEW_" + filename;
        
        cv::cvtColor(previewImage, previewImage, cv::COLOR_RGBA2BGR);
        cv::imwrite(previewPath, previewImage);
        
        // Parse the returned metadata
        std::string metadataJson = progressListener.onPreviewSaved(previewPath);
        previewImage.release();

        //
        // HDR
        //
        
        shared_ptr<HdrMetadata> hdrMetadata;
  
        if(!underexposedImages.empty() && settings.whitePoint > 0.99f) {
            hdrMetadata = prepareHdr(rawContainer.getCameraMetadata(),
                       settings,
                       *referenceRawBuffer,
                       *underexposedImages[0]);
        }
        
        underexposedImages.clear();
                
        //
        // Denoise
        //
        
        ImageProgressHelper progressHelper(progressListener, static_cast<int>(rawContainer.getFrames().size()), 0);
        
        std::vector<Halide::Runtime::Buffer<uint16_t>> denoiseOutput;
        float noise = 0.0f;
        
        denoiseOutput = denoise(rawContainer, referenceRawBuffer, &noise, progressHelper);
        
        progressHelper.denoiseCompleted();
                
        //
        // Post process
        //
        
        const int rawWidth  = referenceRawBuffer->width / 2;
        const int rawHeight = referenceRawBuffer->height / 2;

        const int T = pow(2, EXTEND_EDGE_AMOUNT);
        
        const int offsetX = static_cast<int>(T * ceil(rawWidth / (double) T) - rawWidth);
        const int offsetY = static_cast<int>(T * ceil(rawHeight / (double) T) - rawHeight);

        // Check if we should write a DNG file
        if(rawContainer.getPostProcessSettings().dng) {
            std::vector<cv::Mat> rawChannels;
            rawChannels.reserve(4);

            for(int i = 0; i < 4; i++) {
                rawChannels.emplace_back(denoiseOutput[i].height(), denoiseOutput[i].width(), CV_16U, denoiseOutput[i].data());
            }

            cv::Mat rawImage = util::BuildRawImage(rawChannels, offsetX, offsetY);
            
            size_t extensionStartIdx = outputPath.find_last_of('.');
            std::string rawOutputPath;
            
            if(extensionStartIdx != std::string::npos) {
                rawOutputPath = outputPath.substr(0, extensionStartIdx);
            }
            else {
                rawOutputPath = outputPath;
            }
            
            // Update the black/white levels before writing DNG
            RawCameraMetadata metadata = rawContainer.getCameraMetadata();
            
            metadata.blackLevel[0] = 0;
            metadata.blackLevel[1] = 1;
            metadata.blackLevel[2] = 2;
            metadata.blackLevel[3] = 3;
            
            metadata.whiteLevel = EXPANDED_RANGE;
            
            std::string dngFile = rawOutputPath + ".dng";
            
            try {
                util::WriteDng(rawImage, metadata, referenceRawBuffer->metadata, dngFile);
            }
            catch(std::runtime_error& e) {
            }
        }
        
        cv::Mat outputImage = postProcess(
            denoiseOutput,
            hdrMetadata,
            offsetX,
            offsetY,
            noise,
            referenceRawBuffer->metadata,
            rawContainer.getCameraMetadata(),
            settings);
        
        progressHelper.postProcessCompleted();
         
        // Write image
        std::vector<int> writeParams = { cv::IMWRITE_JPEG_QUALITY, rawContainer.getPostProcessSettings().jpegQuality };
        cv::imwrite(outputPath, outputImage, writeParams);

        // Create thumbnail
        cv::Mat thumbnail;

        int width = 320;
        int height = (int) std::lround((outputImage.rows / (double) outputImage.cols) * width);

        cv::resize(outputImage, thumbnail, cv::Size(width, height));

        // Add exif data to the output image
        auto exifMetadata = referenceRawBuffer->metadata;

        addExifMetadata(exifMetadata,
                        thumbnail,
                        rawContainer.getCameraMetadata(),
                        rawContainer.getPostProcessSettings(),
                        outputPath);
        
        progressHelper.imageSaved();
    }

    void ImageProcessor::process(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ImageProcessorProgress& progressListener)
    {
        Measure measure("process()");

        // Open RAW container
        RawContainer rawContainer(inputPath);

        if(rawContainer.getFrames().empty()) {
            progressListener.onError("No frames found");
            return;
        }
        
        process(rawContainer, outputPath, progressListener);
    }

    float ImageProcessor::adjustShadowsForFaces(cv::Mat input, PreviewMetadata& metadata) {
        return 1.0f;
                
//        if(metadata.faces.empty())
//            return 1.0f;
//
//        cv::Mat gray;
//        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
//
//        float L = 0;
//        float n = 0;
//
//        for(auto f : metadata.faces) {
//            float p = f.area() / (input.cols*input.rows);
//            // Ignore faces which are too small
//            if(p >= 0.00083f) {
//                auto m = cv::mean(gray(f & cv::Rect(0, 0, input.cols, input.rows)));
//
//                L += m[0];
//                n += 1;
//            }
//        }
//
//        if(n <= 0)
//            return 1;
//
//        L = L / n;
//        return std::min(4.0f, std::max(1.0f, 128.0f / L));
    }
    
    void ImageProcessor::addExifMetadata(const RawImageMetadata& metadata,
                                         const cv::Mat& thumbnail,
                                         const RawCameraMetadata& cameraMetadata,
                                         const PostProcessSettings& settings,
                                         const std::string& inputOutput)
    {
        auto image = Exiv2::ImageFactory::open(inputOutput);
        if(image.get() == nullptr)
            return;
        
        image->readMetadata();
        
        Exiv2::ExifData& exifData = image->exifData();
        
        // sRGB color space
        exifData["Exif.Photo.ColorSpace"]       = uint16_t(1);
        
        // Capture settings
        exifData["Exif.Photo.ISOSpeedRatings"]  = uint16_t(metadata.iso);
        exifData["Exif.Photo.ExposureTime"]     = Exiv2::floatToRationalCast(metadata.exposureTime / ((float) 1e9));
        
        switch(metadata.screenOrientation)
        {
            default:
            case ScreenOrientation::LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(2) : uint16_t(1);
                break;
                
            case ScreenOrientation::PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(5) : uint16_t(6);
                break;
                                
            case ScreenOrientation::REVERSE_LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(4) : uint16_t(3);
                break;
                
            case ScreenOrientation::REVERSE_PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(7) : uint16_t(8);
                break;
        }
                
        if(!cameraMetadata.apertures.empty())
            exifData["Exif.Photo.ApertureValue"] = Exiv2::floatToRationalCast(cameraMetadata.apertures[0]);

        if(!cameraMetadata.focalLengths.empty())
            exifData["Exif.Photo.FocalLength"] = Exiv2::floatToRationalCast(cameraMetadata.focalLengths[0]);
        
        // Misc bits
        exifData["Exif.Photo.LensModel"]   = "MotionCam";
        exifData["Exif.Photo.LensMake"]    = "MotionCam";
        
        exifData["Exif.Photo.SceneType"]    = uint8_t(1);
        exifData["Exif.Image.XResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Image.YResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Photo.WhiteBalance"] = uint8_t(0);
        
        // Store GPS coords
        if(!settings.gpsTime.empty()) {
            exifData["Exif.GPSInfo.GPSProcessingMethod"]    = "65 83 67 73 73 0 0 0 72 89 66 82 73 68 45 70 73 88"; // ASCII HYBRID-FIX
            exifData["Exif.GPSInfo.GPSVersionID"]           = "2 2 0 0";
            exifData["Exif.GPSInfo.GPSMapDatum"]            = "WGS-84";
            
            exifData["Exif.GPSInfo.GPSLatitude"]            = toExifString(settings.gpsLatitude, true, true);
            exifData["Exif.GPSInfo.GPSLatitudeRef"]         = settings.gpsLatitude > 0 ? "N" : "S";

            exifData["Exif.GPSInfo.GPSLongitude"]           = toExifString(settings.gpsLongitude, true, false);
            exifData["Exif.GPSInfo.GPSLongitudeRef"]        = settings.gpsLongitude > 0 ? "E" : "W";
            
            exifData["Exif.GPSInfo.GPSAltitude"]            = toExifString(settings.gpsAltitude);
            exifData["Exif.GPSInfo.GPSAltitudeRef"]         = settings.gpsAltitude < 0.0 ? "1" : "0";
            
            exifData["Exif.Image.GPSTag"]                   = 4908;
        }
        
        // Set thumbnail
        if(!thumbnail.empty()) {
            Exiv2::ExifThumb exifThumb(exifData);
            std::vector<uint8_t> thumbnailBuffer;
            
            cv::imencode(".jpg", thumbnail, thumbnailBuffer);
            
            exifThumb.setJpegThumbnail(thumbnailBuffer.data(), thumbnailBuffer.size());
        }
        
        image->writeMetadata();
    }

    double ImageProcessor::measureSharpness(const RawImageBuffer& rawBuffer) {
        //Measure measure("measureSharpness()");
        
        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        Halide::Runtime::Buffer<uint16_t> outputBuffer(halfWidth, halfHeight);
                
        generate_edges(inputBufferContext.getHalideBuffer(),
                       rawBuffer.rowStride,
                       static_cast<int>(rawBuffer.pixelFormat),
                       halfWidth,
                       halfHeight,
                       outputBuffer);
        
        outputBuffer.device_sync();
        outputBuffer.copy_to_host();
        
        cv::Mat output(outputBuffer.height(), outputBuffer.width(), CV_16U, outputBuffer.data());
        
        cv::Scalar m, stddev;
        cv::meanStdDev(output, m, stddev);
        
        return m[0];
    }

    std::vector<Halide::Runtime::Buffer<uint16_t>> ImageProcessor::denoise(
        RawContainer& rawContainer,
        std::shared_ptr<RawImageBuffer> referenceRawBuffer,
        float* outNoise,
        ImageProgressHelper& progressHelper)
    {
        Measure measure("denoise()");
        
        //
        // Measure noise
        //
        
        int ev = (int) (0.5f + calcEv(rawContainer.getCameraMetadata(), referenceRawBuffer->metadata));
        int patchSize = ev < 8 ? 16 : 8;
        
        std::vector<float> noise, signal;
        
        measureNoise(*referenceRawBuffer, noise, signal, patchSize);
        
        float signalAverage = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();
        signalAverage /= rawContainer.getCameraMetadata().whiteLevel;
                
        Halide::Runtime::Buffer<float> thresholdBuffer(&noise[0], 4);

        //
        // Init
        //
        
        auto reference = loadRawImage(*referenceRawBuffer, rawContainer.getCameraMetadata());
        
        std::vector<Halide::Runtime::Buffer<uint16_t>> result;
        
        cv::Mat referenceFlowImage(reference->previewBuffer.height(), reference->previewBuffer.width(), CV_8U, reference->previewBuffer.data());
        Halide::Runtime::Buffer<float> fuseOutput(reference->rawBuffer.width(), reference->rawBuffer.height(), 4);
        
        fuseOutput.fill(0);
        
        auto processFrames = rawContainer.getFrames();
        auto it = processFrames.begin();

        float w = 1.0f/(2.0f*sqrt(2.0f));
        auto method = &fuse_denoise_3x3;
        
        if(signalAverage < 0.02f) {
            method = &fuse_denoise_7x7;
        }
        else if(signalAverage < 0.04f) {
            method = &fuse_denoise_5x5;
        }
        else {
            method = &fuse_denoise_3x3;
        }
        
        //
        // Fuse
        //
                
        while(it != processFrames.end()) {
            if(rawContainer.getReferenceImage() == *it) {
                ++it;
                continue;
            }
            
            auto frame = rawContainer.loadFrame(*it);
            auto current = loadRawImage(*frame, rawContainer.getCameraMetadata());
            
            cv::Mat flow;
            cv::Mat currentFlowImage(current->previewBuffer.height(),
                                     current->previewBuffer.width(),
                                     CV_8U,
                                     current->previewBuffer.data());
            
            cv::Ptr<cv::DISOpticalFlow> opticalFlow =
                cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_ULTRAFAST);
                                
            opticalFlow->setPatchSize(patchSize);
            opticalFlow->setPatchStride(patchSize/2);
            opticalFlow->setGradientDescentIterations(16);
            opticalFlow->setUseMeanNormalization(true);
            opticalFlow->setUseSpatialPropagation(true);
            
            opticalFlow->calc(referenceFlowImage, currentFlowImage, flow);
            
            Halide::Runtime::Buffer<float> flowBuffer =
                Halide::Runtime::Buffer<float>::make_interleaved((float*) flow.data, flow.cols, flow.rows, 2);
            
            auto flowMean = cv::mean(flow);
            
            method(
                reference->rawBuffer,
                current->rawBuffer,
                fuseOutput,
                flowBuffer,
                thresholdBuffer,
                reference->rawBuffer.width(),
                reference->rawBuffer.height(),
                w,
                flowMean[0],
                flowMean[1],
                fuseOutput);
            
            progressHelper.nextFusedImage();

            frame->data->release();
            
            ++it;
        }
        
        const int width = reference->rawBuffer.width();
        const int height = reference->rawBuffer.height();

        Halide::Runtime::Buffer<uint16_t> denoiseInput(width, height, 4);
        
        if(processFrames.size() <= 1)
            denoiseInput.for_each_element([&](int x, int y, int c) {
                float p = reference->rawBuffer(x, y, c) - rawContainer.getCameraMetadata().blackLevel[c];
                float s = EXPANDED_RANGE / (float) (rawContainer.getCameraMetadata().whiteLevel-rawContainer.getCameraMetadata().blackLevel[c]);
                
                denoiseInput(x, y, c) = static_cast<uint16_t>( std::max(0.0f, std::min(p * s + 0.5f, (float) EXPANDED_RANGE) )) ;
            });
        else {
            const float n = (float) processFrames.size() - 1;

            denoiseInput.for_each_element([&](int x, int y, int c) {
                float p = fuseOutput(x, y, c) / n - rawContainer.getCameraMetadata().blackLevel[c];
                float s = EXPANDED_RANGE / (float) (rawContainer.getCameraMetadata().whiteLevel-rawContainer.getCameraMetadata().blackLevel[c]);
                
                denoiseInput(x, y, c) = static_cast<uint16_t>( std::max(0.0f, std::min(p * s + 0.5f, (float) EXPANDED_RANGE) ) ) ;
            });
        }
        
        // Don't need this anymore
        reference->rawBuffer = Halide::Runtime::Buffer<uint16_t>();

        //
        // Spatial denoising
        //

        std::vector<Halide::Runtime::Buffer<uint16_t>> denoiseOutput;
        
        auto wavelet = createWaveletBuffers(denoiseInput.width(), denoiseInput.height());

        std::vector<float> normalisedNoise;

        auto weights = estimateDenoiseWeights(signalAverage);
        auto weightsBuffer = Halide::Runtime::Buffer<float>(&weights[0], 4);

        for(int c = 0; c < 4; c++) {
            forward_transform(denoiseInput,
                              denoiseInput.width(),
                              denoiseInput.height(),
                              c,
                              wavelet[0],
                              wavelet[1],
                              wavelet[2],
                              wavelet[3]);

            int offset = wavelet[0].stride(2);

            cv::Mat ll(wavelet[0].height(), wavelet[0].width(), CV_32F, wavelet[0].data() + 4);
            cv::Mat hh(wavelet[0].height(), wavelet[0].width(), CV_32F, wavelet[0].data() + offset*7);
            
            float noiseSigma = estimateNoise(hh);
            float n = noiseSigma / (1e-5f + cv::mean(ll)[0]);
        
            normalisedNoise.push_back(n);
            
            Halide::Runtime::Buffer<uint16_t> outputBuffer(width, height);

            inverse_transform(wavelet[0],
                              wavelet[1],
                              wavelet[2],
                              wavelet[3],
                              noiseSigma,
                              false,
                              weightsBuffer,
                              outputBuffer);

            denoiseOutput.push_back(outputBuffer);
        }
        
        // Release RAW data
        referenceRawBuffer->data.reset();
        
        *outNoise = *std::max_element(normalisedNoise.begin(), normalisedNoise.end());
        
        return denoiseOutput;
    }

    float ImageProcessor::testAlignment(std::shared_ptr<RawData> refImage,
                                        std::shared_ptr<RawData> underexposedImage,
                                        const RawCameraMetadata& cameraMetadata,
                                        cv::Mat warpMatrix,
                                        float exposureScale) {
        Measure measure("testAlignment()");

        if(warpMatrix.empty())
            return 1e10f;

        Halide::Runtime::Buffer<uint8_t> ghostMapBuffer(refImage->rawBuffer.width(), refImage->rawBuffer.height());
        Halide::Runtime::Buffer<float> warpBuffer = ToHalideBuffer<float>(warpMatrix);

        hdr_mask(refImage->rawBuffer,
                 underexposedImage->rawBuffer,
                 warpBuffer,
                 cameraMetadata.blackLevel[0],
                 cameraMetadata.blackLevel[1],
                 cameraMetadata.blackLevel[2],
                 cameraMetadata.blackLevel[3],
                 cameraMetadata.whiteLevel,
                 1.0f,
                 exposureScale,
                 16.0f,
                 ghostMapBuffer);

        // Calculate error
        cv::Mat ghostMap(ghostMapBuffer.height(), ghostMapBuffer.width(), CV_8U, ghostMapBuffer.data());

        auto trimmedGhostMap = ghostMap(cv::Rect(32, 32, ghostMap.cols - 64, ghostMap.rows - 64));
        
        return cv::mean(trimmedGhostMap)[0];
    }

    std::shared_ptr<HdrMetadata> ImageProcessor::prepareHdr(const RawCameraMetadata& cameraMetadata,
                                                            const PostProcessSettings& settings,
                                                            const RawImageBuffer& reference,
                                                            const RawImageBuffer& underexposed)
    {
        Measure measure("prepareHdr()");
        
        // Match exposures
        float exposureScale;
                        
        auto a = calcEv(cameraMetadata, reference.metadata);
        auto b = calcEv(cameraMetadata, underexposed.metadata);

        exposureScale = std::pow(2.0f, std::abs(b - a));
                
        //
        // Register images
        //
        
        const bool extendEdges = true;

        auto refImage = loadRawImage(reference, cameraMetadata, extendEdges, 1.0);
        auto underexposedImage = loadRawImage(underexposed, cameraMetadata, extendEdges, exposureScale);

        // Try to register the image two different ways
        auto warpMatrix = registerImage2(refImage->previewBuffer, underexposedImage->previewBuffer);
        
        warpMatrix = warpMatrix.inv();
        warpMatrix.convertTo(warpMatrix, CV_32F);

        float error = testAlignment(refImage, underexposedImage, cameraMetadata, warpMatrix, exposureScale);

        logger::log("HDR error: " + std::to_string(error));
        if(error > MAX_HDR_ERROR)
            return nullptr;
                  
        //
        // Create the underexposed image
        //
        
        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;

        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, underexposed.metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, underexposed.metadata, underexposed.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        
        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;

        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(underexposedImage->metadata.lensShadingMap[i]);
        }
        
        Halide::Runtime::Buffer<float> colorTransformBuffer = ToHalideBuffer<float>(cameraToSrgb);
        Halide::Runtime::Buffer<uint16_t> outputBuffer(underexposedImage->rawBuffer.width()*2, underexposedImage->rawBuffer.height()*2, 3);
        Halide::Runtime::Buffer<float> warpBuffer = ToHalideBuffer<float>(warpMatrix);
                
        linear_image(underexposedImage->rawBuffer,
                     warpBuffer,
                     shadingMapBuffer[0],
                     shadingMapBuffer[1],
                     shadingMapBuffer[2],
                     shadingMapBuffer[3],
                     cameraWhite[0],
                     cameraWhite[1],
                     cameraWhite[2],
                     colorTransformBuffer,
                     underexposedImage->rawBuffer.width(),
                     underexposedImage->rawBuffer.height(),
                     static_cast<int>(cameraMetadata.sensorArrangment),
                     cameraMetadata.blackLevel[0],
                     cameraMetadata.blackLevel[1],
                     cameraMetadata.blackLevel[2],
                     cameraMetadata.blackLevel[3],
                     cameraMetadata.whiteLevel,
                     EXPANDED_RANGE,
                     outputBuffer);
        
        //
        // Shift image to the right if we've underexposed too much
        //

        cv::Mat RGB[3];

        RGB[0] = cv::Mat(outputBuffer.height(), outputBuffer.width(), CV_16U, outputBuffer.data() + 0*outputBuffer.stride(2));
        RGB[1] = cv::Mat(outputBuffer.height(), outputBuffer.width(), CV_16U, outputBuffer.data() + 1*outputBuffer.stride(2));
        RGB[2] = cv::Mat(outputBuffer.height(), outputBuffer.width(), CV_16U, outputBuffer.data() + 2*outputBuffer.stride(2));

        const vector<int> histBins      = { 1024 };
        const vector<float> histRange   = { 0, 65536 };
        const vector<int> channels      = { 0 };

        int p[3] = { 0, 0, 0 };

        for(int c = 0; c < 3; c++) {
            const vector<cv::Mat> inputImages = { RGB[c] };
            cv::Mat histogram;

            cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);

            histogram /= (outputBuffer.width()*outputBuffer.height());

            float sum = 0;

            for(int x = histogram.rows - 1; x >= 0; x--) {
                if( sum > 0.005f )
                    break;

                p[c] = x + 1;
                sum += histogram.at<float>(x);
            }
        }
        
        //
        // Return HDR metadata
        //
        
        auto hdrMetadata = std::make_shared<HdrMetadata>();
                
        hdrMetadata->exposureScale  = exposureScale;
        hdrMetadata->hdrInput       = outputBuffer;
        hdrMetadata->error          = 0;
        hdrMetadata->gain           = 1024.0f / std::max(p[2], std::max(p[0], p[1]));
        
        return hdrMetadata;
    }

    std::vector<cv::Rect2f> ImageProcessor::detectFaces(const RawImageBuffer& buffer, const RawCameraMetadata& cameraMetadata) {
        Measure measure("detectFaces()");
        
        NativeBufferContext inputBufferContext(*buffer.data, false);
        Halide::Runtime::Buffer<uint8_t> output;
        
        int scale = 4;
        
        if(buffer.metadata.screenOrientation == ScreenOrientation::LANDSCAPE ||
           buffer.metadata.screenOrientation == ScreenOrientation::REVERSE_LANDSCAPE)
        {
            output = Halide::Runtime::Buffer<uint8_t>(buffer.width/scale, buffer.height/scale);
        }
        else {
            output = Halide::Runtime::Buffer<uint8_t>(buffer.height/scale, buffer.width/scale);
        }
        
        int rotation = 0;
        switch(buffer.metadata.screenOrientation) {
            case ScreenOrientation::PORTRAIT:
                rotation = -90;
                break;
            case ScreenOrientation::REVERSE_PORTRAIT:
                rotation = 90;
                break;
            case ScreenOrientation::REVERSE_LANDSCAPE:
                rotation = 180;
                break;
                
            default:
            case ScreenOrientation::LANDSCAPE:
                rotation = 0;
        }
        
        fast_preview(inputBufferContext.getHalideBuffer(),
                     buffer.rowStride,
                     static_cast<int>(buffer.pixelFormat),
                     static_cast<int>(cameraMetadata.sensorArrangment),
                     buffer.width/scale,
                     buffer.height/scale,
                     rotation,
                     scale/2,
                     scale/2,
                     cameraMetadata.whiteLevel,
                     cameraMetadata.blackLevel[0],
                     cameraMetadata.blackLevel[1],
                     cameraMetadata.blackLevel[2],
                     cameraMetadata.blackLevel[3],
                     output);

        auto previewImage = cv::Mat(output.height(), output.width(), CV_8U, output.data());        
        cv::equalizeHist(previewImage, previewImage);

        static cv::CascadeClassifier c;
        
        if(c.empty()) {
            cv::FileStorage fs;
            auto classifier = cv::String(&lbpcascade_frontalface_improved_xml[0]);
            
            fs.open(classifier, cv::FileStorage::READ | cv::FileStorage::MEMORY);
            
            c.read(fs.getFirstTopLevelNode());
        }

        std::vector<cv::Rect> dets;
        std::vector<cv::Rect2f> result;
        
        c.detectMultiScale(previewImage, dets, 1.5);
        
        for(int i = 0; i < dets.size(); i++) {
            float x = dets[i].tl().x / (float) output.width();
            float y = dets[i].tl().y / (float) output.height();
            float width = dets[i].width / (float) output.width();
            float height = dets[i].height / (float) output.height();

            result.push_back(cv::Rect2f(x, y, width, height));
        }

//        static dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();
//
//        std::vector<dlib::rectangle> dets;
//        std::vector<cv::Rect2f> result;
//
//        dlib::array2d<dlib::uint8> dlibFrame;
//        dlib::assign_image(dlibFrame, dlib::cv_image<dlib::uint8>(previewImage));
//
//        dets = detector(dlibFrame);
//
//        for(int i = 0; i < dets.size(); i++) {
//            float left = dets[i].left() / (float) output.width();
//            float right = dets[i].right() / (float) output.width();
//            float top = dets[i].top() / (float) output.height();
//            float bottom = dets[i].bottom() / (float) output.height();
//
//            result.push_back(cv::Rect2f(left, top, right - left, bottom - top));
//        }

        return result;
    }
}
