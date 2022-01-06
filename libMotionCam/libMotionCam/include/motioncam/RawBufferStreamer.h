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
                   const std::shared_ptr<AudioInterface> audioInterface,
                   const bool enableCompression,
                   const int numThreads,
                   const RawCameraMetadata& cameraMetadata);
        
        void add(const std::shared_ptr<RawImageBuffer>& frame);
        void stop();
        
        void setCropAmount(int width, int height);
        void setBin(bool bin);
        bool isRunning() const;
        float estimateFps() const;
        size_t writenOutputBytes() const;

        size_t cropAndBin_RAW10(RawImageBuffer& buffer,
                                uint8_t* data,
                                const int16_t ystart,
                                const int16_t yend,
                                const int16_t xstart,
                                const int16_t xend,
                                const int16_t binnedWidth,
                                const bool doCompress) const;

        size_t cropAndBin_RAW12(RawImageBuffer& buffer,
                                uint8_t* data,
                                const int16_t ystart,
                                const int16_t yend,
                                const int16_t xstart,
                                const int16_t xend,
                                const int16_t binnedWidth,
                                const bool doCompress) const;

        size_t cropAndBin_RAW16(RawImageBuffer& buffer,
                                uint8_t* data,
                                const int16_t ystart,
                                const int16_t yend,
                                const int16_t xstart,
                                const int16_t xend,
                                const int16_t binnedWidth,
                                const bool doCompress) const;

        void cropAndBin(RawImageBuffer& buffer) const;
        
        void crop(RawImageBuffer& buffer) const;
        void cropAndCompress(RawImageBuffer& buffer) const;

    private:
        void doProcess();
        void doStream(const int fd, const RawCameraMetadata& cameraMetadata, const int numContainers);
        
        void processBuffer(std::shared_ptr<RawImageBuffer> buffer);
        
    private:
        std::shared_ptr<AudioInterface> mAudioInterface;
        int mAudioFd;
        
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mProcessThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        int mCropHeight;
        int mCropWidth;
        bool mBin;
        bool mEnableCompression;
        
        std::atomic<bool> mRunning;
        std::atomic<uint32_t> mWrittenFrames;
        std::atomic<uint32_t> mAcceptedFrames;
        std::atomic<size_t> mWrittenBytes;
        std::chrono::steady_clock::time_point mStartTime;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnprocessedBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mReadyBuffers;
    };
}

#endif
