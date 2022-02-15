#include "motioncam/RawImageBuffer.h"
#include "motioncam/Util.h"

namespace motioncam {

    RawImageBuffer::RawImageBuffer(const json11::Json metadata) :
        data(new NativeHostBuffer())
    {
        parse(metadata);
    }

    RawImageBuffer::RawImageBuffer(std::unique_ptr<NativeBuffer> buffer) :
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

    RawImageBuffer::RawImageBuffer() :
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

    RawImageBuffer::RawImageBuffer(const RawImageBuffer& other) :
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

    RawImageBuffer::RawImageBuffer(RawImageBuffer&& other) noexcept :
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

    RawImageBuffer& RawImageBuffer::operator=(const RawImageBuffer &obj) {
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

    void RawImageBuffer::shallowCopy(const RawImageBuffer &obj) {
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

    void RawImageBuffer::parse(const json11::Json& metadata) {
        this->width               = util::GetRequiredSettingAsInt(metadata, "width");
        this->height              = util::GetRequiredSettingAsInt(metadata, "height");
        this->originalWidth       = util::GetOptionalSetting(metadata, "originalWidth", this->width);
        this->originalHeight      = util::GetOptionalSetting(metadata, "originalHeight", this->height);
        this->isBinned            = util::GetOptionalSetting(metadata, "isBinned", false);
        this->rowStride           = util::GetRequiredSettingAsInt(metadata, "rowStride");
        this->isCompressed        = util::GetOptionalSetting(metadata, "isCompressed", false);
        this->compressionType     = static_cast<CompressionType>(util::GetOptionalSetting(metadata, "compressionType", 0));
                
        // Default to ZSTD if no compression type specified
        if(this->isCompressed && this->compressionType == CompressionType::UNCOMPRESSED) {
            this->compressionType = CompressionType::ZSTD;
        }
        
        std::string offset   = util::GetOptionalStringSetting(metadata, "offset", "0");
        this->offset       = stoll(offset);
        
        std::string pixelFormat = util::GetOptionalStringSetting(metadata, "pixelFormat", "raw10");

        if(pixelFormat == "raw16") {
            this->pixelFormat = PixelFormat::RAW16;
        }
        else if(pixelFormat == "raw12") {
            this->pixelFormat = PixelFormat::RAW12;
        }
        else if(pixelFormat == "yuv_420_888") {
            this->pixelFormat = PixelFormat::YUV_420_888;
        }
        else {
            // Default to RAW10
            this->pixelFormat = PixelFormat::RAW10;
        }

        this->metadata.exposureTime           = util::GetOptionalSetting(metadata, "exposureTime", 0);
        this->metadata.iso                    = util::GetOptionalSetting(metadata, "iso", 0);
        this->metadata.exposureCompensation   = util::GetOptionalSetting(metadata, "exposureCompensation", 0);
        this->metadata.screenOrientation      = static_cast<ScreenOrientation>(
            util::GetOptionalSetting(metadata, "orientation", static_cast<int>(ScreenOrientation::LANDSCAPE)));
        
        this->metadata.asShot               = util::toVec3f((metadata)["asShotNeutral"].array_items());
        
        std::string timestamp               = util::GetRequiredSettingAsString(metadata, "timestamp");
        this->metadata.timestampNs          = stoll(timestamp);

        if(metadata.object_items().find("colorMatrix1") != metadata.object_items().end()) {
            this->metadata.colorMatrix1 = util::toMat3x3((metadata)["colorMatrix1"].array_items());
        }

        if(metadata.object_items().find("colorMatrix2") != metadata.object_items().end()) {
            this->metadata.colorMatrix2 = util::toMat3x3((metadata)["colorMatrix2"].array_items());
        }

        if(metadata.object_items().find("calibrationMatrix1") != metadata.object_items().end()) {
            this->metadata.calibrationMatrix1 = util::toMat3x3((metadata)["calibrationMatrix1"].array_items());
        }

        if(metadata.object_items().find("calibrationMatrix2") != metadata.object_items().end()) {
            this->metadata.calibrationMatrix1 = util::toMat3x3((metadata)["calibrationMatrix2"].array_items());
        }

        if(metadata.object_items().find("forwardMatrix1") != metadata.object_items().end()) {
            this->metadata.calibrationMatrix1 = util::toMat3x3((metadata)["forwardMatrix1"].array_items());
        }

        if(metadata.object_items().find("forwardMatrix2") != metadata.object_items().end()) {
            this->metadata.calibrationMatrix1 = util::toMat3x3((metadata)["forwardMatrix2"].array_items());
        }
        
        // Dynamic black/white levels
        this->metadata.dynamicWhiteLevel = util::GetOptionalSetting(metadata, "dynamicWhiteLevel", 0.0f);
        
        if(metadata["dynamicBlackLevel"].is_array()) {
            auto arr = metadata["dynamicBlackLevel"].array_items();
            if(arr.size() == 4) {
                this->metadata.dynamicBlackLevel.resize(4);
                for(int c = 0; c < 4; c++)
                    this->metadata.dynamicBlackLevel[c] = arr[c].number_value();
            }
        }
        
        // Store shading map
        this->metadata.updateShadingMap(GetLensShadingMap(metadata));
    }

    void RawImageBuffer::toJson(json11::Json::object& metadata) const {
        metadata["timestamp"]       = std::to_string(this->metadata.timestampNs);
        metadata["filename"]        = std::to_string(this->metadata.timestampNs);
        metadata["width"]           = this->width;
        metadata["height"]          = this->height;
        metadata["originalWidth"]   = this->originalWidth;
        metadata["originalHeight"]  = this->originalHeight;
        metadata["isBinned"]        = this->isBinned;
        metadata["rowStride"]       = this->rowStride;
        metadata["offset"]          = std::to_string(this->offset);
        metadata["pixelFormat"]     = util::toString(this->pixelFormat);
        metadata["type"]            = util::toString(this->metadata.rawType);

        std::vector<float> asShot = {
            this->metadata.asShot[0],
            this->metadata.asShot[1],
            this->metadata.asShot[2],
        };

        metadata["asShotNeutral"]          = asShot;
        
        metadata["iso"]                    = this->metadata.iso;
        metadata["exposureCompensation"]   = this->metadata.exposureCompensation;
        metadata["exposureTime"]           = (double) this->metadata.exposureTime;
        metadata["orientation"]            = static_cast<int>(this->metadata.screenOrientation);
        metadata["isCompressed"]           = this->isCompressed;
        metadata["compressionType"]        = static_cast<int>(this->compressionType);

        if(!this->metadata.calibrationMatrix1.empty()) {
            metadata["calibrationMatrix1"]  = util::toJsonArray(this->metadata.calibrationMatrix1);
        }

        if(!this->metadata.calibrationMatrix2.empty()) {
            metadata["calibrationMatrix2"]  = util::toJsonArray(this->metadata.calibrationMatrix2);
        }

        if(!this->metadata.colorMatrix1.empty()) {
            metadata["colorMatrix1"]  = util::toJsonArray(this->metadata.colorMatrix1);
        }

        if(!this->metadata.colorMatrix2.empty()) {
            metadata["colorMatrix2"]  = util::toJsonArray(this->metadata.colorMatrix2);
        }

        if(!this->metadata.forwardMatrix1.empty()) {
            metadata["forwardMatrix1"]  = util::toJsonArray(this->metadata.forwardMatrix1);
        }

        if(!this->metadata.forwardMatrix2.empty()) {
            metadata["forwardMatrix2"]  = util::toJsonArray(this->metadata.forwardMatrix2);
        }
                
        if(this->metadata.dynamicWhiteLevel > 0)
            metadata["dynamicWhiteLevel"] = this->metadata.dynamicWhiteLevel;
        
        if(!this->metadata.dynamicBlackLevel.empty()) {
            metadata["dynamicBlackLevel"] = this->metadata.dynamicBlackLevel;
        }
        
        if(!this->metadata.shadingMap().empty()) {
            const auto& shadingMap = this->metadata.shadingMap();
            
            metadata["lensShadingMapWidth"]    = shadingMap[0].cols;
            metadata["lensShadingMapHeight"]   = shadingMap[0].rows;
        
            std::vector<std::vector<float>> points;
            
            for(auto& i : shadingMap) {
                std::vector<float> p;
                
                for(int y = 0; y < i.rows; y++) {
                    for(int x = 0; x < i.cols; x++) {
                        p.push_back(i.at<float>(y, x));
                    }
                }
                
                points.push_back(p);
            }
            
            metadata["lensShadingMap"] = std::move(points);
        }
        else {
            metadata["lensShadingMapWidth"] = 0;
            metadata["lensShadingMapHeight"] = 0;
        }
    }

    std::vector<cv::Mat> RawImageBuffer::GetLensShadingMap(const json11::Json& obj) {
        std::vector<cv::Mat> lensShadingMap;
        
        // Lens shading maps
        int lenShadingMapWidth  = util::GetOptionalSetting(obj, "lensShadingMapWidth", 0);
        int lenShadingMapHeight = util::GetOptionalSetting(obj, "lensShadingMapHeight", 0);

        // Make sure there are a reasonable number of points available
        if(lenShadingMapHeight > 4 && lenShadingMapWidth > 4) {
            for(int i = 0; i < 4; i++) {
                cv::Mat m(lenShadingMapHeight, lenShadingMapWidth, CV_32F, cv::Scalar(1));
                lensShadingMap.push_back(m);
            }

            // Load points for shading map
            auto shadingMapPts = (obj)["lensShadingMap"].array_items();

            if(shadingMapPts.size() == 4) {
                for(int i = 0; i < 4; i++) {
                    auto pts = shadingMapPts[i].array_items();

                    // Check number of points matches
                    if(pts.size() == lenShadingMapWidth * lenShadingMapHeight) {
                        for(int y = 0; y < lenShadingMapHeight; y++) {
                            for(int x = 0; x < lenShadingMapWidth; x++) {
                                lensShadingMap[i].at<float>(y, x) = pts[y * lenShadingMapWidth + x].number_value();
                            }
                        }
                    }
                }
            }
            else {
                if(shadingMapPts.size() == lenShadingMapWidth * lenShadingMapHeight * 4) {
                    for(int y = 0; y < lenShadingMapHeight * 4; y+=4) {
                        for(int x = 0; x < lenShadingMapWidth * 4; x+=4) {
                            lensShadingMap[0].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 0].number_value();
                            lensShadingMap[1].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 1].number_value();
                            lensShadingMap[2].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 2].number_value();
                            lensShadingMap[3].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 3].number_value();
                        }
                    }
                }
            }
        }

        // Fix shading map if all zeros
        for(size_t i = 0; i < lensShadingMap.size(); i++) {
            if(cv::sum(lensShadingMap[i])[0] < 1e-5f) {
                lensShadingMap[i].setTo(1.0f);
            }
        }
                
        return lensShadingMap;
    }
}
