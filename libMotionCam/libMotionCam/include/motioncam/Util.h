#ifndef Util_hpp
#define Util_hpp

#include <string>
#include <vector>

#include <miniz_zip.h>
#include <json11/json11.hpp>
#include <opencv2/opencv.hpp>

namespace motioncam {
    class RawContainer;

    struct RawImageMetadata;
    struct RawCameraMetadata;
    struct RawImageBuffer;
    enum class ScreenOrientation : int;
    enum class ColorFilterArrangment : int;
    enum class PixelFormat : int;
    enum class RawType : int;

    namespace util {
        struct ContainerFrame {
            std::string frameName;
            int64_t timestamp;
            size_t containerIndex;
        };

        class CloseableFd {
        public:
            CloseableFd(const int fd);
            ~CloseableFd();
            
        private:
            const int mFd;
        };
    
        class ZipWriter {
        public:
            ZipWriter(const int fd, bool append=false);
            ZipWriter(const std::string& pathname, bool append=false);
            ~ZipWriter();
            
            void addFile(const std::string& filename, const std::string& data);
            void addFile(const std::string& filename, const void* data, const size_t numBytes);
            void addFile(const std::string& filename, const std::vector<uint8_t>& data, const size_t numBytes);
            
            void commit();
            
        private:
            mz_zip_archive mZip;
            FILE* mFile;
            bool mCommited;
        };

        class ZipReader {
        public:
            ZipReader(FILE* file);
            ZipReader(const std::string& pathname);
            ~ZipReader();
            
            void read(const std::string& filename, std::string& output);
            void read(const std::string& filename, std::vector<uint8_t>& output);
            
            const std::vector<std::string>& getFiles() const;
            
        private:
            mz_zip_archive mZip;
            FILE* mFile;
            std::vector<std::string> mFiles;
        };

        void ReadCompressedFile(const std::string& inputPath, std::vector<uint8_t>& output);
        void WriteCompressedFile(const std::vector<uint8_t>& data, const std::string& outputPath);
        void ReadFile(const std::string& inputPath, std::vector<uint8_t>& output);
        void WriteFile(const uint8_t* data, size_t size, const std::string& outputPath);
        json11::Json ReadJsonFromFile(const std::string& path);
        void GetBasePath(const std::string& path, std::string& basePath, std::string& filename);    
        bool EndsWith(const std::string& str, const std::string& ending);

        cv::Mat BuildRawImage(std::vector<cv::Mat> channels, int cropX, int cropY);
    
        void WriteDng(const cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const ScreenOrientation orientation,
                      const bool enableCompression,
                      const bool saveShadingMap,
                      const std::string& outputPath);

        void WriteDng(const cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const ScreenOrientation orientation,
                      const bool enableCompression,
                      const bool saveShadingMap,
                      const int fd);

        void WriteDng(const cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const ScreenOrientation orientation,
                      const bool enableCompression,
                      const bool saveShadingMap,
                      ZipWriter& zipWriter,
                      const std::string& outputName);

        std::string GetRequiredSettingAsString(const json11::Json& json, const std::string& key);
        int GetRequiredSettingAsInt(const json11::Json& json, const std::string& key);
        std::string GetOptionalStringSetting(const json11::Json& json, const std::string& key, const std::string& defaultValue);
        int GetOptionalSetting(const json11::Json& json, const std::string& key, const int defaultValue);
        double GetOptionalSetting(const json11::Json& json, const std::string& key, const double defaultValue);
        bool GetOptionalSetting(const json11::Json& json, const std::string& key, const bool defaultValue);
    
        void GetNearestBuffers(
            const std::vector<std::unique_ptr<RawContainer>>& containers,
            const std::vector<ContainerFrame>& orderedFrames,
            const int startIdx,
            const int numBuffers,
            std::vector<std::shared_ptr<RawImageBuffer>>& outNearestBuffers);
    
        void GetOrderedFrames(
            const std::vector<std::unique_ptr<RawContainer>>& containers,
            std::vector<ContainerFrame>& outOrderedFrames);
    
        std::string toString(const ColorFilterArrangment& sensorArrangment);
        std::string toString(const PixelFormat& format);
        std::string toString(const RawType& rawType);
    
        cv::Mat toMat3x3(const std::vector<json11::Json>& array);
        cv::Vec3f toVec3f(const std::vector<json11::Json>& array);
        json11::Json::array toJsonArray(const cv::Mat& m);
    
        void CropShadingMap(std::vector<cv::Mat>& shadingMap,
                            int width,
                            int height,
                            int originalWidth,
                            int originalHeight,
                            bool isBinned);

    }
}

#endif /* Util_hpp */
