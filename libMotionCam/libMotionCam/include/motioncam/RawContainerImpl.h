#ifndef RawContainer2_hpp
#define RawContainer2_hpp

#include <string>
#include <vector>
#include <utility>

#include "motioncam/RawContainer.h"

namespace motioncam {
    struct RawCameraMetadata;
    struct RawImageBuffer;
    
    enum class Mode : int {
        CREATE,
        READ,
        CLOSED
    };

    //
    // Simple file format
    //

    const uint32_t INDEX_MAGIC_NUMBER = 0x34884CED;

    enum class Type : uint32_t {
        BUFFER,
        METADATA
    };

    struct Item {
        Type type;
        uint32_t size;
    };

    struct ItemOffset {
        int64_t offset;
        int64_t timestamp;
    };

    struct Index {
        uint32_t indexMagicNumber;
        uint32_t numOffsets;
    };

    class RawContainerImpl : public RawContainer {
    public:
        RawContainerImpl(FILE* file);
        RawContainerImpl(const int fd,
                         const RawCameraMetadata& cameraMetadata,
                         const int numSegments=1,
                         const json11::Json& extraData={});

        RawContainerImpl(const RawCameraMetadata& cameraMetadata,
                         const int numSegments=1,
                         const json11::Json& extraData={});

        ~RawContainerImpl();
        
        RawCameraMetadata& getCameraMetadata() const;
        PostProcessSettings& getPostProcessSettings() const;
        
        bool isHdr() const;
        
        std::vector<std::string> getFrames() const;        
        std::shared_ptr<RawImageBuffer> getFrame(const std::string& frame);
        int64_t getFrameTimestamp(const std::string& frame) const;
        std::shared_ptr<RawImageBuffer> loadFrame(const std::string& frame);
        void removeFrame(const std::string& frame);
        
        bool isInMemory() const;
        int getNumSegments() const;
        
        void add(const RawImageBuffer& buffer, bool flush);
        void add(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers, bool flush);
        
        void commit();
        void commit(const std::string& outputPath);

    private:
        void create(const json11::Json& extraData);
        void init();
        std::vector<ItemOffset> recover();
        std::shared_ptr<RawImageBuffer> readMetadata();
        std::shared_ptr<RawImageBuffer> readFrame(const std::string& frame, const bool readData=true);
        void uncompressBuffer(std::vector<uint8_t>& compressedBuffer, const std::shared_ptr<RawImageBuffer>& dst) const;
        void writeBuffer(const RawImageBuffer& buffer);
        void write(const void* data, size_t size, size_t items=1) const;
        void read(void* data, size_t size, size_t items=1) const;
        
    private:
        Mode mMode;
        FILE* mFile;
        const int mNumSegments;
        const bool mIsInMemory;
        json11::Json mExtraData;
        int64_t mBufferStartOffset;
        
        std::vector<ItemOffset> mOffsets;
        std::map<std::string, int64_t> mFrameOffsetMap;

        std::vector<std::string> mFrameList;
        std::map<std::string, std::shared_ptr<RawImageBuffer>> mBuffers;

        std::unique_ptr<RawCameraMetadata> mCameraMetadata;
        std::unique_ptr<PostProcessSettings> mPostProcessSettings;
    };
}

#endif /* RawContainer2_hpp */
