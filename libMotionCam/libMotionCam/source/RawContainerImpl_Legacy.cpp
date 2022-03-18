#include "motioncam/RawContainerImpl_Legacy.h"

#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"
#include "motioncam/RawImageBuffer.h"
#include "motioncam/RawCameraMetadata.h"
#include "motioncam/Settings.h"

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

    static const uint16_t FRAME_CHUNK = 0x01;
    static const uint16_t END_CHUNK = 0x02;

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

    RawContainerImpl_Legacy::RawContainerImpl_Legacy(FILE* file) :
        mIsHdr(false),
        mFile(nullptr),
        mIsInMemory(false),
        mNumSegments(0)
    {
        initialise(file);
    }

    RawContainerImpl_Legacy::RawContainerImpl_Legacy(const string& inputPath) :
        mFile(nullptr),
        mIsHdr(false),
        mIsInMemory(false),
        mNumSegments(0)
    {
        initialise(inputPath);
    }

    RawContainerImpl_Legacy::~RawContainerImpl_Legacy() {
        if(mFile) {
            fclose(mFile);
        }
        mFile = nullptr;
    }

    void RawContainerImpl_Legacy::loadFromBin(FILE* file) {
        if(file == nullptr)
            throw IOException("Failed to open");

        mFile = file;

        // Verify header
        Header header;
        
        if(fread(&header, sizeof(Header), 1, mFile) != 1)
            throw IOException("Failed to reader container header");
        
        if(header.version != CONTAINER_VERSION &&
           memcmp(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID)) != 0)
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

    void RawContainerImpl_Legacy::loadFromZip(std::unique_ptr<util::ZipReader> zipReader) {
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

    void RawContainerImpl_Legacy::initialise(FILE* file) {
        Header header;
        
        size_t result = fread(&header, sizeof(Header), 1, file);
        if(result != 1) {
            fclose(file);
            throw IOException("Failed to determine type of container");
        }
        
        // Reset position
        rewind(file);
        
        if(header.version == 1 &&
           memcmp(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID)) == 0)
        {
            loadFromBin(file);
        }
        else {
            auto zipReader = std::unique_ptr<util::ZipReader>(new util::ZipReader(file));
            loadFromZip(std::move(zipReader));
        }
    }

    void RawContainerImpl_Legacy::initialise(const std::string& inputPath) {
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
    }

    void RawContainerImpl_Legacy::loadContainerMetadata(const json11::Json& metadata) {
        // Load post process settings if available
        if(metadata["postProcessingSettings"].is_object()) {
            mPostProcessSettings = std::unique_ptr<PostProcessSettings>(new PostProcessSettings(metadata["postProcessingSettings"]));
        }
        
        mIsHdr = util::GetOptionalSetting(metadata, "isHdr", false);
        mNumSegments = util::GetOptionalSetting(metadata, "numSegments", 1);

        mCameraMetadata = std::unique_ptr<RawCameraMetadata>(new RawCameraMetadata(metadata));

        // Get overall shading map if present
        mContainerShadingMap = RawImageBuffer::GetLensShadingMap(metadata);
        
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
            
            mFrames.push_back(filename);
            mFrameBuffers.insert(make_pair(filename, buffer));
            
            ++it;
        }
    }

    const RawCameraMetadata& RawContainerImpl_Legacy::getCameraMetadata() const {
        return *mCameraMetadata;
    }

    const PostProcessSettings& RawContainerImpl_Legacy::getPostProcessSettings() const {
        return *mPostProcessSettings;
    }

    bool RawContainerImpl_Legacy::isHdr() const {
        return mIsHdr;
    }

    vector<string> RawContainerImpl_Legacy::getFrames() const {
        return mFrames;
    }

    int64_t RawContainerImpl_Legacy::getFrameTimestamp(const std::string& frame) const {
        if(mFrameBuffers.find(frame) != mFrameBuffers.end()) {
            auto buffer = mFrameBuffers.at(frame);
            return buffer->metadata.timestampNs;
        }
        
        return 0;
    }

    shared_ptr<RawImageBuffer> RawContainerImpl_Legacy::loadFrame(const string& frame) {
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
        
        util::CropShadingMap(shadingMap,
                             buffer->second->width,
                             buffer->second->height,
                             buffer->second->originalWidth,
                             buffer->second->originalHeight,
                             buffer->second->isBinned);

        buffer->second->metadata.updateShadingMap(shadingMap);
        
        return buffer->second;
    }

    shared_ptr<RawImageBuffer> RawContainerImpl_Legacy::getFrame(const string& frame) {
        auto buffer = mFrameBuffers.find(frame);
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }
        
        return buffer->second;
    }

    void RawContainerImpl_Legacy::removeFrame(const string& frame) {
        auto it = find(mFrames.begin(), mFrames.end(), frame);
        if(it != mFrames.end()) {
            mFrames.erase(it);
        }
        
        auto bufferIt = mFrameBuffers.find(frame);
        if(bufferIt != mFrameBuffers.end())
            mFrameBuffers.erase(bufferIt);
    }

    shared_ptr<RawImageBuffer> RawContainerImpl_Legacy::loadFrameMetadata(const json11::Json& obj) {
        shared_ptr<RawImageBuffer> buffer = std::make_shared<RawImageBuffer>(obj);
        
        if(buffer->metadata.shadingMap().empty()) {
            buffer->metadata.updateShadingMap(mContainerShadingMap);
        }
        
        return buffer;
    }

    int RawContainerImpl_Legacy::getNumSegments() const {
        return mNumSegments;
    }
}
