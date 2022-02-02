#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"

#include <zstd.h>
#include <utility>
#include <vector>
#include <vint.h>
#include <vp4.h>
#include <bitpack.h>

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
    
    RawContainer::RawContainer(const int fd) :
        mReferenceTimestamp(-1),
        mIsHdr(false),
        mFile(nullptr),
        mIsInMemory(false),
        mNumSegments(0)
    {
        initialise(fd);
    }

    RawContainer::RawContainer(const string& inputPath) :
        mFile(nullptr),
        mReferenceTimestamp(-1),
        mIsHdr(false),
        mIsInMemory(false),
        mNumSegments(0)
    {
        initialise(inputPath);
    }

    RawContainer::RawContainer(const RawCameraMetadata& cameraMetadata, const int numSegments) :
        mFile(nullptr),
        mCameraMetadata(cameraMetadata),
        mIsHdr(false),
        mIsInMemory(false),
        mNumSegments(numSegments)
    {
        // Empty container for streaming
    }

    RawContainer::RawContainer(const RawCameraMetadata& cameraMetadata,
                               const PostProcessSettings& postProcessSettings,
                               const int64_t referenceTimestamp,
                               const bool isHdr,
                               const vector<shared_ptr<RawImageBuffer>>& buffers) :
        mFile(nullptr),
        mCameraMetadata(cameraMetadata),
        mPostProcessSettings(postProcessSettings),
        mReferenceTimestamp(referenceTimestamp),
        mIsHdr(isHdr),
        mIsInMemory(true),
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
        if(file == nullptr)
            throw IOException("Failed to open");

        mFile = file;

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
            
            for(size_t i = 0; i < files.size(); i++) {
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
                string filename = util::GetRequiredSettingAsString(frameJson, "filename");

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
        
        ensureValid();
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
        
        ensureValid();
    }

    void RawContainer::ensureValid() {
        // Set reference
        if(mReferenceImage.empty() && !mFrames.empty()) {
            mReferenceImage = *mFrames.begin();
        }
    }

    void RawContainer::loadContainerMetadata(const json11::Json& metadata) {
        // Load post process settings if available
        if(metadata["postProcessingSettings"].is_object()) {
            mPostProcessSettings = PostProcessSettings(metadata["postProcessingSettings"]);
        }
        
        mReferenceTimestamp = stoll(util::GetOptionalStringSetting(metadata, "referenceTimestamp", "0"));
        mIsHdr = util::GetOptionalSetting(metadata, "isHdr", false);
        mNumSegments = util::GetOptionalSetting(metadata, "numSegments", 1);

        // Black/white levels
        std::vector<float> blackLevel;
        
        vector<Json> blackLevelValues = metadata["blackLevel"].array_items();
        for(auto& blackLevelValue : blackLevelValues) {
            blackLevel.push_back(blackLevelValue.number_value());
        }
        
        int whiteLevel = util::GetRequiredSettingAsInt(metadata, "whiteLevel");
        
        // Default to 64
        if(blackLevel.empty()) {
            for(int i = 0; i < 4; i++)
                blackLevel.push_back(64);
        }

        // Default to 1023
        if(whiteLevel <= 0)
            whiteLevel = 1023;

        mCameraMetadata.updateBayerOffsets(blackLevel, whiteLevel);
        
        // Color arrangement
        string colorFilterArrangment = util::GetRequiredSettingAsString(metadata, "sensorArrangment");

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
            for(size_t i = 0; i < metadata["apertures"].array_items().size(); i++)
                mCameraMetadata.apertures.push_back(metadata["apertures"].array_items().at(i).number_value());
        }

        if(metadata["focalLengths"].is_array()) {
            for(size_t i = 0; i < metadata["focalLengths"].array_items().size(); i++)
                mCameraMetadata.focalLengths.push_back(metadata["focalLengths"].array_items().at(i).number_value());
        }

        // Get overall shading map if present
        mContainerShadingMap = getLensShadingMap(metadata);
        
        // Make sure there's a valid shading map
        if(mContainerShadingMap.empty()) {
            for(int i = 0; i < 4; i++) {
                cv::Mat m(12, 16, CV_32F, cv::Scalar(1.0f));
                mContainerShadingMap.push_back(m);
            }
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
            string filename = util::GetRequiredSettingAsString(*it, "filename");

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
            
            data.reserve(frameChunk.frameSize + (buffer->second->rowStride * 4)); // Reserve extra space at the end
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
            else if(buffer->second->compressionType == CompressionType::V8NZENC     ||
                    buffer->second->compressionType == CompressionType::P4NZENC     ||
                    buffer->second->compressionType == CompressionType::BITNZPACK   ||
                    buffer->second->compressionType == CompressionType::BITNZPACK_2)
            {
                std::vector<uint16_t> rowOutput(2*buffer->second->width);
                std::vector<uint8_t> uncompressedBuffer(2*buffer->second->width*buffer->second->height);
                
                const uint16_t rowSize = buffer->second->width;
            
                auto decodeFunc = &v8nzdec128v16;
                
                if(buffer->second->compressionType == CompressionType::P4NZENC)
                    decodeFunc = &p4nzdec128v16;
                else if(buffer->second->compressionType == CompressionType::BITNZPACK)
                    decodeFunc = &bitnzunpack128v16;
                else if(buffer->second->compressionType == CompressionType::BITNZPACK_2)
                    decodeFunc = &bitnzunpack16;
                else if(buffer->second->compressionType == CompressionType::V8NZENC)
                    decodeFunc = &v8nzdec128v16;
                else
                    return nullptr;
                
                size_t offset = 0;
                size_t p = 0;

                // Allocate extra padding on the input
                data.resize(data.size() + buffer->second->rowStride * 4);
                
                // Read the image
                for(int y = 0; y < buffer->second->height; y++) {
                    size_t readBytes = decodeFunc(data.data() + offset, rowSize, rowOutput.data());
                    
                    // Reshuffle the row
                    for(size_t i = 0; i < rowSize/2; i++) {
                        uncompressedBuffer[p]   = rowOutput[i];
                        uncompressedBuffer[p+1] = rowOutput[i] >> 8;

                        uncompressedBuffer[p+2] = rowOutput[i+rowSize/2];
                        uncompressedBuffer[p+3] = rowOutput[i+rowSize/2] >> 8;

                        p+=4;
                    }
                    
                    offset += readBytes;
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
        
        // Crop the shading map at the point that it is loaded
        auto shadingMap = buffer->second->metadata.shadingMap();
        
        cropShadingMap(shadingMap,
                       buffer->second->width,
                       buffer->second->height,
                       buffer->second->originalWidth,
                       buffer->second->originalHeight,
                       buffer->second->isBinned);
        
        buffer->second->metadata.updateShadingMap(shadingMap);
        
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

    std::vector<cv::Mat> RawContainer::getLensShadingMap(const json11::Json& obj) {
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

    void RawContainer::cropShadingMap(std::vector<cv::Mat>& shadingMap, int width, int height, int originalWidth, int originalHeight, bool isBinned) const {
        if(originalWidth == width && originalHeight == height && !isBinned) {
            return;
        }
        
        if(isBinned) {
            originalWidth /= 2;
            originalHeight /= 2;
        }

        const int dstOriginalWidth = 80;
        const int dstOriginalHeight = (dstOriginalWidth * originalHeight) / originalWidth;
        
        const int dstWidth = width / (originalWidth / dstOriginalWidth);
        const int dstHeight = (dstWidth * height) / width;
        
        for(size_t i = 0; i < shadingMap.size(); i++) {
            cv::resize(shadingMap[i],
                       shadingMap[i],
                       cv::Size(dstOriginalWidth, dstOriginalHeight),
                       0, 0,
                       cv::INTER_LINEAR);

            int x = (dstOriginalWidth - dstWidth) / 2;
            int y = (dstOriginalHeight - dstHeight) / 2;

            shadingMap[i] = shadingMap[i](cv::Rect(x, y, dstOriginalWidth - x*2, dstOriginalHeight - y*2));

            // Shrink the shading map back to a reasonable size
            int shadingMapWidth = 32;
            int shadingMapHeight = (shadingMapWidth * shadingMap[i].rows) / shadingMap[i].cols;

            cv::resize(shadingMap[i],
                       shadingMap[i],
                       cv::Size(shadingMapWidth, shadingMapHeight),
                       0, 0,
                       cv::INTER_LINEAR);
        }
    }

    shared_ptr<RawImageBuffer> RawContainer::loadFrameMetadata(const json11::Json& obj) {
        shared_ptr<RawImageBuffer> buffer = std::make_shared<RawImageBuffer>();
        
        buffer->width               = util::GetRequiredSettingAsInt(obj, "width");
        buffer->height              = util::GetRequiredSettingAsInt(obj, "height");
        buffer->originalWidth       = util::GetOptionalSetting(obj, "originalWidth", buffer->width);
        buffer->originalHeight      = util::GetOptionalSetting(obj, "originalHeight", buffer->height);
        buffer->isBinned            = util::GetOptionalSetting(obj, "isBinned", false);
        buffer->rowStride           = util::GetRequiredSettingAsInt(obj, "rowStride");
        buffer->isCompressed        = util::GetOptionalSetting(obj, "isCompressed", false);
        buffer->compressionType     = static_cast<CompressionType>(util::GetOptionalSetting(obj, "compressionType", 0));
                
        // Default to ZSTD if no compression type specified
        if(buffer->isCompressed && buffer->compressionType == CompressionType::UNCOMPRESSED) {
            buffer->compressionType = CompressionType::ZSTD;
        }
        
        std::string offset   = util::GetOptionalStringSetting(obj, "offset", "0");
        buffer->offset       = stoll(offset);
        
        string pixelFormat = util::GetOptionalStringSetting(obj, "pixelFormat", "raw10");

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

        buffer->metadata.exposureTime           = util::GetOptionalSetting(obj, "exposureTime", 0);
        buffer->metadata.iso                    = util::GetOptionalSetting(obj, "iso", 0);
        buffer->metadata.exposureCompensation   = util::GetOptionalSetting(obj, "exposureCompensation", 0);
        buffer->metadata.screenOrientation      =
            static_cast<ScreenOrientation>(util::GetOptionalSetting(obj, "orientation", static_cast<int>(ScreenOrientation::LANDSCAPE)));
        
        buffer->metadata.asShot             = toVec3f((obj)["asShotNeutral"].array_items());
        
        string timestamp                    = util::GetRequiredSettingAsString(obj, "timestamp");
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

        // Store shading map
        auto shadingMap = getLensShadingMap(obj);

        if(shadingMap.empty()) {
            auto containerShadingMap = mContainerShadingMap;
            buffer->metadata.updateShadingMap(containerShadingMap);
        }
        else {
            buffer->metadata.updateShadingMap(shadingMap);
        }
        
        // Dynamic black/white levels
        buffer->metadata.dynamicWhiteLevel = util::GetOptionalSetting(obj, "dynamicWhiteLevel", 0.0f);
        
        if(obj["dynamicBlackLevel"].is_array()) {
            auto arr = obj["dynamicBlackLevel"].array_items();
            if(arr.size() == 4) {
                buffer->metadata.dynamicBlackLevel.resize(4);
                for(int c = 0; c < 4; c++)
                    buffer->metadata.dynamicBlackLevel[c] = arr[c].number_value();
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
        metadataJson["blackLevel"]          = mCameraMetadata.getBlackLevel();
        metadataJson["whiteLevel"]          = mCameraMetadata.getWhiteLevel();
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
        metadata["timestamp"]       = std::to_string(frame.metadata.timestampNs);
        metadata["filename"]        = filename;
        metadata["width"]           = frame.width;
        metadata["height"]          = frame.height;
        metadata["originalWidth"]   = frame.originalWidth;
        metadata["originalHeight"]  = frame.originalHeight;
        metadata["isBinned"]        = frame.isBinned;
        metadata["rowStride"]       = frame.rowStride;
        metadata["offset"]          = std::to_string(frame.offset);
        metadata["pixelFormat"]     = toString(frame.pixelFormat);
        metadata["type"]            = toString(frame.metadata.rawType);

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
                
        if(frame.metadata.dynamicWhiteLevel > 0)
            metadata["dynamicWhiteLevel"] = frame.metadata.dynamicWhiteLevel;
        
        if(!frame.metadata.dynamicBlackLevel.empty()) {
            metadata["dynamicBlackLevel"] = frame.metadata.dynamicBlackLevel;
        }
        
        if(!frame.metadata.shadingMap().empty()) {
            const auto& shadingMap = frame.metadata.shadingMap();
            
            metadata["lensShadingMapWidth"]    = shadingMap[0].cols;
            metadata["lensShadingMapHeight"]   = shadingMap[0].rows;
        
            vector<vector<float>> points;
            
            for(auto& i : shadingMap) {
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
