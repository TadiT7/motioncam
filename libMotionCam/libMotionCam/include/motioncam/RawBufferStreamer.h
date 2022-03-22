#ifndef RawBufferStreamer_hpp
#define RawBufferStreamer_hpp

#include "motioncam/RawImageMetadata.h"

#include <string>
#include <memory>
#include <vector>
#include <thread>

#include <queue/blockingconcurrentqueue.h>

namespace motioncam {
    struct RawCameraMetadata;
    struct RawImageBuffer;
    class AudioInterface;

    class RawBufferStreamer {
    public:
        RawBufferStreamer();
        ~RawBufferStreamer();
        
        void start(const std::vector<int>& fds,
                   const int& audioFd,
                   const std::shared_ptr<AudioInterface>& audioInterface,
                   const int numThreads,
                   const RawCameraMetadata& cameraMetadata);
        
        void add(const std::shared_ptr<RawImageBuffer>& frame);
        void stop();
        
        void setCropAmount(int width, int height);
        void setBin(bool bin);
        bool isRunning() const;
        float estimateFps() const;
        size_t writenOutputBytes() const;
        int droppedFrames() const;

        void cropAndBin(RawImageBuffer& buffer) const;
        void crop(RawImageBuffer& buffer) const;

    private:
        void doProcess();
        void doStream(const int fd, const RawCameraMetadata& cameraMetadata, const int numContainers);
        
        void processBuffer(const std::shared_ptr<RawImageBuffer>& buffer) const;
        
    private:
        std::shared_ptr<AudioInterface> mAudioInterface;
        int mAudioFd;
        
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mProcessThreads;

        int mCropHeight;
        int mCropWidth;
        bool mBin;
        
        std::atomic<bool> mRunning;
        std::atomic<int> mWrittenFrames;
        std::atomic<int> mAcceptedFrames;
        std::atomic<size_t> mWrittenBytes;
        std::atomic<int> mDroppedFrames;
        std::chrono::steady_clock::time_point mStartTime;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnprocessedBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mReadyBuffers;
    };
}

#endif
