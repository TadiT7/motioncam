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
        bool add(const std::shared_ptr<RawImageBuffer>& frame);
        void stop();
        
        void setCropAmount(int horizontal, int vertical);
        
        bool isRunning() const;
        
    private:
        std::shared_ptr<RawImageBuffer> crop(const std::shared_ptr<RawImageBuffer>& buffer) const;
        std::shared_ptr<RawImageBuffer> compressBuffer(const std::shared_ptr<RawImageBuffer>& buffer, std::vector<uint8_t>& tmpBuffer) const;
        void doCompress();
        void doStream(const std::string& outputContainerPath, const RawCameraMetadata& cameraMetadata);
        
    private:
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        long mMaxMemoryUsageBytes;
        int mCropVertical;
        int mCropHorizontal;
        std::atomic<bool> mRunning;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mRawBufferQueue;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mCompressedBufferQueue;
        
        std::atomic<int64_t> mMemoryUsage;
    };

}

#endif
