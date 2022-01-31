#ifndef RawImageMetadata_hpp
#define RawImageMetadata_hpp

#include "motioncam/Color.h"
#include "motioncam/Settings.h"

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace motioncam {
    enum class ColorFilterArrangment : int {
        RGGB = 0,
        GRBG,
        GBRG,
        BGGR,
        RGB,
        MONO
    };
    
    // This needs to match the generator input
    enum class PixelFormat : int {
        RAW10 = 0,
        RAW12,
        RAW16,
        YUV_420_888
    };

    enum class ScreenOrientation : int {
        PORTRAIT = 0,
        REVERSE_PORTRAIT,
        LANDSCAPE,
        REVERSE_LANDSCAPE
    };

    enum class RawType : int {
        ZSL,
        HDR
    };

    enum class CompressionType : int {
        UNCOMPRESSED,
        ZSTD,
        V8NZENC,
        P4NZENC,
        BITNZPACK,
        BITNZPACK_2
    };

    struct RawImageMetadata
    {
        RawImageMetadata() :
            dynamicWhiteLevel(0),
            exposureTime(0),
            iso(0),
            timestampNs(0),
            recvdTimestampMs(0),
            exposureCompensation(0),
            screenOrientation(ScreenOrientation::PORTRAIT),
            rawType(RawType::ZSL)
        {
        }

        RawImageMetadata(const RawImageMetadata& other) :
            dynamicBlackLevel(other.dynamicBlackLevel),
            dynamicWhiteLevel(other.dynamicWhiteLevel),
            asShot(other.asShot),
            lensShadingMap(other.lensShadingMap),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            recvdTimestampMs(other.recvdTimestampMs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType),
            noiseProfile(other.noiseProfile)
        {
        }

        RawImageMetadata(const RawImageMetadata&& other) noexcept :
            dynamicBlackLevel(std::move(other.dynamicBlackLevel)),
            dynamicWhiteLevel(other.dynamicWhiteLevel),
            asShot(std::move(other.asShot)),
            lensShadingMap(std::move(other.lensShadingMap)),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            recvdTimestampMs(other.recvdTimestampMs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType),
            noiseProfile(other.noiseProfile)
        {
        }

        RawImageMetadata& operator=(const RawImageMetadata &obj) {
            dynamicBlackLevel = obj.dynamicBlackLevel;
            dynamicWhiteLevel = obj.dynamicWhiteLevel;
            asShot = obj.asShot;
            lensShadingMap = obj.lensShadingMap;
            exposureTime = obj.exposureTime;
            iso = obj.iso;
            exposureCompensation = obj.exposureCompensation;
            timestampNs = obj.timestampNs;
            recvdTimestampMs = obj.recvdTimestampMs;
            screenOrientation = obj.screenOrientation;
            rawType = obj.rawType;
            noiseProfile = obj.noiseProfile;

            return *this;
        }

        std::vector<float> dynamicBlackLevel;
        float dynamicWhiteLevel;

        cv::Vec3f asShot;

        cv::Mat colorMatrix1;
        cv::Mat colorMatrix2;

        cv::Mat calibrationMatrix1;
        cv::Mat calibrationMatrix2;

        cv::Mat forwardMatrix1;
        cv::Mat forwardMatrix2;

        int64_t exposureTime;
        int32_t iso;
        int32_t exposureCompensation;
        int64_t timestampNs;
        int64_t recvdTimestampMs;
        ScreenOrientation screenOrientation;
        RawType rawType;
        std::vector<double> noiseProfile;
        
        void updateShadingMap(const std::vector<cv::Mat>& shadingMap) {
            this->lensShadingMap = std::move(shadingMap);
        }
        
        const std::vector<cv::Mat>& shadingMap() const {
            return lensShadingMap;
        }
        
    private:
        std::vector<cv::Mat> lensShadingMap;
    };

    class NativeBuffer {
    public:
        NativeBuffer() : mValidStart{0}, mValidEnd{0} {
            
        }
        virtual ~NativeBuffer() {}

        virtual uint8_t* lock(bool write) = 0;
        virtual void unlock() = 0;
        virtual uint64_t nativeHandle() = 0;
        virtual size_t len() = 0;
        virtual const std::vector<uint8_t>& hostData() = 0;
        virtual void copyHostData(const std::vector<uint8_t>& data) = 0;
        virtual void release() = 0;
        virtual std::unique_ptr<NativeBuffer> clone() = 0;
        virtual void shrink(size_t newSize) = 0;
        
        void setValidRange(size_t start, size_t end) {
            mValidStart = start;
            mValidEnd = end;
        }
        
        void getValidRange(size_t& outStart, size_t& outEnd) {
            if(mValidStart == mValidEnd) {
                outStart = 0;
                outEnd = len();
            }
            else {
                outStart = mValidStart;
                outEnd = mValidEnd;
            }
        }
        
    private:
        size_t mValidStart;
        size_t mValidEnd;
    };

    class NativeHostBuffer : public NativeBuffer {
    public:
        NativeHostBuffer()
        {
        }

        NativeHostBuffer(size_t length) : data(length)
        {
        }

        NativeHostBuffer(const std::vector<uint8_t>& other) : data(other)
        {
        }

        NativeHostBuffer(const uint8_t* other, size_t len)
        {
            data.resize(len);
            data.assign(other, other + len);
        }

        std::unique_ptr<NativeBuffer> clone() {
            return std::unique_ptr<NativeHostBuffer>(new NativeHostBuffer(data));
        }

        uint8_t* lock(bool write) {
            return data.data();
        }
        
        void unlock() {
        }
        
        uint64_t nativeHandle() {
            return 0;
        }
        
        size_t len() {
            return data.size();
        }
        
        void allocate(size_t len) {
            data.resize(len);
        }
        
        const std::vector<uint8_t>& hostData()
        {
            return data;
        }
        
        void copyHostData(const std::vector<uint8_t>& other)
        {
            data = std::move(other);
        }
        
        void release()
        {
            data.resize(0);
            data.shrink_to_fit();
        }

        void shrink(size_t newSize)
        {
            data.resize(newSize);
        }

    private:
        std::vector<uint8_t> data;
    };

    struct RawImageBuffer {        
        RawImageBuffer(std::unique_ptr<NativeBuffer> buffer) :
            data(std::move(buffer)),
            pixelFormat(PixelFormat::RAW10),
            width(0),
            height(0),
            originalWidth(0),
            originalHeight(0),
            isBinned(false),
            rowStride(0),
            isCompressed(false),
            compressionType(CompressionType::UNCOMPRESSED),
            offset(0)
        {
        }
        
        RawImageBuffer() :
            data(new NativeHostBuffer()),
            pixelFormat(PixelFormat::RAW10),
            width(0),
            height(0),
            originalWidth(0),
            originalHeight(0),
            isBinned(false),
            rowStride(0),
            isCompressed(false),
            compressionType(CompressionType::UNCOMPRESSED),
            offset(0)
        {
        }

        RawImageBuffer(const RawImageBuffer& other) :
            metadata(other.metadata),
            pixelFormat(other.pixelFormat),
            width(other.width),
            height(other.height),
            originalWidth(other.width),
            originalHeight(other.height),
            isBinned(other.isBinned),
            rowStride(other.rowStride),
            isCompressed(other.isCompressed),
            compressionType(other.compressionType),
            offset(other.offset)
        {
            data = other.data->clone();
        }
        
        RawImageBuffer(RawImageBuffer&& other) noexcept :
                data(std::move(other.data)),
                metadata(std::move(other.metadata)),
                pixelFormat(other.pixelFormat),
                width(other.width),
                height(other.height),
                originalWidth(other.width),
                originalHeight(other.height),
                isBinned(other.isBinned),
                rowStride(other.rowStride),
                isCompressed(other.isCompressed),
                compressionType(other.compressionType),
                offset(other.offset)
        {
        }

        RawImageBuffer& operator=(const RawImageBuffer &obj) {
            data = obj.data->clone();
            metadata = obj.metadata;
            pixelFormat = obj.pixelFormat;
            width = obj.width;
            height = obj.height;
            originalWidth = obj.originalWidth;
            originalHeight = obj.originalHeight;
            isBinned = obj.isBinned;
            rowStride = obj.rowStride;
            isCompressed = obj.isCompressed;
            compressionType = obj.compressionType;
            offset = obj.offset;
            
            return *this;
        }
        
        void shallowCopy(const RawImageBuffer &obj) {
            metadata = obj.metadata;
            pixelFormat = obj.pixelFormat;
            width = obj.width;
            height = obj.height;
            originalWidth = obj.originalWidth;
            originalHeight = obj.originalHeight;
            isBinned = obj.isBinned;
            rowStride = obj.rowStride;
            isCompressed = obj.isCompressed;
            compressionType = obj.compressionType;
            offset = obj.offset;
        }

        std::unique_ptr<NativeBuffer> data;
        RawImageMetadata metadata;
        PixelFormat pixelFormat;
        int32_t width;
        int32_t height;
        int32_t originalWidth;
        int32_t originalHeight;
        bool isBinned;
        int32_t rowStride;
        bool isCompressed;
        CompressionType compressionType;
        uint64_t offset;
    };

    struct RawCameraMetadata {
        RawCameraMetadata() :
            sensorArrangment(ColorFilterArrangment::RGGB),
            colorIlluminant1(color::StandardA),
            colorIlluminant2(color::D50),
            whiteLevel(0)
        {
        }
        
        ColorFilterArrangment sensorArrangment;

        cv::Mat colorMatrix1;
        cv::Mat colorMatrix2;
        
        cv::Mat calibrationMatrix1;
        cv::Mat calibrationMatrix2;

        cv::Mat forwardMatrix1;
        cv::Mat forwardMatrix2;

        color::Illuminant colorIlluminant1;
        color::Illuminant colorIlluminant2;
      
        std::vector<float> apertures;
        std::vector<float> focalLengths;
        
        const std::vector<float>& getBlackLevel(const RawImageMetadata& bufferMetadata) const {
            return bufferMetadata.dynamicBlackLevel.empty() ? this->blackLevel : bufferMetadata.dynamicBlackLevel;
        }
        
        const std::vector<float>& getBlackLevel() const {
            return blackLevel;
        }
        
        const float getWhiteLevel(const RawImageMetadata& bufferMetadata) const {
            return bufferMetadata.dynamicWhiteLevel <= 0 ? this->whiteLevel : bufferMetadata.dynamicWhiteLevel;
        }
        
        const float getWhiteLevel() const {
            return whiteLevel;
        }
        
        void updateBayerOffsets(const std::vector<float>& blackLevel, const float whiteLevel) {
            this->blackLevel = blackLevel;
            this->whiteLevel = whiteLevel;
        }
        
    private:
        float whiteLevel;
        std::vector<float> blackLevel;
    };
}
#endif /* RawImageMetadata_hpp */
