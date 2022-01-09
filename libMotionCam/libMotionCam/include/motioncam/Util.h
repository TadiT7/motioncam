#ifndef Util_hpp
#define Util_hpp

#include <string>
#include <vector>

#include <miniz_zip.h>
#include <json11/json11.hpp>
#include <opencv2/opencv.hpp>

namespace motioncam {
    struct RawImageMetadata;
    struct RawCameraMetadata;

    namespace util {
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
                      const std::string& outputPath);

        void WriteDng(const cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const int fd);

        void WriteDng(const cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      ZipWriter& zipWriter,
                      const std::string& outputName);

        std::string GetRequiredSettingAsString(const json11::Json& json, const std::string& key);
        int GetRequiredSettingAsInt(const json11::Json& json, const std::string& key);
        std::string GetOptionalStringSetting(const json11::Json& json, const std::string& key, const std::string& defaultValue);
        int GetOptionalSetting(const json11::Json& json, const std::string& key, const int defaultValue);
        double GetOptionalSetting(const json11::Json& json, const std::string& key, const double defaultValue);
        bool GetOptionalSetting(const json11::Json& json, const std::string& key, const bool defaultValue);
    }
}

#endif /* Util_hpp */
