#include "motioncam/RawContainerImpl.h"
#include "motioncam/RawCameraMetadata.h"
#include "motioncam/RawImageBuffer.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Util.h"
#include "motioncam/RawEncoder.h"

#include <utility>

#define _FILE_OFFSET_BITS 64

#if defined(_WIN32)
    #define FSEEK _fseeki64
    #define FTELL _ftelli64
#else
    #define FSEEK fseeko
    #define FTELL ftello
#endif

namespace motioncam {

    std::string GetBufferName(const int64_t timestampNs) {
        return std::to_string(timestampNs);
    }

    std::string GetBufferName(const RawImageBuffer& buffer) {
        return std::to_string(buffer.metadata.timestampNs);
    }

    RawContainerImpl::RawContainerImpl(FILE* file) :
        mMode(Mode::READ),
        mFile(file),
        mNumSegments(1),
        mIsInMemory(false),
        mExtraData(json11::Json()),
        mBufferStartOffset(0)
    {
        init();
    }

    RawContainerImpl::RawContainerImpl(const int fd,
                                       const RawCameraMetadata& cameraMetadata,
                                       const int numSegments,
                                       const json11::Json& extraData) :
        mMode(Mode::CREATE),
        mFile(fdopen(fd, "w")),
        mNumSegments(numSegments),
        mIsInMemory(true),
        mExtraData(extraData),
        mBufferStartOffset(0),
        mCameraMetadata(new RawCameraMetadata(cameraMetadata)),
        mPostProcessSettings(new PostProcessSettings())
    {
        create(extraData);
    }

    RawContainerImpl::RawContainerImpl(const RawCameraMetadata& cameraMetadata,
                                       const int numSegments,
                                       const json11::Json& extraData) :
        mMode(Mode::CREATE),
        mFile(nullptr),
        mNumSegments(numSegments),
        mIsInMemory(true),
        mExtraData(extraData),
        mBufferStartOffset(0),
        mCameraMetadata(new RawCameraMetadata(cameraMetadata))
    {
        mPostProcessSettings = std::unique_ptr<PostProcessSettings>(
                new PostProcessSettings(mExtraData["postProcessSettings"]));
    }

    RawContainerImpl::~RawContainerImpl() {
        if(mFile)
            fclose(mFile);
        mFile = nullptr;
    }

    void RawContainerImpl::writeBuffer(const RawImageBuffer& buffer) {
        // Keep offset
        int64_t offset = FTELL(mFile);

        // Get buffer size
        size_t start, end;
        buffer.data->getValidRange(start, end);

        // Don't write empty buffers
        const int64_t bufferSize = end - start;
        if(bufferSize <= 0)
            return;
        
        // Write buffer
        Item bufferItem { Type::BUFFER, static_cast<uint32_t>(bufferSize) };
        write(&bufferItem, sizeof(Item));
                
        auto* data = buffer.data->lock(false);
        try {
            write(data+start, end-start);
        }
        catch(const IOException& e) {
            buffer.data->unlock();
            throw e;
        }
        
        buffer.data->unlock();
        
        // Write metadata
        json11::Json::object metadata;
        buffer.toJson(metadata);
        
        auto json = json11::Json(metadata).dump();
        
        // Write the buffer metadata
        Item metadataItem { Type::METADATA, static_cast<uint32_t>(json.size()) };
        
        write(&metadataItem, sizeof(Item));
        write(json.data(), json.size());

        mOffsets.push_back( { offset, buffer.metadata.timestampNs } );
    }

    void RawContainerImpl::add(const RawImageBuffer& buffer, bool flush) {
        if(mMode != Mode::CREATE)
            throw IOException("Can't add. Container not it create mode");
        
        if(flush) {
            writeBuffer(buffer);
        }
        else {
            auto name = GetBufferName(buffer);

            mFrameList.push_back(name);
            mBuffers.insert(std::make_pair(name, std::make_shared<RawImageBuffer>(buffer)));
        }
    }

    void RawContainerImpl::add(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers, bool flush) {
        for(const auto& buffer : buffers) {
            if(flush) {
                writeBuffer(*buffer);
            }
            else {
                auto name = GetBufferName(*buffer);

                mFrameList.push_back(name);
                mBuffers.insert(std::make_pair(name, std::make_shared<RawImageBuffer>(*buffer)));
            }
        }
    }

    void RawContainerImpl::writeIndex() {
        if(FSEEK(mFile, 0, SEEK_END) != 0)
            throw IOException("Failed to write index");

        // Write offsets
        write(mOffsets.data(), sizeof(ItemOffset), mOffsets.size());
        
        // Write index
        Index index { INDEX_MAGIC_NUMBER, static_cast<uint32_t>(mOffsets.size()) };
        write(&index, sizeof(Index));
    }

    void RawContainerImpl::commit(const std::string& outputPath) {
        if(mMode != Mode::CREATE || mFile != nullptr)
            throw IOException("Can't commit. Container is not in a valid state");

        mFile = fopen(outputPath.c_str(), "wb");
        if(!mFile)
            throw IOException("Failed to open file " + outputPath);

        create(mExtraData);

        commit();
    }

    void RawContainerImpl::commit() {
        if(mMode != Mode::CREATE)
            throw IOException("Can't commit. Container not it create mode");

        // Flush buffers
        for(const auto& buffer : mBuffers) {
            writeBuffer(*(buffer.second));
        }
        
        mBuffers.clear();
        mFrameList.clear();
        
        writeIndex();
        
        mMode = Mode::CLOSED;
    }

    void RawContainerImpl::init() {
        if(!mFile)
            throw IOException("Can't open container");

        Header header{};
        
        // Check validity of file
        read(&header, sizeof(Header));
        
        if(header.version != CONTAINER_VERSION) {
            throw IOException("Invalid container version");
        }
        
        if(memcmp(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID)) != 0) {
            throw IOException("Invalid header id");
        }
        
        // Read camera metadata
        Item metadataItem{};
        read(&metadataItem, sizeof(Item));
        
        if(metadataItem.type != Type::METADATA) {
            throw IOException("Invalid camera metadata");
        }
        
        std::vector<uint8_t> metadataJson(metadataItem.size);
        read(metadataJson.data(), metadataItem.size);
        
        // Parse the camera metadata
        std::string m(metadataJson.begin(), metadataJson.end());
        std::string err;
        
        auto metadata = json11::Json::parse(m, err);
        if(!err.empty()) {
            throw IOException("Invalid camera metadata");
        }
        
        mCameraMetadata = std::unique_ptr<RawCameraMetadata>(new RawCameraMetadata(metadata));
        mNumSegments = util::GetOptionalSetting(metadata, "numSegments", 1);
        
        // Store extra data
        mExtraData = metadata["extraData"];
        
        // Get post process settings if available
        if(!mPostProcessSettings) {
            auto settings = std::unique_ptr<PostProcessSettings>(new PostProcessSettings(mExtraData["postProcessSettings"]));
            mPostProcessSettings = std::move(settings);
        }
        
        // Keep offset of where the buffers begin
        mBufferStartOffset = FTELL(mFile);
        
        // Read index
        if(FSEEK(mFile, -static_cast<long>(sizeof(Index)), SEEK_END) != 0) {
            throw IOException("Failed to get end chunk");
        }
        
        Index index{};
        read(&index, sizeof(Index));
        
        // Check validity of index
        if(index.indexMagicNumber != INDEX_MAGIC_NUMBER) {
            mMode = Mode::CORRUPTED;
        }
        else {
            mOffsets.resize(index.numOffsets);
            
            // Read the index
            int64_t offsetIndex = sizeof(Index) + index.numOffsets * sizeof(ItemOffset);
            if(FSEEK(mFile, -offsetIndex, SEEK_END) != 0) {
                throw IOException("Failed to get index");
            }
            
            read(mOffsets.data(), sizeof(ItemOffset), mOffsets.size());
        }
        
        reindexOffsets();
    }

    void RawContainerImpl::create(const json11::Json& extraData) {
        if(!mFile)
            throw IOException("Invalid file");
        
        Header h{};
        
        h.version = CONTAINER_VERSION;
        std::memcpy(h.ident, CONTAINER_ID, sizeof(CONTAINER_ID));
        
        json11::Json::object metadata;
        mCameraMetadata->toJson(metadata);

        // Store number of segements
        metadata["numSegments"] = mNumSegments;
        metadata["extraData"] = extraData;
                
        auto json = json11::Json(metadata).dump();
        const size_t metadataSize = json.size();
        
        Item metadataItem { Type::METADATA, static_cast<uint32_t>(metadataSize) };

        // Write the header
        write(&h, sizeof(Header));

        // Write the camera metadata after the header
        write(&metadataItem, sizeof(Item));
        write(json.data(), json.size());
    }

    void RawContainerImpl::reindexOffsets() {
        // Sort offsets so they are in order of timestamps
        std::sort(mOffsets.begin(), mOffsets.end(), [](const auto& a, const auto&b) {
            return a.timestamp < b.timestamp;
        });
        
        mFrameList.clear();
        mFrameOffsetMap.clear();
        
        for(const auto& i : mOffsets) {
            auto name = GetBufferName(i.timestamp);

            mFrameList.push_back(name);
            mFrameOffsetMap.insert({ name, i });
        }
    }

    void RawContainerImpl::recover() {
        if(mMode != Mode::CORRUPTED)
            return;
        
        mOffsets = attemptToRecover();
        
        reindexOffsets();
        
        // Switch to read mode
        mMode = Mode::READ;
    }

    std::vector<ItemOffset> RawContainerImpl::attemptToRecover() {
        int64_t currentOffset = mBufferStartOffset;
        std::vector<ItemOffset> offsets;
        
        // Get file size
        if(FSEEK(mFile, 0, SEEK_END) != 0)
            return offsets;
        
        int64_t fileSize = FTELL(mFile);
        
        while(currentOffset < fileSize) {
            if(FSEEK(mFile, currentOffset, SEEK_SET) != 0)
                break;

            Item bufferItem{};
            read(&bufferItem, sizeof(Item));

            if(bufferItem.type != Type::BUFFER)
                break;

            // Skip buffer if possible
            if(currentOffset + bufferItem.size > fileSize)
                break;

            if(FSEEK(mFile, bufferItem.size, SEEK_CUR) != 0)
                break;

            // Create and insert the buffer, if possible
            auto buffer = readMetadata();
            if(!buffer)
                break;
                        
            // Add buffer
            auto name = GetBufferName(*buffer);

            offsets.push_back( { currentOffset, static_cast<int64_t>(buffer->metadata.timestampNs) } );
            
            // Keep the buffer metadata
            mBuffers.insert(std::make_pair(name, buffer));
            
            currentOffset = FTELL(mFile);
        }
        
        return offsets;
    }

    RawCameraMetadata& RawContainerImpl::getCameraMetadata() const {
        return *mCameraMetadata;
    }

    PostProcessSettings& RawContainerImpl::getPostProcessSettings() const {
        return *mPostProcessSettings;
    }

    bool RawContainerImpl::isHdr() const {
        return util::GetOptionalSetting(mExtraData, "isHdr", false);
    }

    std::vector<std::string> RawContainerImpl::getFrames() const {
        return mFrameList;
    }

    void RawContainerImpl::uncompressBuffer(std::vector<uint8_t>& compressedBuffer, const std::shared_ptr<RawImageBuffer>& dst) const {
        std::vector<uint8_t> uncompressedBuffer(2 * dst->width * dst->height);
        
        if(dst->compressionType != CompressionType::MOTIONCAM)
            throw IOException("Invalid compression type");

        encoder::decode(reinterpret_cast<uint16_t*>(uncompressedBuffer.data()),
                        dst->width,
                        dst->height,
                        compressedBuffer.data(),
                        compressedBuffer.size());
                
        dst->data->copyHostData(uncompressedBuffer);
    }

    std::shared_ptr<RawImageBuffer> RawContainerImpl::readMetadata() {
        Item metadataItem{};
        read(&metadataItem, sizeof(Item));
        
        if(metadataItem.type != Type::METADATA)
            return nullptr;
        
        std::vector<uint8_t> metadataJson(metadataItem.size);
        read(metadataJson.data(), metadataItem.size);
        
        // Parse the metadata
        std::string m(metadataJson.begin(), metadataJson.end());
        std::string err;
        
        // Create and insert the buffer
        auto metadata = json11::Json::parse(m, err);
        if(!err.empty()) {
            return nullptr;
        }
        
        return std::make_shared<RawImageBuffer>(metadata);
    }

    std::shared_ptr<RawImageBuffer> RawContainerImpl::readFrame(const std::string& frame, bool readData) {
        // Load the metadata
        if(mFrameOffsetMap.find(frame) == mFrameOffsetMap.end())
            return nullptr;
        
        int64_t offset = mFrameOffsetMap.at(frame).offset;
        
        if(FSEEK(mFile, offset, SEEK_SET) != 0)
            throw IOException("Invalid offset");
        
        Item bufferItem{};
        read(&bufferItem, sizeof(Item));

        if(bufferItem.type != Type::BUFFER)
            throw IOException("Invalid buffer type");

        std::vector<uint8_t> data(bufferItem.size);

        if(readData) {
            read(data.data(), bufferItem.size);
        }
        else {
            if(FSEEK(mFile, bufferItem.size, SEEK_CUR) != 0)
                throw IOException("Invalid metadata");
        }
        
        std::shared_ptr<RawImageBuffer> buffer;
        
        auto bufferIt = mBuffers.find(frame);
        if(bufferIt != mBuffers.end()) {
            buffer = bufferIt->second;
        }
        
        // Read metadata if buffer was not found
        if(!buffer) {
            buffer = readMetadata();
            
            // If we can't read the metadata, return
            if(!buffer)
                return nullptr;
            
            mBuffers.insert(std::make_pair(frame, buffer));
        }
        
        // If we have read the buffer, uncompress it if necessary
        if(readData) {
            if(buffer->isCompressed) {
                uncompressBuffer(data, buffer);
            }
            else {
                buffer->data->copyHostData(data);
            }
        }
        
        // Finally crop shading map
        auto shadingMap = buffer->metadata.shadingMap();

        if(shadingMap.empty()) {
            std::vector<cv::Mat> emptyShadingMap;
            cv::Mat m(24, 18, CV_32F, cv::Scalar(1.0f));

            for(int i = 0; i < 4; i++)
                emptyShadingMap.push_back(m.clone());

            buffer->metadata.updateShadingMap(emptyShadingMap);
        }
        else {
            util::CropShadingMap(shadingMap,
                                 buffer->width,
                                 buffer->height,
                                 buffer->originalWidth,
                                 buffer->originalHeight,
                                 buffer->isBinned);

            buffer->metadata.updateShadingMap(shadingMap);
        }

        return buffer;
    }

    int64_t RawContainerImpl::getFrameTimestamp(const std::string& frame) const {
        if(mFrameOffsetMap.find(frame) != mFrameOffsetMap.end()) {
            return mFrameOffsetMap.at(frame).timestamp;
        }
        
        if(mBuffers.find(frame) != mBuffers.end()) {
            return mBuffers.at(frame)->metadata.timestampNs;
        }
        
        return 0;
    }

    std::shared_ptr<RawImageBuffer> RawContainerImpl::getFrame(const std::string& frame) {
        if(mBuffers.find(frame) != mBuffers.end())
            return mBuffers.at(frame);
                
        return readFrame(frame, false);
    }

    std::shared_ptr<RawImageBuffer> RawContainerImpl::loadFrame(const std::string& frame) {
        std::shared_ptr<RawImageBuffer> buffer;
        
        if(mBuffers.find(frame) != mBuffers.end())
            buffer = mBuffers.at(frame);

        if(buffer && buffer->data->len() > 0) {
            return buffer;
        }
        
        return readFrame(frame, true);
    }

    void RawContainerImpl::removeFrame(const std::string& frame) {
        // Remove from buffers map, frame list and offset map
        auto frameMapIt = mBuffers.find(frame);
        if(frameMapIt != mBuffers.end()) {
            mBuffers.erase(frameMapIt);
        }
        
        auto frameIt = std::find(mFrameList.begin(), mFrameList.end(), frame);
        if(frameIt != mFrameList.end())
            mFrameList.erase(frameIt);
        
        auto offsetIt = mFrameOffsetMap.find(frame);
        if(offsetIt != mFrameOffsetMap.end())
            mFrameOffsetMap.erase(offsetIt);
    }

    bool RawContainerImpl::isInMemory() const {
        return mIsInMemory;
    }

    int RawContainerImpl::getNumSegments() const {
        return mNumSegments;
    }

    bool RawContainerImpl::isCorrupted() const {
        return mMode == Mode::CORRUPTED;
    }

    void RawContainerImpl::write(const void* data, size_t size, size_t items) const {
        if(fwrite(data, size, items, mFile) != items) {
            throw IOException("Failed to write data");
        }
    }

    void RawContainerImpl::read(void* data, size_t size, size_t items) const {
        if(fread(data, size, items, mFile) != items) {
            throw IOException("Failed to read data");
        }
    }
}
