#ifndef RawContainer_hpp
#define RawContainer_hpp

#include <string>
#include <set>
#include <map>

#include <opencv2/opencv.hpp>
#include <json11/json11.hpp>

#include "motioncam/RawImageMetadata.h"

namespace motioncam {
    namespace util {
        class ZipWriter;
        class ZipReader;
    }

    class RawContainer {
    public:
        RawContainer(const int fd);
        RawContainer(const std::string& inputPath);
        RawContainer(const RawCameraMetadata& cameraMetadata, const int numSegments=1);

        RawContainer(const RawCameraMetadata& cameraMetadata,
                     const PostProcessSettings& postProcessSettings,
                     const int64_t referenceTimestamp,
                     const bool isHdr,
                     const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);

        ~RawContainer();
        
        const RawCameraMetadata& getCameraMetadata() const;
        const PostProcessSettings& getPostProcessSettings() const;

        std::string getReferenceImage() const;
        void updateReferenceImage(const std::string& referenceName);
        
        bool isHdr() const;
        std::vector<std::string> getFrames() const;
        
        std::shared_ptr<RawImageBuffer> getFrame(const std::string& frame) const;
        std::shared_ptr<RawImageBuffer> loadFrame(const std::string& frame) const;
        void removeFrame(const std::string& frame);
        
        bool create(const int fd);
        bool append(const int fd, const RawImageBuffer& frame);
        bool commit(const int fd);
        
        void save(const int fd);
        void save(const std::string& outputPath);
        void save(util::ZipWriter& writer);
                
        bool isInMemory() const { return mIsInMemory; };
        int getNumSegments() const;
        
    private:
        void initialise(const std::string& inputPath);
        void initialise(const int fd);
        void ensureValid();
        
        void loadFromBin(FILE* file);
        void loadFromZip(std::unique_ptr<util::ZipReader> zipReader);
        
        void generateContainerMetadata(json11::Json::object& metadataJson);
        void loadContainerMetadata(const json11::Json& metadata);
        std::shared_ptr<RawImageBuffer> loadFrameMetadata(const json11::Json& obj);

        void cropShadingMap(std::vector<cv::Mat>& shadingMap, int width, int height, int originalWidth, int originalHeight, bool isBinned);
        
        static std::vector<cv::Mat> getLensShadingMap(const json11::Json& obj);
        
        static std::string toString(ColorFilterArrangment sensorArrangment);
        static std::string toString(PixelFormat format);
        static std::string toString(RawType rawType);
        
        static cv::Mat toMat3x3(const std::vector<json11::Json>& array);
        static cv::Vec3f toVec3f(const std::vector<json11::Json>& array);
        static json11::Json::array toJsonArray(cv::Mat m);

        static void generateMetadata(const RawImageBuffer& frame, json11::Json::object& metadata, const std::string& filename);

    private:
        std::unique_ptr<util::ZipReader> mZipReader;
        FILE* mFile;
        
        RawCameraMetadata mCameraMetadata;
        PostProcessSettings mPostProcessSettings;
        int64_t mReferenceTimestamp;
        std::string mReferenceImage;
        bool mIsHdr;
        bool mIsInMemory;
        int mNumSegments;
        std::vector<std::string> mFrames;
        std::map<std::string, std::shared_ptr<RawImageBuffer>> mFrameBuffers;
        std::vector<cv::Mat> mContainerShadingMap;
    };
}

#endif /* RawContainer_hpp */
