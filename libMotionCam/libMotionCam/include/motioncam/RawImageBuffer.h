#ifndef RawImageBuffer_h
#define RawImageBuffer_h

#include "motioncam/Types.h"
#include "motioncam/NativeBuffer.h"
#include "motioncam/RawImageMetadata.h"

#include <stdint.h>
#include <memory>
#include <json11/json11.hpp>

namespace motioncam {
    struct RawImageBuffer {
        
        RawImageBuffer();
        RawImageBuffer(const json11::Json metadata);
        
        RawImageBuffer(std::unique_ptr<NativeBuffer> buffer);
        RawImageBuffer(const RawImageBuffer& other);
        RawImageBuffer(RawImageBuffer&& other) noexcept;

        RawImageBuffer& operator=(const RawImageBuffer &obj);
        
        void shallowCopy(const RawImageBuffer &obj);

        std::unique_ptr<NativeBuffer> data;
        RawImageMetadata metadata;
        PixelFormat pixelFormat;
        int width;
        int height;
        int originalWidth;
        int originalHeight;
        bool isBinned;
        int rowStride;
        bool isCompressed;
        CompressionType compressionType;
        uint64_t offset;
        
        void toJson(json11::Json::object& metadataJson) const;
        
        static std::vector<cv::Mat> GetLensShadingMap(const json11::Json& obj);
        
    private:
        void parse(const json11::Json& metadata);
    };
}

#endif /* RawImageBuffer_h */
