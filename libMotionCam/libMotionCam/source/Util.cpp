#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"
#include "motioncam/RawImageMetadata.h"

#include <fstream>
#include <zstd.h>

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
    #include <unistd.h>
#endif

#include <dng/dng_host.h>
#include <dng/dng_negative.h>
#include <dng/dng_camera_profile.h>
#include <dng/dng_file_stream.h>
#include <dng/dng_memory_stream.h>
#include <dng/dng_image_writer.h>
#include <dng/dng_render.h>
#include <dng/dng_gain_map.h>
#include <dng/dng_exif.h>

using std::string;
using std::vector;
using std::ios;
using std::set;

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)

class dng_fd_stream : public dng_stream {
public:
    dng_fd_stream(const int fd, bool output) :
        dng_stream ((dng_abort_sniffer *) NULL, kDefaultBufferSize, 0),
    fFd(fd)
    {
        if(fd < 0)
            ThrowFileIsDamaged();
    }
    
    ~dng_fd_stream () {
        if (fFd < 0)
            return;

        fsync(fFd);
        close(fFd);
    }

    uint64 DoGetLength () {
        if (lseek (fFd, 0, SEEK_END) < 0) {
            ThrowReadFile ();
        }
        
        return (uint64) lseek(fFd, 0, SEEK_CUR);
    }
            
    void DoRead(void *data, uint32 count, uint64 offset) {
        if (lseek (fFd, (long) offset, SEEK_SET) < 0) {
            ThrowReadFile ();
        }
        
        uint32 bytesRead = (uint32) read (fFd, data, count);
        
        if (bytesRead != count) {
            ThrowReadFile ();
        }
    }
    
    void DoWrite(const void *data, uint32 count, uint64 offset) {
        if (lseek (fFd, (uint32) offset, SEEK_SET) < 0) {
            ThrowWriteFile ();
        }
                
        uint32 bytesWritten = (uint32) write (fFd, data, count);

        if (bytesWritten != count) {
            ThrowWriteFile ();
        }
    }
    
private:
    int fFd;
};

#endif

namespace motioncam {
    namespace util {
        CloseableFd::CloseableFd(const int fd) : mFd(fd) {
        }
    
        CloseableFd::~CloseableFd() {
            #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
                if(mFd >= 0)
                    close(mFd);
            #endif
        }
    
        //
        // Very basic zip writer
        //
    
        ZipWriter::ZipWriter(const int fd, bool append) : mFile(nullptr), mZip{ 0 }, mCommited(false) {
            if(append) {
                mFile = fdopen(fd, "w");

                int result = mz_zip_reader_init_cfile(&mZip, mFile, 0, 0);
                
                if(!result) {
                    throw IOException("Failed to init reader err: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&mZip))));
                }
                
                result = mz_zip_writer_init_from_reader(&mZip, nullptr);
                if(!result) {
                    throw IOException("Failed to convert to writer err: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&mZip))));
                }
            }
            else {
                mFile = fdopen(fd, "w");
                
                int result = mz_zip_writer_init_cfile(&mZip, mFile, 0);
                
                if(!result) {
                    throw IOException("Can't create err: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&mZip))));
                }
            }
        }
    
        ZipWriter::ZipWriter(const string& filename, bool append) : mZip{ 0 }, mCommited(false), mFile(nullptr) {
            if(append) {
                if(!mz_zip_reader_init_file(&mZip, filename.c_str(), 0)) {
                    throw IOException("Can't read " + filename);
                }

                if(!mz_zip_writer_init_from_reader(&mZip, filename.c_str())) {
                    throw IOException("Can't append to " + filename);
                }
            }
            else {
                if(!mz_zip_writer_init_file(&mZip, filename.c_str(), 0)) {
                    throw IOException("Can't create " + filename);
                }
            }
        }
    
        void ZipWriter::addFile(const std::string& filename, const std::string& data) {
            addFile(filename, vector<uint8_t>(data.begin(), data.end()), data.size());
        }

        void ZipWriter::addFile(const std::string& filename, const std::vector<uint8_t>& data, const size_t numBytes) {
            addFile(filename, data.data(), numBytes);
        }
    
        void ZipWriter::addFile(const std::string& filename, const void* data, const size_t numBytes) {
            if(mCommited) {
                throw IOException("Can't add " + filename + " because archive has been commited");
            }
            
            if(!mz_zip_writer_add_mem(&mZip, filename.c_str(), data, numBytes, MZ_NO_COMPRESSION)) {
                throw IOException("Can't add " + filename);
            }
        }
    
        void ZipWriter::commit() {
            if(!mz_zip_writer_finalize_archive(&mZip)) {
                throw IOException("Failed to finalize archive!");
            }
        
            if(!mz_zip_writer_end(&mZip)) {
                throw IOException("Failed to complete archive!");
            }
            
            mCommited = true;
            
            if(mFile) {
                fflush(mFile);
            }
        }
        
        ZipWriter::~ZipWriter() {
            if(!mCommited) {
                mz_zip_writer_finalize_archive(&mZip);
                mz_zip_writer_end(&mZip);
            }
            
            if(mFile) {
                fclose(mFile);
            }
        }
    
        //
        // Very basic zip reader
        //

        ZipReader::ZipReader(FILE* file) : mZip{ 0 }, mFile(file)
        {
            if(!mz_zip_reader_init_cfile(&mZip, mFile, 0, 0)) {
                throw IOException("Can't read from fd");
            }
            
            auto numFiles = mz_zip_reader_get_num_files(&mZip);
            char entry[512];

            for(auto i = 0; i < numFiles; i++) {
                auto len = mz_zip_reader_get_filename(&mZip, i, entry, 512);
                if(len == 0) {
                    throw IOException("Failed to parse entry");
                }
                
                mFiles.emplace_back(entry, len - 1);
            }
        }

        ZipReader::ZipReader(const string& filename) : mFile(nullptr), mZip{ 0 } {
            if(!mz_zip_reader_init_file(&mZip, filename.c_str(), 0)) {
                throw IOException("Can't read " + filename);
            }
            
            auto numFiles = mz_zip_reader_get_num_files(&mZip);
            char entry[512];

            for(auto i = 0; i < numFiles; i++) {
                auto len = mz_zip_reader_get_filename(&mZip, i, entry, 512);
                if(len == 0) {
                    throw IOException("Failed to parse " + filename);
                }
                
                mFiles.emplace_back(entry, len - 1);
            }
        }
    
        ZipReader::~ZipReader() {
            mz_zip_reader_end(&mZip);
            
            if(mFile) {
                fclose(mFile);
            }
        }
    
        void ZipReader::read(const std::string& filename, std::string& output) {
            vector<uint8_t> tmp;
            
            read(filename, tmp);
            
            output = std::string(tmp.begin(), tmp.end());
        }
    
        void ZipReader::read(const string& filename, vector<uint8_t>& output) {
            auto it = std::find(mFiles.begin(), mFiles.end(), filename);
            if(it == mFiles.end()) {
                throw IOException("Unable to find " + filename);
            }
            
            size_t index = it - mFiles.begin();
            mz_zip_archive_file_stat stat;
            
            if (!mz_zip_reader_file_stat(&mZip, static_cast<mz_uint>(index), &stat))
                throw IOException("Failed to stat " + filename);

            // Resize output
            output.resize(stat.m_uncomp_size);
            
            if(!mz_zip_reader_extract_to_mem(&mZip, static_cast<mz_uint>(index), &output[0], output.size(), 0)) {
                throw IOException("Failed to load " + filename);
            }
        }
    
        const std::vector<std::string>& ZipReader::getFiles() const {
            return mFiles;
        }
    
        //

        void ReadCompressedFile(const string& inputPath, vector<uint8_t>& output) {
            std::ifstream file(inputPath, std::ios::binary);
            
            if (file.eof() || file.fail())
                throw IOException("Can't read file " + inputPath);

            const size_t buffInSize = ZSTD_DStreamInSize();
            const size_t buffOutSize = ZSTD_DStreamOutSize();

            vector<uint8_t> buffIn(buffInSize);
            vector<uint8_t> buffOut(buffOutSize);

            std::shared_ptr<ZSTD_DCtx> ctx(ZSTD_createDCtx(), ZSTD_freeDCtx);

            size_t err = 0;
            
            while(!file.eof() || !file.fail()) {
                file.read(reinterpret_cast<char*>(buffIn.data()), buffInSize);
                size_t readBytes = file.gcount();
                
                ZSTD_inBuffer inputBuffer = { buffIn.data(), readBytes, 0 };
                
                while (inputBuffer.pos < inputBuffer.size) {
                    ZSTD_outBuffer outputBuffer = { buffOut.data(), buffOut.size(), 0 };

                    err = ZSTD_decompressStream(ctx.get(), &outputBuffer, &inputBuffer);
                    if(ZSTD_isError(err)) {
                        throw IOException("Failed to decompress file " + inputPath + " error: " + ZSTD_getErrorName(err));
                    }
                    
                    output.insert(output.end(), buffOut.begin(), buffOut.end());
                }
            }

            if (err != 0) {
                throw IOException("Failed to decompress file " + inputPath + " input is truncated." + ZSTD_getErrorName(err));
            }
        }
    
        void WriteCompressedFile(const vector<uint8_t>& data, const string& outputPath) {
            std::ofstream file(outputPath, std::ios::binary);
            
            // If we have a problem
            if(!file.is_open() || file.fail()) {
                throw IOException("Cannot write to " + outputPath);
            }

            // Create compression context
            std::shared_ptr<ZSTD_CCtx> ctx(ZSTD_createCCtx(), ZSTD_freeCCtx);
            
            const size_t buffInSize = ZSTD_CStreamInSize();
            const size_t buffOutSize = ZSTD_CStreamOutSize();

            vector<uint8_t> buffOut(buffOutSize);
            
            ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, 1);
            ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_checksumFlag, 1);
            
            size_t pos = 0;
            bool lastChunk = false;
            
            while (!lastChunk) {
                size_t read = buffInSize;
                
                if(pos + buffInSize >= data.size()) {
                    read = data.size() - pos;
                    lastChunk = true;
                }

                const ZSTD_EndDirective mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
                const char* buffIn = reinterpret_cast<const char*>(data.data() + pos);
                
                ZSTD_inBuffer input = { buffIn, read, 0 };
                
                int finished;
                
                do {
                   ZSTD_outBuffer output = { buffOut.data(), buffOutSize, 0 };
                   const size_t remaining = ZSTD_compressStream2(ctx.get(), &output, &input, mode);
                   
                    file.write(reinterpret_cast<char*>(buffOut.data()), output.pos);
                    
                    if(file.fail()) {
                        throw IOException("Cannot write to " + outputPath);
                    }
                    
                   finished = lastChunk ? (remaining == 0) : (input.pos == input.size);
                } while (!finished);
                
                pos += read;
            }
        }

        void ReadFile(const string& inputPath, vector<uint8_t>& output) {
            std::ifstream file(inputPath, std::ios::binary);
            
            if (file.eof() || file.fail())
                throw IOException("Can't read file " + inputPath);
            
            file.seekg(0, ios::end);
            
            std::streampos fileSize = file.tellg();
            
            output.resize(fileSize);
            
            file.seekg(0, ios::beg);
            
            file.read(reinterpret_cast<char*>(&output[0]), fileSize);
            
            file.close();
        }

        void WriteFile(const uint8_t* data, size_t size, const std::string& outputPath) {
            std::ofstream file(outputPath, std::ios::binary);
            
            // If we have a problem
            if(!file.is_open() || file.fail()) {
                throw IOException("Cannot write to " + outputPath);
            }
            
            file.write( (char*) data, size );
            
            // Error writing this file?
            if(file.fail()) {
                file.close();

                throw IOException("Cannot write " + outputPath);
            }
            
            file.close();
        }

        json11::Json ReadJsonFromFile(const string& path) {
            // Read file to string
            std::ifstream file(path);
            string str, err;
            
            if(file.eof() || file.fail()) {
                throw IOException("Cannot open JSON file: " + path);
            }
            
            file.seekg(0, ios::end);
            
            str.reserve(file.tellg());
            
            file.seekg(0, ios::beg);
            
            str.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            
            // Parse the metadata
            json11::Json metadata = json11::Json::parse(str, err);
            
            if(!err.empty()) {
                throw IOException("Cannot parse JSON file: " + path);
            }
            
            return metadata;
        }

        void GetBasePath(const std::string& path, std::string& basePath, std::string& filename) {
            size_t index = path.find_last_of('/');
            if(index == std::string::npos) {
                basePath = "";
                filename = path;
                return;
            }
            
            basePath = path.substr(0, index);
            filename = path.substr(index + 1, path.size());
        }
    
        cv::Mat BuildRawImage(std::vector<cv::Mat> channels, int cropX, int cropY) {
            const uint32_t height = channels[0].rows * 2;
            const uint32_t width  = channels[1].cols * 2;
            
            cv::Mat outputImage(height, width, CV_16U);
            
            for (int y = 0; y < height; y+=2) {
                auto* outRow1 = outputImage.ptr<uint16_t>(y);
                auto* outRow2 = outputImage.ptr<uint16_t>(y + 1);
                
                int ry = y / 2;
                
                const uint16_t* inC0 = channels[0].ptr<uint16_t>(ry);
                const uint16_t* inC1 = channels[1].ptr<uint16_t>(ry);
                const uint16_t* inC2 = channels[2].ptr<uint16_t>(ry);
                const uint16_t* inC3 = channels[3].ptr<uint16_t>(ry);
                
                for(int x = 0; x < width; x+=2) {
                    int rx = x / 2;
                    
                    outRow1[x]      = inC0[rx];
                    outRow1[x + 1]  = inC1[rx];
                    outRow2[x]      = inC2[rx];
                    outRow2[x + 1]  = inC3[rx];
                }
            }
            
            return outputImage(cv::Rect(cropX, cropY, width - cropX*2, height - cropY*2)).clone();
        }

        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      dng_stream& dngStream)
        {
            const int width  = rawImage.cols;
            const int height = rawImage.rows;
            
            dng_host host;

            host.SetSaveLinearDNG(false);
            host.SetSaveDNGVersion(dngVersion_SaveDefault);
            
            AutoPtr<dng_negative> negative(host.Make_dng_negative());
            
            // Create lens shading map for each channel
            for(int c = 0; c < 4; c++) {
                dng_point channelGainMapPoints(imageMetadata.lensShadingMap[c].rows, imageMetadata.lensShadingMap[c].cols);
                
                AutoPtr<dng_gain_map> gainMap(new dng_gain_map(host.Allocator(),
                                                               channelGainMapPoints,
                                                               dng_point_real64(1.0 / (imageMetadata.lensShadingMap[c].rows), 1.0 / (imageMetadata.lensShadingMap[c].cols)),
                                                               dng_point_real64(0, 0),
                                                               1));
                
                for(int y = 0; y < imageMetadata.lensShadingMap[c].rows; y++) {
                    for(int x = 0; x < imageMetadata.lensShadingMap[c].cols; x++) {
                        gainMap->Entry(y, x, 0) = imageMetadata.lensShadingMap[c].at<float>(y, x);
                    }
                }
                
                int left = 0;
                int top  = 0;
                
                if(c == 0) {
                    left = 0;
                    top = 0;
                }
                else if(c == 1) {
                    left = 1;
                    top = 0;
                }
                else if(c == 2) {
                    left = 0;
                    top = 1;
                }
                else if(c == 3) {
                    left = 1;
                    top = 1;
                }
                
                dng_rect gainMapArea(top, left, height, width);
                AutoPtr<dng_opcode> gainMapOpCode(new dng_opcode_GainMap(dng_area_spec(gainMapArea, 0, 1, 2, 2), gainMap));
                
                negative->OpcodeList2().Append(gainMapOpCode);
            }
            
            negative->SetModelName("MotionCam");
            negative->SetLocalName("MotionCam");
            
            // We always use RGGB at this point
            negative->SetColorKeys(colorKeyRed, colorKeyGreen, colorKeyBlue);
            
            uint32_t phase = 0;
            
            switch(cameraMetadata.sensorArrangment) {
                case ColorFilterArrangment::GRBG:
                    phase = 0;
                    break;

                default:
                case ColorFilterArrangment::RGGB:
                    phase = 1;
                    break;

                case ColorFilterArrangment::BGGR:
                    phase = 2;
                    break;
                    
                case ColorFilterArrangment::GBRG:
                    phase = 3;
                    break;
            }
            
            negative->SetBayerMosaic(phase);
            negative->SetColorChannels(3);
                        
            negative->SetQuadBlacks(cameraMetadata.blackLevel[0],
                                    cameraMetadata.blackLevel[1],
                                    cameraMetadata.blackLevel[2],
                                    cameraMetadata.blackLevel[3]);
            
            negative->SetWhiteLevel(cameraMetadata.whiteLevel);
            
            // Square pixels
            negative->SetDefaultScale(dng_urational(1,1), dng_urational(1,1));
            
            negative->SetDefaultCropSize(width, height);
            negative->SetNoiseReductionApplied(dng_urational(0,0));
            negative->SetCameraNeutral(dng_vector_3(imageMetadata.asShot[0], imageMetadata.asShot[1], imageMetadata.asShot[2]));

            // Set metadata
            auto exif = negative->Metadata().GetExif();

            exif->SetExposureTime(imageMetadata.exposureTime / 1.0e9);
            exif->fISOSpeedRatings[0] = imageMetadata.iso;
            exif->fISOSpeedRatings[1] = imageMetadata.iso;
            exif->fISOSpeedRatings[2] = imageMetadata.iso;
            exif->SetApertureValue(cameraMetadata.apertures[0]);
                        
            dng_orientation orientation;
            
            switch(imageMetadata.screenOrientation)
            {
                default:
                case ScreenOrientation::PORTRAIT:
                    orientation = dng_orientation::Rotate90CW();
                    break;
                
                case ScreenOrientation::REVERSE_PORTRAIT:
                    orientation = dng_orientation::Rotate90CCW();
                    break;
                    
                case ScreenOrientation::LANDSCAPE:
                    orientation = dng_orientation::Normal();
                    break;
                    
                case ScreenOrientation::REVERSE_LANDSCAPE:
                    orientation = dng_orientation::Rotate180();
                    break;
            }
            
            negative->SetBaseOrientation(orientation);

            // Set up camera profile
            AutoPtr<dng_camera_profile> cameraProfile(new dng_camera_profile());
            
            // Color matrices
            cv::Mat color1 = cameraMetadata.colorMatrix1;
            cv::Mat color2 = cameraMetadata.colorMatrix2;
            
            dng_matrix_3by3 dngColor1 = dng_matrix_3by3(color1.at<float>(0, 0), color1.at<float>(0, 1), color1.at<float>(0, 2),
                                                        color1.at<float>(1, 0), color1.at<float>(1, 1), color1.at<float>(1, 2),
                                                        color1.at<float>(2, 0), color1.at<float>(2, 1), color1.at<float>(2, 2));
            
            dng_matrix_3by3 dngColor2 = dng_matrix_3by3(color2.at<float>(0, 0), color2.at<float>(0, 1), color2.at<float>(0, 2),
                                                        color2.at<float>(1, 0), color2.at<float>(1, 1), color2.at<float>(1, 2),
                                                        color2.at<float>(2, 0), color2.at<float>(2, 1), color2.at<float>(2, 2));
            
            cameraProfile->SetColorMatrix1(dngColor1);
            cameraProfile->SetColorMatrix2(dngColor2);
            
            // Forward matrices
            cv::Mat forward1 = cameraMetadata.forwardMatrix1;
            cv::Mat forward2 = cameraMetadata.forwardMatrix2;
            
            if(!forward1.empty() && !forward2.empty()) {
                dng_matrix_3by3 dngForward1 = dng_matrix_3by3( forward1.at<float>(0, 0), forward1.at<float>(0, 1), forward1.at<float>(0, 2),
                                                               forward1.at<float>(1, 0), forward1.at<float>(1, 1), forward1.at<float>(1, 2),
                                                               forward1.at<float>(2, 0), forward1.at<float>(2, 1), forward1.at<float>(2, 2) );
                
                dng_matrix_3by3 dngForward2 = dng_matrix_3by3( forward2.at<float>(0, 0), forward2.at<float>(0, 1), forward2.at<float>(0, 2),
                                                               forward2.at<float>(1, 0), forward2.at<float>(1, 1), forward2.at<float>(1, 2),
                                                               forward2.at<float>(2, 0), forward2.at<float>(2, 1), forward2.at<float>(2, 2) );
                
                cameraProfile->SetForwardMatrix1(dngForward1);
                cameraProfile->SetForwardMatrix2(dngForward2);
            }
            
            uint32_t illuminant1 = 0;
            uint32_t illuminant2 = 0;
            
            // Convert to DNG format
            switch(cameraMetadata.colorIlluminant1) {
                case color::StandardA:
                    illuminant1 = lsStandardLightA;
                    break;
                case color::StandardB:
                    illuminant1 = lsStandardLightB;
                    break;
                case color::StandardC:
                    illuminant1 = lsStandardLightC;
                    break;
                case color::D50:
                    illuminant1 = lsD50;
                    break;
                case color::D55:
                    illuminant1 = lsD55;
                    break;
                case color::D65:
                    illuminant1 = lsD65;
                    break;
                case color::D75:
                    illuminant1 = lsD75;
                    break;
            }
            
            switch(cameraMetadata.colorIlluminant2) {
                case color::StandardA:
                    illuminant2 = lsStandardLightA;
                    break;
                case color::StandardB:
                    illuminant2 = lsStandardLightB;
                    break;
                case color::StandardC:
                    illuminant2 = lsStandardLightC;
                    break;
                case color::D50:
                    illuminant2 = lsD50;
                    break;
                case color::D55:
                    illuminant2 = lsD55;
                    break;
                case color::D65:
                    illuminant2 = lsD65;
                    break;
                case color::D75:
                    illuminant2 = lsD75;
                    break;
            }
            
            cameraProfile->SetCalibrationIlluminant1(illuminant1);
            cameraProfile->SetCalibrationIlluminant2(illuminant2);
            
            cameraProfile->SetName("MotionCam");
            cameraProfile->SetEmbedPolicy(pepAllowCopying);
            
            // This ensures profile is saved
            cameraProfile->SetWasReadFromDNG();
            
            negative->AddProfile(cameraProfile);
            
            // Finally add the raw data to the negative
            dng_rect dngArea(height, width);
            dng_pixel_buffer dngBuffer;

            AutoPtr<dng_image> dngImage(host.Make_dng_image(dngArea, 1, ttShort));

            dngBuffer.fArea         = dngArea;
            dngBuffer.fPlane        = 0;
            dngBuffer.fPlanes       = 1;
            dngBuffer.fRowStep      = dngBuffer.fPlanes * width;
            dngBuffer.fColStep      = dngBuffer.fPlanes;
            dngBuffer.fPixelType    = ttShort;
            dngBuffer.fPixelSize    = TagTypeSize(ttShort);
            dngBuffer.fData         = rawImage.ptr();
            
            dngImage->Put(dngBuffer);
            
            // Build the DNG images
            negative->SetStage1Image(dngImage);
            negative->BuildStage2Image(host);
            negative->BuildStage3Image(host);
            
            negative->SynchronizeMetadata();

            // Write DNG file to disk
            AutoPtr<dng_image_writer> dngWriter(new dng_image_writer());

            dngWriter->WriteDNG(host, dngStream, *negative.Get(), nullptr, dngVersion_SaveDefault, false);
        }

        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const std::string& outputPath)
        {
            dng_file_stream stream(outputPath.c_str(), true);
            
            WriteDng(rawImage, cameraMetadata, imageMetadata, stream);
            
            stream.Flush();
        }

        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const int fd)
        {
            #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
                dng_fd_stream stream(fd, true);

                WriteDng(rawImage, cameraMetadata, imageMetadata, stream);

                stream.Flush();
            #endif
        }

        void WriteDng(cv::Mat rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      ZipWriter& zipWriter,
                      const std::string& outputName)
        {
            dng_memory_stream stream(gDefaultDNGMemoryAllocator);
            
            WriteDng(rawImage, cameraMetadata, imageMetadata, stream);
            
            stream.Flush();
            
            auto memoryBlock = stream.AsMemoryBlock(gDefaultDNGMemoryAllocator);
            
            try {
                if(memoryBlock)
                    zipWriter.addFile(outputName, memoryBlock->Buffer_uint8(), memoryBlock->LogicalSize());
            }
            catch(std::runtime_error& e) {
            }
            
            delete memoryBlock;
        }
    
        bool EndsWith(const std::string& str, const std::string& ending) {
            if (str.length() >= ending.length()) {
                return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
            }
            else {
                return false;
            }
        }
    }
}
