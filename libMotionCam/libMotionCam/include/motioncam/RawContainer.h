#ifndef RawContainer_hpp
#define RawContainer_hpp

#include <string>
#include <set>
#include <map>

#include <opencv2/opencv.hpp>
#include <json11/json11.hpp>

namespace motioncam {
    struct RawImageBuffer;
    struct RawCameraMetadata;
    struct PostProcessSettings;

    const uint8_t CONTAINER_VERSION = 2;
    const uint8_t CONTAINER_ID[7] = {'M', 'O', 'T', 'I', 'O', 'N', ' '};

    struct Header {
        uint8_t ident[7];
        uint8_t version;
    };

    class RawContainer {
    public:
        virtual ~RawContainer() = default;
        
        virtual const RawCameraMetadata& getCameraMetadata() const = 0;
        virtual const PostProcessSettings& getPostProcessSettings() const = 0;
                
        virtual bool isHdr() const = 0;
        virtual std::vector<std::string> getFrames() const = 0;
        
        virtual std::shared_ptr<RawImageBuffer> getFrame(const std::string& frame) = 0;
        virtual int64_t getFrameTimestamp(const std::string& frame) const = 0;
        virtual std::shared_ptr<RawImageBuffer> loadFrame(const std::string& frame) = 0;
        virtual void removeFrame(const std::string& frame) = 0;
        
        virtual bool isInMemory() const = 0;
        virtual int getNumSegments() const = 0;
        
        virtual void add(const RawImageBuffer& frame, bool flush) = 0;
        virtual void add(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers, bool flush) = 0;
        virtual void commit() = 0;
        virtual void commit(const std::string& outputPath) = 0;
        
        static std::unique_ptr<RawContainer> Open(const int fd);
        static std::unique_ptr<RawContainer> Open(const std::string& inputPath);
        
        static std::unique_ptr<RawContainer> Create(const RawCameraMetadata& cameraMetadata,
                                                    const int numSegments=1,
                                                    const json11::Json& extraData=json11::Json());

        static std::unique_ptr<RawContainer> Create(const int fd,
                                                    const RawCameraMetadata& cameraMetadata,
                                                    const int numSegments=1,
                                                    const json11::Json& extraData=json11::Json());
    };
}

#endif /* RawContainer_hpp */
