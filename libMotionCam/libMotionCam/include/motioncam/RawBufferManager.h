#ifndef RawBufferManager_hpp
#define RawBufferManager_hpp

#include "motioncam/RawImageMetadata.h"
#include "motioncam/RawBufferStreamer.h"

#include <queue/concurrentqueue.h>
#include <set>
#include <vector>
#include <memory>
#include <mutex>

namespace motioncam {
    class RawContainer;
    class AudioInterface;

    class RawBufferManager {
    public:
        // Not copyable
        RawBufferManager(const RawBufferManager&) = delete;
        RawBufferManager& operator=(const RawBufferManager&) = delete;

        static RawBufferManager& get() {
            static RawBufferManager instance;
            return instance;
        }
        
        struct LockedBuffers {
        public:
            ~LockedBuffers();
            std::vector<std::shared_ptr<RawImageBuffer>> getBuffers() const;
            
            friend class RawBufferManager;

        private:
            LockedBuffers(std::vector<std::shared_ptr<RawImageBuffer>> buffers);
            LockedBuffers();
            
            const std::vector<std::shared_ptr<RawImageBuffer>> mBuffers;
        };
        
        void addBuffer(std::shared_ptr<RawImageBuffer>& buffer);
        void recordingStats(size_t& outMemoryUseBytes, float& outFps, size_t& outOutputSizeBytes);
        size_t memoryUseBytes() const;
        int numBuffers() const;
        void reset();
        void setTargetMemory(size_t memoryUseBytes);

        std::shared_ptr<RawImageBuffer> dequeueUnusedBuffer();
        void enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);
        void returnBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);

        int numHdrBuffers();
        int64_t latestTimeStamp();
        
        std::shared_ptr<RawContainer> popPendingContainer();
        
        std::unique_ptr<LockedBuffers> consumeLatestBuffer();
        std::unique_ptr<LockedBuffers> consumeAllBuffers();
        std::unique_ptr<LockedBuffers> consumeBuffer(int64_t timestampNs);
        
        void saveHdr(int numSaveBuffers,
                     int64_t referenceTimestampNs,
                     const RawCameraMetadata& metadata,
                     const PostProcessSettings& settings,
                     const std::string& outputPath);

        void save(RawCameraMetadata& metadata,
                  int64_t referenceTimestampNs,
                  int numSaveBuffers,
                  const PostProcessSettings& settings,
                  const std::string& outputPath);
        
        void enableStreaming(const std::vector<int>& fds,
                             const int audioFd,
                             std::shared_ptr<AudioInterface> audioInterface,
                             const bool enableCompression,
                             const int numThreads,
                             const RawCameraMetadata& metadata);
        
        void setCropAmount(int horizontal, int vertical);
        void setVideoBin(bool bin);
        void endStreaming();
        float bufferSpaceUse();
        
    private:
        RawBufferManager();

        int mHorizontalCrop;
        int mVerticalCrop;
        bool mBin;

        std::atomic<size_t> mMemoryUseBytes;
        std::atomic<size_t> mMemoryTargetBytes;
        std::atomic<int> mNumBuffers;
                
        std::recursive_mutex mMutex;
        
        std::vector<std::shared_ptr<RawImageBuffer>> mReadyBuffers;

        moodycamel::ConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnusedBuffers;
        moodycamel::ConcurrentQueue<std::unique_ptr<RawContainer>> mPendingContainers;
        
        std::shared_ptr<RawBufferStreamer> mStreamer;
    };

} // namespace motioncam

#endif // RawBufferManager_hpp
