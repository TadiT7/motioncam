#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"

#include <zstd.h>
#include <utility>
#include <vector>
#include <vint.h>

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
    #include <unistd.h>
    #include <arpa/inet.h>
#else
    #include <WinSock2.h>
#endif

using std::string;
using std::vector;
using std::shared_ptr;
using json11::Json;

#if defined(_WIN32)
    #define FSEEK _fseeki64
#else
    #define FSEEK fseek
#endif

namespace motioncam {
    static const char* METATDATA_FILENAME = "metadata";

    static const uint8_t CONTAINER_VERSION = 1;
    static const uint8_t CONTAINER_HEADER[7] = {'M', 'O', 'T', 'I', 'O', 'N', ' '};

    static const uint16_t FRAME_CHUNK = 0x01;
    static const uint16_t END_CHUNK = 0x02;

    struct Header {
        uint8_t id[7] = {'M', 'O', 'T', 'I', 'O', 'N', ' '};
        uint8_t version = { CONTAINER_VERSION };
    };

    struct FrameChunk {
        uint8_t chunkId[2] = { 0x00, FRAME_CHUNK };
        uint8_t reserved[2] = { 0x00, 0x00 };
        uint32_t frameSize{0};
    };

    struct EndChunk {
        uint8_t chunkId[2] = { 0x00, END_CHUNK };
        uint8_t reserved[2] = { 0x00, 0x00 };
        uint32_t metadataMinusOffset{0};
    };

    json11::Json::array RawContainer::toJsonArray(cv::Mat m) {
        assert(m.type() == CV_32F);

        json11::Json::array result;

        for(int y = 0; y < m.rows; y++) {
            for(int x = 0; x< m.cols; x++) {
                result.push_back(m.at<float>(y, x));
            }
        }

        return result;
    }

    cv::Vec3f RawContainer::toVec3f(const vector<Json>& array) {
        if(array.size() != 3) {
            throw InvalidState("Can't convert to vector. Invalid number of items.");
        }

        cv::Vec3f result;

        result[0] = array[0].number_value();
        result[1] = array[1].number_value();
        result[2] = array[2].number_value();

        return result;
    }

    cv::Mat RawContainer::toMat3x3(const vector<Json>& array) {
        if(array.size() < 9)
            return cv::Mat();
        
        cv::Mat mat(3, 3, CV_32F);
        cv::setIdentity(mat);

        auto* data = mat.ptr<float>(0);

        data[0] = array[0].number_value();
        data[1] = array[1].number_value();
        data[2] = array[2].number_value();

        data[3] = array[3].number_value();
        data[4] = array[4].number_value();
        data[5] = array[5].number_value();

        data[6] = array[6].number_value();
        data[7] = array[7].number_value();
        data[8] = array[8].number_value();

        return mat;
    }

    string RawContainer::toString(RawType rawType) {
        switch (rawType) {
            case RawType::HDR:
                return "HDR";

            default:
            case RawType::ZSL:
                return "ZSL";
        }
    }
    
    string RawContainer::toString(PixelFormat format) {
        switch(format) {
            case PixelFormat::RAW12:
                return "raw12";
            
            case PixelFormat::RAW16:
                return "raw16";

            case PixelFormat::YUV_420_888:
                return "yuv_420_888";

            default:
            case PixelFormat::RAW10:
                return "raw10";
        }
    }

    string RawContainer::toString(ColorFilterArrangment sensorArrangment) {
        switch(sensorArrangment) {
            case ColorFilterArrangment::GRBG:
                return "grbg";

            case ColorFilterArrangment::GBRG:
                return "gbrg";

            case ColorFilterArrangment::BGGR:
                return "bggr";

            case ColorFilterArrangment::RGB:
                return "rgb";

            case ColorFilterArrangment::MONO:
                return "mono";

            default:
            case ColorFilterArrangment::RGGB:
                return "rggb";
        }
    }

    int RawContainer::getOptionalSetting(const json11::Json& json, const string& key, const int defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_number())
            return defaultValue;
        
        return json[key].int_value();
    }

    bool RawContainer::getOptionalSetting(const json11::Json& json, const string& key, const bool defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_bool())
            return defaultValue;
        
        return json[key].bool_value();
    }

    string RawContainer::getOptionalStringSetting(const json11::Json& json, const string& key, const string& defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_string())
            return defaultValue;
        
        return json[key].string_value();
    }

    int RawContainer::getRequiredSettingAsInt(const json11::Json& json, const string& key) {
        if(json.object_items().find(key) == json.object_items().end() || !json[key].is_number()) {
            throw InvalidState("Invalid metadata. Missing " + key);
        }
        
        return json[key].int_value();
    }

    string RawContainer::getRequiredSettingAsString(const json11::Json& json, const string& key) {
        if(json.object_items().find(key) == json.object_items().end() || !json[key].is_string()) {
            throw InvalidState("Invalid metadata. Missing " + key);
        }
        
        return json[key].string_value();
    }

    RawContainer::RawContainer(const int fd) :
        mReferenceTimestamp(-1),
        mIsHdr(false),
        mIsInMemory(false),
        mFile(nullptr),
        mNumSegments(0)
    {
        initialise(fd);
    }

    RawContainer::RawContainer(const string& inputPath) :
        mReferenceTimestamp(-1),
        mIsHdr(false),
        mIsInMemory(false),
        mFile(nullptr),
        mNumSegments(0)
    {
        initialise(inputPath);
    }

    RawContainer::RawContainer(const RawCameraMetadata& cameraMetadata, const int numSegments) :
        mCameraMetadata(cameraMetadata),
        mIsHdr(false),
        mIsInMemory(false),
        mFile(nullptr),
        mNumSegments(numSegments)
    {
        // Empty container for streaming
    }

    RawContainer::RawContainer(const RawCameraMetadata& cameraMetadata,
                               const PostProcessSettings& postProcessSettings,
                               const int64_t referenceTimestamp,
                               const bool isHdr,
                               const vector<shared_ptr<RawImageBuffer>>& buffers) :
        mCameraMetadata(cameraMetadata),
        mPostProcessSettings(postProcessSettings),
        mReferenceTimestamp(referenceTimestamp),
        mIsHdr(isHdr),
        mIsInMemory(true),
        mFile(nullptr),
        mNumSegments(0)
    {
        if(buffers.empty()) {
            throw InvalidState("No buffers");
        }

        // Clone buffers
        int filenameIdx = 0;

        for(const auto& buffer : buffers) {
            string filename = "frame" + std::to_string(filenameIdx) + ".raw";

            mFrameBuffers[filename] = std::make_shared<RawImageBuffer>(*buffer);
            mFrames.push_back(filename);
            
            if(buffer->metadata.timestampNs == referenceTimestamp) {
                mReferenceImage = filename;
            }

            ++filenameIdx;
        }
        
        if(mReferenceImage.empty()) {
            mReferenceImage = mFrameBuffers.begin()->first;
            mReferenceTimestamp = mFrameBuffers.begin()->second->metadata.timestampNs;
        }
    }

    RawContainer::~RawContainer() {
        if(mFile) {
            fclose(mFile);
        }
        mFile = nullptr;
    }

    void RawContainer::save(const int fd) {
        util::ZipWriter writer(fd);
        
        save(writer);
        
        writer.commit();
    }

    void RawContainer::save(const string& outputPath) {
        util::ZipWriter writer(outputPath);
        
        save(writer);
        
        writer.commit();
    }

    void RawContainer::save(util::ZipWriter& writer) {
        json11::Json::object metadata;
        
        generateContainerMetadata(metadata);
                
        // We'll compress the data
        for(auto& filename : mFrames) {
            auto frame = mFrameBuffers[filename];
            
            writer.addFile(filename, frame->data->hostData(), frame->data->len());
        }

        string jsonOutput = json11::Json(metadata).dump();

        writer.addFile(METATDATA_FILENAME, jsonOutput);
    }

    void RawContainer::loadFromBin(FILE* file) {
        mFile = file;
        if(!mFile)
            throw IOException("Failed to open");

        // Verify header
        Header header;
        
        if(fread(&header, sizeof(Header), 1, mFile) != 1)
            throw IOException("Failed to reader container header");
        
        if(header.version != CONTAINER_VERSION &&
           memcmp(header.id, CONTAINER_HEADER, sizeof(CONTAINER_HEADER)) != 0)
        {
            throw IOException("Failed to verify container header");
        }
        
        // Load metadata
        if(FSEEK(mFile, -sizeof(EndChunk), SEEK_END) != 0) {
            throw IOException("Failed to get end chunk");
        }
        
        EndChunk endChunk;
        
        if(fread(&endChunk, sizeof(EndChunk), 1, mFile) != 1)
            throw IOException("Can't read end chunk");
        
        if(endChunk.chunkId[1] != END_CHUNK)
            throw IOException("Invalid end chunk");
        
        endChunk.metadataMinusOffset = ntohl(endChunk.metadataMinusOffset) + sizeof(EndChunk);
        
        if(FSEEK(mFile, -(int)endChunk.metadataMinusOffset, SEEK_CUR) != 0)
            throw IOException("Failed to get metadata");
        
        std::vector<uint8_t> metadataBytes(endChunk.metadataMinusOffset - sizeof(EndChunk));
        
        if(fread(metadataBytes.data(), metadataBytes.size(), 1, mFile) != 1)
            throw IOException("Failed to read metadata");
        
        std::string metadata(metadataBytes.begin(), metadataBytes.end());

        std::string err;
        json11::Json containerMetadata = json11::Json::parse(metadata, err);
        
        if(!err.empty()) {
            throw IOException("Cannot parse metadata");
        }

        loadContainerMetadata(containerMetadata);
    }

    void RawContainer::loadFromZip(std::unique_ptr<util::ZipReader> zipReader) {
        string jsonStr, err;

        if(!zipReader)
            throw IOException("Invalid ZIP reader");
        
        mZipReader = std::move(zipReader);
        mZipReader->read(METATDATA_FILENAME, jsonStr);
        
        json11::Json containerMetadata = json11::Json::parse(jsonStr, err);
        if(!err.empty()) {
            throw IOException("Cannot parse metadata");
        }
        
        loadContainerMetadata(containerMetadata);
        
        // Add files that were streamed if there were no frames in the metadata
        if(mFrames.empty()) {
            auto& files = mZipReader->getFiles();
            
            for(int i = 0; i < files.size(); i++) {
                size_t p = files[i].find_last_of(".");
                if(p == string::npos)
                    continue;

                auto type = files[i].substr(p + 1, files[i].size() - 1);
                if(type != "metadata")
                    continue;

                string metadataStr;

                mZipReader->read(files[i], metadataStr);

                json11::Json frameJson = json11::Json::parse(metadataStr, err);
                if(!err.empty()) {
                    throw IOException("Cannot parse file metadata");
                }

                auto buffer = loadFrameMetadata(frameJson);
                string filename = getRequiredSettingAsString(frameJson, "filename");

                mFrames.push_back(filename);
                mFrameBuffers.insert(make_pair(filename, buffer));
            }
        }
    }

    void RawContainer::initialise(const int fd) {
        FILE* file = fdopen(fd, "rb");
        if(!file)
            throw IOException("Failed to open file descriptor");
        
        Header header;
        
        size_t result = fread(&header, sizeof(Header), 1, file);
        if(result != 1) {
            fclose(file);
            throw IOException("Failed to determine type of container");
        }
        
        // Reset position
        rewind(file);
        
        if(header.version == CONTAINER_VERSION &&
           memcmp(header.id, CONTAINER_HEADER, sizeof(CONTAINER_HEADER)) == 0)
        {
            loadFromBin(file);
        }
        else {
            auto zipReader = std::unique_ptr<util::ZipReader>(new util::ZipReader(file));
            loadFromZip(std::move(zipReader));
        }
        
        if(mReferenceImage.empty() && !mFrames.empty()) {
            mReferenceImage = *mFrames.begin();
        }
    }

    void RawContainer::initialise(const std::string& inputPath) {        
        if(util::EndsWith(inputPath, "zip")) {
            auto zipReader = std::unique_ptr<util::ZipReader>(new util::ZipReader(inputPath));
            loadFromZip(std::move(zipReader));
        }
        else if(util::EndsWith(inputPath, "container")) {
            FILE* file = fopen(inputPath.c_str(), "rb");
            if(!file)
                throw IOException("Failed to open container");
            
            loadFromBin(file);
        }
        else {
            throw IOException("Invalid file type");
        }
        
        if(mReferenceImage.empty() && !mFrames.empty()) {
            mReferenceImage = *mFrames.begin();
        }
    }

    void RawContainer::loadContainerMetadata(const json11::Json& metadata) {
        // Load post process settings if available
        if(metadata["postProcessingSettings"].is_object()) {
            mPostProcessSettings = PostProcessSettings(metadata["postProcessingSettings"]);
        }
        
        mReferenceTimestamp = stoll(getOptionalStringSetting(metadata, "referenceTimestamp", "0"));
        mIsHdr = getOptionalSetting(metadata, "isHdr", false);
        mNumSegments = getOptionalSetting(metadata, "numSegments", 1);

        // Black/white levels
        vector<Json> blackLevelValues = metadata["blackLevel"].array_items();
        for(auto& blackLevelValue : blackLevelValues) {
            mCameraMetadata.blackLevel.push_back(blackLevelValue.number_value());
        }
        
        mCameraMetadata.whiteLevel = getRequiredSettingAsInt(metadata, "whiteLevel");
        
        // Default to 64
        if(mCameraMetadata.blackLevel.empty()) {
            for(int i = 0; i < 4; i++)
                mCameraMetadata.blackLevel.push_back(64);
        }

        // Default to 1023
        if(mCameraMetadata.whiteLevel <= 0)
            mCameraMetadata.whiteLevel = 1023;

        // Color arrangement
        string colorFilterArrangment = getRequiredSettingAsString(metadata, "sensorArrangment");

        if(colorFilterArrangment == "grbg") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::GRBG;
        }
        else if(colorFilterArrangment == "gbrg") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::GBRG;
        }
        else if(colorFilterArrangment == "bggr") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::BGGR;
        }
        else if(colorFilterArrangment == "rgb") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::RGB;
        }
        else if(colorFilterArrangment == "mono") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::MONO;
        }
        else {
            // Default to RGGB
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::RGGB;
        }
        
        // Matrices
        mCameraMetadata.colorIlluminant1 = color::IlluminantFromString(metadata["colorIlluminant1"].string_value());
        mCameraMetadata.colorIlluminant2 = color::IlluminantFromString(metadata["colorIlluminant2"].string_value());

        mCameraMetadata.colorMatrix1 = toMat3x3(metadata["colorMatrix1"].array_items());
        mCameraMetadata.colorMatrix2 = toMat3x3(metadata["colorMatrix2"].array_items());

        mCameraMetadata.calibrationMatrix1 = toMat3x3(metadata["calibrationMatrix1"].array_items());
        mCameraMetadata.calibrationMatrix2 = toMat3x3(metadata["calibrationMatrix2"].array_items());

        mCameraMetadata.forwardMatrix1 = toMat3x3(metadata["forwardMatrix1"].array_items());
        mCameraMetadata.forwardMatrix2 = toMat3x3(metadata["forwardMatrix2"].array_items());

        // Misc
        if(metadata["apertures"].is_array()) {
            for(int i = 0; i < metadata["apertures"].array_items().size(); i++)
                mCameraMetadata.apertures.push_back(metadata["apertures"].array_items().at(i).number_value());
        }

        if(metadata["focalLengths"].is_array()) {
            for(int i = 0; i < metadata["focalLengths"].array_items().size(); i++)
                mCameraMetadata.focalLengths.push_back(metadata["focalLengths"].array_items().at(i).number_value());
        }
        
        // Add the frames
        Json frames = metadata["frames"];
        if(!frames.is_array()) {
            throw IOException("No frames found in metadata");
        }
        
        // Add all frame metadata to a list
        vector<Json> frameList = frames.array_items();
        vector<Json>::const_iterator it = frameList.begin();
        
        while(it != frameList.end()) {
            auto buffer = loadFrameMetadata(*it);
            string filename = getRequiredSettingAsString(*it, "filename");

            // If this is the reference image, keep the name
            if(buffer->metadata.timestampNs == mReferenceTimestamp) {
                mReferenceImage = filename;
            }
            
            mFrames.push_back(filename);
            mFrameBuffers.insert(make_pair(filename, buffer));
            
            ++it;
        }
    }

    const RawCameraMetadata& RawContainer::getCameraMetadata() const {
        return mCameraMetadata;
    }

    const PostProcessSettings& RawContainer::getPostProcessSettings() const {
        return mPostProcessSettings;
    }

    bool RawContainer::isHdr() const {
        return mIsHdr;
    }

    string RawContainer::getReferenceImage() const {
        return mReferenceImage;
    }

    void RawContainer::updateReferenceImage(const string& referenceName) {
        mReferenceTimestamp = -1;
        mReferenceImage = referenceName;
    }

    vector<string> RawContainer::getFrames() const {
        return mFrames;
    }

    shared_ptr<RawImageBuffer> RawContainer::loadFrame(const string& frame) const {
        auto buffer = mFrameBuffers.find(frame);
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }
        
        // If we've already loaded the data, return it
        if(buffer->second->data->len() > 0)
            return buffer->second;
        
        // Load the data into the buffer
        vector<uint8_t> data;

        if(mZipReader) {
            mZipReader->read(frame, data);
        }
        else if(mFile) {
            int result = FSEEK(mFile, buffer->second->offset, SEEK_SET);

            if(result != 0)
                throw IOException("Cannot read " + frame + " in container");

            FrameChunk frameChunk;
            
            if(fread(&frameChunk, sizeof(frameChunk), 1, mFile) != 1) {
                throw IOException("Cannot read frame chunk header");
            }
            
            if(frameChunk.chunkId[1] != FRAME_CHUNK) {
                throw IOException("Invalid frame chunk id");
            }
            
            data.resize(frameChunk.frameSize);
            
            if(fread(data.data(), frameChunk.frameSize, 1, mFile) != 1) {
                throw IOException("Invalid frame chunk id");
            }
        }
        else {
            throw IOException("Cannot read frame chunk");
        }
        
        if(buffer->second->isCompressed) {
            if(buffer->second->compressionType == CompressionType::ZSTD) {
                vector<uint8_t> tmp;
                
                size_t outputSize = ZSTD_getFrameContentSize(static_cast<void*>(&data[0]), data.size());
                if( outputSize == ZSTD_CONTENTSIZE_UNKNOWN ||
                    outputSize == ZSTD_CONTENTSIZE_ERROR )
                {
                    // Invalid data
                    return nullptr;
                }

                tmp.resize(outputSize);
                
                long readBytes =
                    ZSTD_decompress(static_cast<void*>(&tmp[0]), tmp.size(), &data[0], data.size());
                
                tmp.resize(readBytes);
                
                buffer->second->data->copyHostData(tmp);
            }
            else if(buffer->second->compressionType == CompressionType::V8NZENC) {
                std::vector<uint16_t> row(buffer->second->width);
                std::vector<uint8_t> uncompressedBuffer(2*buffer->second->width*buffer->second->height);
                
                size_t offset = 0;
                size_t p = 0;
                
                while(offset < data.size()) {
                    offset += v8nzdec128v16(data.data() + offset, buffer->second->width, row.data());
                    
                    // Reshuffle the row
                    for(size_t i = 0; i < row.size()/2; i++) {
                        uncompressedBuffer[p]   = row[i];
                        uncompressedBuffer[p+1] = row[i] >> 8;

                        uncompressedBuffer[p+2] = row[i+row.size()/2];
                        uncompressedBuffer[p+3] = row[i+row.size()/2] >> 8;

                        p+=4;
                    }
                }
                
                buffer->second->data->copyHostData(uncompressedBuffer);
            }
            else {
                // Unknown compression type
                return nullptr;
            }
        }
        else {
            buffer->second->data->copyHostData(data);
        }
                
        return buffer->second;
    }

    shared_ptr<RawImageBuffer> RawContainer::getFrame(const string& frame) const {
        auto buffer = mFrameBuffers.find(frame);
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }
        
        return buffer->second;
    }

    void RawContainer::removeFrame(const string& frame) {
        auto it = find(mFrames.begin(), mFrames.end(), frame);
        if(it != mFrames.end()) {
            mFrames.erase(it);
        }
        
        auto bufferIt = mFrameBuffers.find(frame);
        if(bufferIt != mFrameBuffers.end())
            mFrameBuffers.erase(bufferIt);
    }

    shared_ptr<RawImageBuffer> RawContainer::loadFrameMetadata(const json11::Json& obj) {
        shared_ptr<RawImageBuffer> buffer = std::make_shared<RawImageBuffer>();
        
        buffer->width               = getRequiredSettingAsInt(obj, "width");
        buffer->height              = getRequiredSettingAsInt(obj, "height");
        buffer->rowStride           = getRequiredSettingAsInt(obj, "rowStride");
        buffer->isCompressed        = getOptionalSetting(obj, "isCompressed", false);
        buffer->compressionType     = static_cast<CompressionType>(getOptionalSetting(obj, "compressionType", 0));
        
        // Default to ZSTD if no compression type specified
        if(buffer->isCompressed && buffer->compressionType == CompressionType::UNCOMPRESSED) {
            buffer->compressionType = CompressionType::ZSTD;
        }
        
        std::string offset   = getOptionalStringSetting(obj, "offset", "0");
        buffer->offset       = stoll(offset);
        
        string pixelFormat = getOptionalStringSetting(obj, "pixelFormat", "raw10");

        if(pixelFormat == "raw16") {
            buffer->pixelFormat = PixelFormat::RAW16;
        }
        else if(pixelFormat == "raw12") {
            buffer->pixelFormat = PixelFormat::RAW12;
        }
        else if(pixelFormat == "yuv_420_888") {
            buffer->pixelFormat = PixelFormat::YUV_420_888;
        }
        else {
            // Default to RAW10
            buffer->pixelFormat = PixelFormat::RAW10;
        }

        buffer->metadata.exposureTime           = getOptionalSetting(obj, "exposureTime", 0);
        buffer->metadata.iso                    = getOptionalSetting(obj, "iso", 0);
        buffer->metadata.exposureCompensation   = getOptionalSetting(obj, "exposureCompensation", 0);
        buffer->metadata.screenOrientation      =
            static_cast<ScreenOrientation>(getOptionalSetting(obj, "orientation", static_cast<int>(ScreenOrientation::LANDSCAPE)));
        
        buffer->metadata.asShot             = toVec3f((obj)["asShotNeutral"].array_items());
        
        string timestamp                    = getRequiredSettingAsString(obj, "timestamp");
        buffer->metadata.timestampNs        = stoll(timestamp);

        if(obj.object_items().find("colorMatrix1") != obj.object_items().end()) {
            buffer->metadata.colorMatrix1 = toMat3x3((obj)["colorMatrix1"].array_items());
        }

        if(obj.object_items().find("colorMatrix2") != obj.object_items().end()) {
            buffer->metadata.colorMatrix2 = toMat3x3((obj)["colorMatrix2"].array_items());
        }

        if(obj.object_items().find("calibrationMatrix1") != obj.object_items().end()) {
            buffer->metadata.calibrationMatrix1 = toMat3x3((obj)["calibrationMatrix1"].array_items());
        }

        if(obj.object_items().find("calibrationMatrix2") != obj.object_items().end()) {
            buffer->metadata.calibrationMatrix1 = toMat3x3((obj)["calibrationMatrix2"].array_items());
        }

        if(obj.object_items().find("forwardMatrix1") != obj.object_items().end()) {
            buffer->metadata.calibrationMatrix1 = toMat3x3((obj)["forwardMatrix1"].array_items());
        }

        if(obj.object_items().find("forwardMatrix2") != obj.object_items().end()) {
            buffer->metadata.calibrationMatrix1 = toMat3x3((obj)["forwardMatrix2"].array_items());
        }
        
        // Lens shading maps
        int lenShadingMapWidth  = getOptionalSetting(obj, "lensShadingMapWidth", 0);
        int lenShadingMapHeight = getOptionalSetting(obj, "lensShadingMapHeight", 0);
        
        // Make sure there are a reasonable number of points available
        if(lenShadingMapHeight < 4 || lenShadingMapWidth < 4) {
            lenShadingMapWidth = 16;
            lenShadingMapHeight = 12;
        }
        
        for(int i = 0; i < 4; i++) {
            cv::Mat m(lenShadingMapHeight, lenShadingMapWidth, CV_32F, cv::Scalar(1));
            buffer->metadata.lensShadingMap.push_back(m);
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
                            buffer->metadata.lensShadingMap[i].at<float>(y, x) = pts[y * lenShadingMapWidth + x].number_value();
                        }
                    }
                }
            }
        }
        else {
            if(shadingMapPts.size() == lenShadingMapWidth * lenShadingMapHeight * 4) {
                for(int y = 0; y < lenShadingMapHeight * 4; y+=4) {
                    for(int x = 0; x < lenShadingMapWidth * 4; x+=4) {
                        buffer->metadata.lensShadingMap[0].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 0].number_value();
                        buffer->metadata.lensShadingMap[1].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 1].number_value();
                        buffer->metadata.lensShadingMap[2].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 2].number_value();
                        buffer->metadata.lensShadingMap[3].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 3].number_value();
                    }
                }
            }
        }
        
        return buffer;
    }

    void RawContainer::generateContainerMetadata(json11::Json::object& metadataJson) {
        // Save misc stuff
        metadataJson["referenceTimestamp"]  = std::to_string(mReferenceTimestamp);
        metadataJson["isHdr"]               = mIsHdr;
        metadataJson["numSegments"]         = mNumSegments;
        
        // Global camera metadata
        json11::Json::object postProcessSettings;
        
        mPostProcessSettings.toJson(postProcessSettings);
        
        metadataJson["colorIlluminant1"]    = color::IlluminantToString(mCameraMetadata.colorIlluminant1);
        metadataJson["colorIlluminant2"]    = color::IlluminantToString(mCameraMetadata.colorIlluminant2);
        metadataJson["forwardMatrix1"]      = toJsonArray(mCameraMetadata.forwardMatrix1);
        metadataJson["forwardMatrix2"]      = toJsonArray(mCameraMetadata.forwardMatrix2);
        metadataJson["colorMatrix1"]        = toJsonArray(mCameraMetadata.colorMatrix1);
        metadataJson["colorMatrix2"]        = toJsonArray(mCameraMetadata.colorMatrix2);
        metadataJson["calibrationMatrix1"]  = toJsonArray(mCameraMetadata.calibrationMatrix1);
        metadataJson["calibrationMatrix2"]  = toJsonArray(mCameraMetadata.calibrationMatrix2);
        metadataJson["blackLevel"]          = mCameraMetadata.blackLevel;
        metadataJson["whiteLevel"]          = mCameraMetadata.whiteLevel;
        metadataJson["sensorArrangment"]    = toString(mCameraMetadata.sensorArrangment);
        metadataJson["postProcessingSettings"] = postProcessSettings;
        metadataJson["apertures"]           = mCameraMetadata.apertures;
        metadataJson["focalLengths"]        = mCameraMetadata.focalLengths;
        
        json11::Json::array rawImages;
        
        // Write frames first
        auto it = mFrames.begin();

        while(it != mFrames.end()) {
            auto filename = *it;
            auto frameIt = mFrameBuffers.find(filename);
            if(frameIt == mFrameBuffers.end()) {
                throw InvalidState("Can't find buffer for " + filename);
            }
            
            auto frame = frameIt->second;
            
            json11::Json::object imageMetadata;

            generateMetadata(*frame, imageMetadata, filename);

            rawImages.push_back(imageMetadata);

            ++it;
        }
        
        // Write the metadata
        metadataJson["frames"] = rawImages;
    }

    void RawContainer::generateMetadata(const RawImageBuffer& frame, json11::Json::object& metadata, const string& filename) {
        metadata["timestamp"]   = std::to_string(frame.metadata.timestampNs);
        metadata["filename"]    = filename;
        metadata["width"]       = frame.width;
        metadata["height"]      = frame.height;
        metadata["rowStride"]   = frame.rowStride;
        metadata["offset"]      = std::to_string(frame.offset);
        metadata["pixelFormat"] = toString(frame.pixelFormat);
        metadata["type"]        = toString(frame.metadata.rawType);

        vector<float> asShot = {
            frame.metadata.asShot[0],
            frame.metadata.asShot[1],
            frame.metadata.asShot[2],
        };

        metadata["asShotNeutral"]          = asShot;
        
        metadata["iso"]                    = frame.metadata.iso;
        metadata["exposureCompensation"]   = frame.metadata.exposureCompensation;
        metadata["exposureTime"]           = (double) frame.metadata.exposureTime;
        metadata["orientation"]            = static_cast<int>(frame.metadata.screenOrientation);
        metadata["isCompressed"]           = frame.isCompressed;
        metadata["compressionType"]        = static_cast<int>(frame.compressionType);

        if(!frame.metadata.calibrationMatrix1.empty()) {
            metadata["calibrationMatrix1"]  = toJsonArray(frame.metadata.calibrationMatrix1);
        }

        if(!frame.metadata.calibrationMatrix2.empty()) {
            metadata["calibrationMatrix2"]  = toJsonArray(frame.metadata.calibrationMatrix2);
        }

        if(!frame.metadata.colorMatrix1.empty()) {
            metadata["colorMatrix1"]  = toJsonArray(frame.metadata.colorMatrix1);
        }

        if(!frame.metadata.colorMatrix2.empty()) {
            metadata["colorMatrix2"]  = toJsonArray(frame.metadata.colorMatrix2);
        }

        if(!frame.metadata.forwardMatrix1.empty()) {
            metadata["forwardMatrix1"]  = toJsonArray(frame.metadata.forwardMatrix1);
        }

        if(!frame.metadata.forwardMatrix2.empty()) {
            metadata["forwardMatrix2"]  = toJsonArray(frame.metadata.forwardMatrix2);
        }
        
        if(!frame.metadata.lensShadingMap.empty()) {
            metadata["lensShadingMapWidth"]    = frame.metadata.lensShadingMap[0].cols;
            metadata["lensShadingMapHeight"]   = frame.metadata.lensShadingMap[0].rows;
        
            vector<vector<float>> points;
            
            for(auto& i : frame.metadata.lensShadingMap) {
                vector<float> p;
                
                for(int y = 0; y < i.rows; y++) {
                    for(int x = 0; x < i.cols; x++) {
                        p.push_back(i.at<float>(y, x));
                    }
                }
                
                points.push_back(p);
            }
            
            metadata["lensShadingMap"] = points;
        }
        else {
            metadata["lensShadingMapWidth"] = 0;
            metadata["lensShadingMapHeight"] = 0;
        }
    }

    bool RawContainer::create(const int fd) {
        #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
            Header h;
        
            if(write(fd, &h, sizeof(Header)) < 0)
                return false;
        
            return true;
        #else
            return false;
        #endif
    }

    bool RawContainer::append(const int fd, const RawImageBuffer& frame) {
        #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
            // Write to file
            size_t start, end;

            start = end = 0;
            frame.data->getValidRange(start, end);

            auto stubBuffer = std::make_shared<RawImageBuffer>();

            stubBuffer->shallowCopy(frame);
            stubBuffer->offset = lseek(fd, 0, SEEK_CUR);

            FrameChunk frameChunk;

            // Write header and frame
            frameChunk.frameSize = static_cast<uint32_t>(end - start);

            if(write(fd, &frameChunk, sizeof(FrameChunk)) < 0)
                return false;

            auto data = frame.data->lock(false);

            if(write(fd, data + start, end - start) < 0) {
                frame.data->unlock();
                return false;
            }

            frame.data->unlock();

            // Add stub
            std::string filename = std::string("frame") + std::to_string(mFrames.size()) + std::string(".raw");
            mFrames.push_back(filename);

            mFrameBuffers[filename] = stubBuffer;

            return true;
        #else
            return false;
        #endif
    }

    bool RawContainer::commit(const int fd) {
    #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
        json11::Json::object metadata;

        generateContainerMetadata(metadata);

        string jsonOutput = json11::Json(metadata).dump();

        // Write metadata
        if(write(fd, jsonOutput.data(), jsonOutput.size()) < 0)
            return false;

        // Write final chunk
        EndChunk endChunk;

        endChunk.metadataMinusOffset = htonl(static_cast<uint32_t>(jsonOutput.size()));

        if(write(fd, &endChunk, sizeof(EndChunk)) < 0)
            return false;

        return true;
#else
        return false;
#endif
    }

    int RawContainer::getNumSegments() const {
        return mNumSegments;
    }
}
