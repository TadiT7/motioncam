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
        
        class ZipWriter {
        public:
            ZipWriter(const int fd);
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
            ZipReader(const std::string& pathname);
            ~ZipReader();
            
            void read(const std::string& filename, std::string& output);
            void read(const std::string& filename, std::vector<uint8_t>& output);
            
            const std::vector<std::string>& getFiles() const;
            
        private:
            mz_zip_archive mZip;
            std::vector<std::string> mFiles;
        };

#ifdef ZSTD_AVAILABLE
        void ReadCompressedFile(const std::string& inputPath, std::vector<uint8_t>& output);
        void WriteCompressedFile(const std::vector<uint8_t>& data, const std::string& outputPath);
#endif

        void ReadFile(const std::string& inputPath, std::vector<uint8_t>& output);
        void WriteFile(const uint8_t* data, size_t size, const std::string& outputPath);
        json11::Json ReadJsonFromFile(const std::string& path);
        void GetBasePath(const std::string& path, std::string& basePath, std::string& filename);
    
        cv::Mat BuildRawImage(std::vector<cv::Mat> channels, int cropX, int cropY);
    
        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const std::string& outputPath);
    
        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      ZipWriter& zipWriter,
                      const std::string& outputName);

    }
}

#endif /* Util_hpp */
