#include "motioncam/RawSequenceConverter.h"
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"

#include "build_bayer.h"

#include <HalideBuffer.h>

namespace motioncam {
#ifdef DNG_SUPPORT
    void ConvertToDNG(const std::string& containerPath, const std::string& outputPath) {
        std::cout << "Opening " << containerPath << std::endl;
        
        RawContainer container(containerPath);
        
        auto frames = container.getFrames();
        
        // Sort frames by timestamp
        std::sort(frames.begin(), frames.end(), [&](auto& a, auto& b) {
            return container.getFrame(a)->metadata.timestampNs < container.getFrame(b)->metadata.timestampNs;
        });
        
        std::cout << "Found " << frames.size() << " frames" << std::endl;
        
        int64_t timestampOffset = 0;
        float timestamp = 0;
                
        for(int i = 0; i < frames.size(); i++) {
            auto frame = container.loadFrame(frames[i]);
            
            if(frame->width <= 0 || frame->height <= 0) {
                std::cout << "Skipping " << frames[i] << std::endl;
                continue;
            }
            
            // Convert from RAW10/16 -> bayer image
            auto* data = frame->data->lock(false);

            auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());
            auto bayerBuffer = Halide::Runtime::Buffer<uint16_t>(frame->width, frame->height);
            
            build_bayer(inputBuffer, frame->rowStride, static_cast<int>(frame->pixelFormat), bayerBuffer);

            frame->data->unlock();
                                
            if(timestampOffset <= 0) {
                timestampOffset = frame->metadata.timestampNs;
            }
            
            timestamp = (frame->metadata.timestampNs - timestampOffset) / (1000.0f*1000.0f*1000.0f);
        
            cv::Mat bayerImage(bayerBuffer.height(), bayerBuffer.width(), CV_16U, bayerBuffer.data());
            std::string outputDngPath = outputPath + "/frame" + std::to_string(i) + ".dng";

            std::cout << "[" << i+1 << "/" << frames.size() << "] [" << std::fixed << std::setprecision(2) << timestamp << "] " << outputDngPath << std::endl;
        
            util::WriteDng(bayerImage, container.getCameraMetadata(), frame->metadata, outputDngPath);
            
            frame->data->release();
        }
        
        float fps = frames.size() / (1e-5f + timestamp);
        
        std::cout << "Estimated FPS: " << std::setprecision(2) << fps << std::endl;
        std::cout << "Done" << std::endl;
    }
#endif
}
