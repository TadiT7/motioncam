#ifndef RawContainer_hpp
#define RawContainer_hpp

#include <string>
#include <set>
#include <map>

#include <opencv2/opencv.hpp>
#include <json11/json11.hpp>

#include "motioncam/RawImageMetadata.h"

namespace motioncam {

    class RawContainer {
    public:
        RawContainer(const std::string& inputPath);
        RawContainer(RawCameraMetadata  cameraMetadata,
                     const PostProcessSettings& postProcessSettings,
                     const int64_t referenceTimestamp,
                     const bool writeDNG,
                     std::vector<std::string>  frames,
                     std::map<std::string, std::shared_ptr<RawImageBuffer>>  frameBuffers);
        
        const RawCameraMetadata& getCameraMetadata() const;
        const PostProcessSettings& getPostProcessSettings() const;

        std::string getReferenceImage() const;
        bool getWriteDNG() const;
        const std::vector<std::string>& getFrames() const;
        
        std::shared_ptr<RawImageBuffer> getFrame(const std::string& frame) const;
        std::shared_ptr<RawImageBuffer> loadFrame(const std::string& frame) const;
        void releaseFrame(const std::string& frame) const;
        
        void saveContainer(const std::string& outputPath);
        
    private:
        void initialise();
        
        static std::string getRequiredSettingAsString(const json11::Json& json, const std::string& key);
        static int getRequiredSettingAsInt(const json11::Json& json, const std::string& key);
        static std::string getOptionalStringSetting(const json11::Json& json, const std::string& key, const std::string& defaultValue);
        static int getOptionalSetting(const json11::Json& json, const std::string& key, const int defaultValue);
        static bool getOptionalSetting(const json11::Json& json, const std::string& key, const bool defaultValue);
    
        static std::string toString(ColorFilterArrangment sensorArrangment);
        static std::string toString(PixelFormat format);

        static cv::Mat toMat3x3(const std::vector<json11::Json>& array);
        static cv::Vec3f toVec3f(const std::vector<json11::Json>& array);
        static json11::Json::array toJsonArray(cv::Mat m);

    private:
        std::unique_ptr<util::ZipReader> mZipReader;
        RawCameraMetadata mCameraMetadata;
        PostProcessSettings mPostProcessSettings;
        int64_t mReferenceTimestamp{};
        std::string mReferenceImage;
        bool mWriteDNG{};
        std::vector<std::string> mFrames;
        std::map<std::string, std::shared_ptr<RawImageBuffer>> mFrameBuffers;
    };
}

#endif /* RawContainer_hpp */
