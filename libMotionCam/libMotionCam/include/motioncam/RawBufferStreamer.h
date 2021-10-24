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
        
        void start(const std::string& outputPath, const int64_t maxMemoryUsageBytes, const RawCameraMetadata& cameraMetadata);
        bool add(std::shared_ptr<RawImageBuffer> frame);
        void stop();
        
        void setCropAmount(int percentage);
        
        bool isRunning() const;
        
    private:
        void doCompress();
        void doStream(std::string outputContainerPath, RawCameraMetadata cameraMetadata);
        
    private:
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        long mMaxMemoryUsageBytes;
        int mCropAmount;
        std::atomic<bool> mRunning;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mRawBufferQueue;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mCompressedBufferQueue;
        
        std::atomic<int64_t> mMemoryUsage;
    };

}

#endif
