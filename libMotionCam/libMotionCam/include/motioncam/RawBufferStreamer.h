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
        
        void start(const int fd, const RawCameraMetadata& cameraMetadata);
        void add(const std::shared_ptr<RawImageBuffer>& frame);
        void stop();
        
        void setCropAmount(int horizontal, int vertical);    
        bool isRunning() const;
        
    private:
        void crop(RawImageBuffer& buffer) const;
        void cropAndBin(RawImageBuffer& buffer) const;
        size_t zcompress(RawImageBuffer& inputBuffer, std::vector<uint8_t>& tmpBuffer) const;
        
        void doProcess();
        void doCompress();
        void doStream(const int fd, const RawCameraMetadata& cameraMetadata);
        
    private:
        std::unique_ptr<std::thread> mIoThread;
        std::vector<std::unique_ptr<std::thread>> mProcessThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        int mCropVertical;
        int mCropHorizontal;
        std::atomic<bool> mRunning;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnprocessedBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mReadyBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mCompressedBuffers;
    };

}

#endif
