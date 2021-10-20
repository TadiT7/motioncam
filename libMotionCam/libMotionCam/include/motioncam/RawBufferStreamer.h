#ifndef RawBufferStreamer_hpp
#define RawBufferStreamer_hpp

#include <string>
#include <memory>
#include <vector>
#include <thread>

#include <queue/blockingconcurrentqueue.h>

namespace motioncam {
    struct RawCameraMetadata;
    struct RawImageBuffer;

    class RawBufferStreamer {
    public:
        RawBufferStreamer();
        ~RawBufferStreamer();
        
        void start(const std::string& outputPath, const RawCameraMetadata& cameraMetadata);
        void add(std::shared_ptr<RawImageBuffer> frame);        
        void stop();
        
        bool isRunning() const;
        
    private:
        void doCompress();
        void doStream(std::string outputContainerPath, RawCameraMetadata cameraMetadata);
        
    private:
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        std::string mContainerPath;
        std::atomic<bool> mRunning;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mRawBufferQueue;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mCompressedBufferQueue;
        
        std::atomic<size_t> mMemoryUsage;
    };

}

#endif
