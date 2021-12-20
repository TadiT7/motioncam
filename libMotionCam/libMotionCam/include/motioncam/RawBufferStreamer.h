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
    class AudioInterface;

    class RawBufferStreamer {
    public:
        RawBufferStreamer();
        ~RawBufferStreamer();
        
        void start(const std::vector<int>& fds,
                   const int& audioFd,
                   const std::shared_ptr<AudioInterface> audioInterface,
                   const RawCameraMetadata& cameraMetadata);
        
        void add(const std::shared_ptr<RawImageBuffer>& frame);
        void stop();
        
        void setCropAmount(int horizontal, int vertical);
        void setBin(bool bin);
        bool isRunning() const;
        float estimateFps() const;
        size_t writenOutputBytes() const;

        void cropAndBin_RAW10(RawImageBuffer& buffer,
                              uint8_t* data,
                              const int16_t ystart,
                              const int16_t yend,
                              const int16_t xstart,
                              const int16_t xend,
                              const int16_t binnedWidth) const;

        void cropAndBin_RAW12(RawImageBuffer& buffer,
                              uint8_t* data,
                              const int16_t ystart,
                              const int16_t yend,
                              const int16_t xstart,
                              const int16_t xend,
                              const int16_t binnedWidth) const;

        void cropAndBin_RAW16(RawImageBuffer& buffer,
                              uint8_t* data,
                              const int16_t ystart,
                              const int16_t yend,
                              const int16_t xstart,
                              const int16_t xend,
                              const int16_t binnedWidth) const;

        void cropAndBin(RawImageBuffer& buffer) const;

    private:
        void crop(RawImageBuffer& buffer) const;
        size_t zcompress(RawImageBuffer& inputBuffer, std::vector<uint8_t>& tmpBuffer) const;
        
        void doProcess();
        void doCompress();
        void doStream(const int fd, const RawCameraMetadata& cameraMetadata);
        
        void processBuffer(std::shared_ptr<RawImageBuffer> buffer);
        
    private:
        std::shared_ptr<AudioInterface> mAudioInterface;
        int mAudioFd;
        
        std::vector<std::unique_ptr<std::thread>> mIoThreads;
        std::vector<std::unique_ptr<std::thread>> mProcessThreads;
        std::vector<std::unique_ptr<std::thread>> mCompressThreads;
        
        int mCropVertical;
        int mCropHorizontal;
        bool mBin;
        
        std::atomic<bool> mRunning;
        std::atomic<uint32_t> mWrittenFrames;
        std::atomic<size_t> mWrittenBytes;
        std::chrono::steady_clock::time_point mStartTime;
        
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnprocessedBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mReadyBuffers;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mCompressedBuffers;
    };

}

#endif
