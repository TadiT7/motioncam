#ifndef RawContainerImpl_Legacy_hpp
#define RawContainerImpl_Legacy_hpp

#include "motioncam/RawContainer.h"
#include <opencv2/opencv.hpp>

namespace motioncam {
    namespace util {
        class ZipWriter;
        class ZipReader;
    }

    class RawContainerImpl_Legacy : public RawContainer {
    public:
        RawContainerImpl_Legacy(FILE* file);
        RawContainerImpl_Legacy(const std::string& inputPath);

        ~RawContainerImpl_Legacy();
        
        const RawCameraMetadata& getCameraMetadata() const;
        const PostProcessSettings& getPostProcessSettings() const;
        
        bool isHdr() const;
        std::vector<std::string> getFrames() const;
        
        std::shared_ptr<RawImageBuffer> getFrame(const std::string& frame);
        int64_t getFrameTimestamp(const std::string& frame) const;
        std::shared_ptr<RawImageBuffer> loadFrame(const std::string& frame);
        void removeFrame(const std::string& frame);

        void add(const RawImageBuffer& frame, bool flush) { throw std::runtime_error("Unsupported"); };
        void add(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers, bool flush) { throw std::runtime_error("Unsupported"); };
        void commit() { throw std::runtime_error("Unsupported"); };
        void commit(const std::string& outputPath) { throw std::runtime_error("Unsupported"); };
        
        bool isInMemory() const { return mIsInMemory; };
        int getNumSegments() const;
        bool isCorrupted() const { return false; };
        
        void recover() { };
        
    private:
        void initialise(const std::string& inputPath);
        void initialise(FILE* file);
        
        void loadFromBin(FILE* file);
        void loadFromZip(std::unique_ptr<util::ZipReader> zipReader);
        
        void loadContainerMetadata(const json11::Json& metadata);
        std::shared_ptr<RawImageBuffer> loadFrameMetadata(const json11::Json& obj);
        
    private:
        std::unique_ptr<util::ZipReader> mZipReader;
        FILE* mFile;
        
        std::unique_ptr<RawCameraMetadata> mCameraMetadata;
        std::unique_ptr<PostProcessSettings> mPostProcessSettings;
        bool mIsHdr;
        bool mIsInMemory;
        int mNumSegments;
        std::vector<std::string> mFrames;
        std::map<std::string, std::shared_ptr<RawImageBuffer>> mFrameBuffers;
        std::vector<cv::Mat> mContainerShadingMap;
    };

} // namespace motioncam

#endif /* RawContainerImpl_Legacy_hpp */
