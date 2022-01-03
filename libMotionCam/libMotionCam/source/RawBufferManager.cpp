#include "motioncam/RawBufferManager.h"

#include <utility>

#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"
#include "motioncam/Lock.h"

namespace motioncam {
    static const bool AlwaysSaveToDisk = false;
    static const int NumContainersToKeepInMemory = 2;

    RawBufferManager::RawBufferManager() :
        mMemoryUseBytes(0),
        mMemoryTargetBytes(0),
        mNumBuffers(0),
        mStreamer(new RawBufferStreamer())
    {
    }

    RawBufferManager::LockedBuffers::LockedBuffers() = default;
    RawBufferManager::LockedBuffers::LockedBuffers(
        std::vector<std::shared_ptr<RawImageBuffer>> buffers) : mBuffers(std::move(buffers)) {}

    std::vector<std::shared_ptr<RawImageBuffer>> RawBufferManager::LockedBuffers::getBuffers() const {
        return mBuffers;
    }

    RawBufferManager::LockedBuffers::~LockedBuffers() {
        RawBufferManager::get().returnBuffers(mBuffers);
    }

    void RawBufferManager::addBuffer(std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.enqueue(buffer);
        
        ++mNumBuffers;
        
        mMemoryUseBytes += static_cast<int>(buffer->data->len());
        mMemoryTargetBytes = static_cast<size_t>(mMemoryUseBytes);
    }

    int RawBufferManager::numBuffers() const {
        return mNumBuffers;
    }

    void RawBufferManager::recordingStats(size_t& outMemoryUseBytes, float& outFps, size_t& outOutputSizeBytes) const {
        outMemoryUseBytes = mMemoryUseBytes;
        outFps = mStreamer->estimateFps();
        outOutputSizeBytes = mStreamer->writenOutputBytes();
    }

    size_t RawBufferManager::memoryUseBytes() const {
        return mMemoryUseBytes;
    }

    void RawBufferManager::reset() {
        std::shared_ptr<RawImageBuffer> buffer;
        while(mUnusedBuffers.try_dequeue(buffer)) {
        }

        {
            Lock lock(mMutex, "reset()");
            mReadyBuffers.clear();
        }
        
        mNumBuffers = 0;
        mMemoryUseBytes = 0;
        mMemoryTargetBytes = 0;
    }

    void RawBufferManager::setTargetMemory(size_t memoryUseBytes) {
        mMemoryTargetBytes = memoryUseBytes;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::dequeueUnusedBuffer() {
        std::shared_ptr<RawImageBuffer> buffer;

        if(mUnusedBuffers.try_dequeue(buffer)) {
            return buffer;
        }
        
        {
            Lock lock(mMutex, "dequeueUnusedBuffer()");

            if(mMemoryUseBytes <= mMemoryTargetBytes) {
                if(!mReadyBuffers.empty()) {
                    buffer = mReadyBuffers.front();
                    mReadyBuffers.erase(mReadyBuffers.begin());

                    return buffer;
                }
            }
            else {
                // Shrink memory
                while(true) {
                    if(mReadyBuffers.size() > 0 && mMemoryUseBytes > mMemoryTargetBytes) {
                        buffer = mReadyBuffers.front();
                        mReadyBuffers.erase(mReadyBuffers.begin());

                        mMemoryUseBytes -= buffer->data->len();
                        --mNumBuffers;

                        logger::log("Shrinking memory to " + std::to_string(mMemoryUseBytes));
                    }
                    else {
                        if(!mReadyBuffers.empty()) {
                            buffer = mReadyBuffers.front();
                            mReadyBuffers.erase(mReadyBuffers.begin());

                            return buffer;
                        }
                    }
                }
                
                return buffer;
            }
        }
                
        return nullptr;
    }

    void RawBufferManager::enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        Lock lock(mMutex, "enqueueReadyBuffer()");

        if(mStreamer->isRunning()) {
            mStreamer->add(buffer);
        }
        else
            mReadyBuffers.push_back(buffer);
    }

    int RawBufferManager::numHdrBuffers() {
        Lock lock(mMutex, "numHdrBuffers()");
        
        int hdrBuffers = 0;
        
        for(auto& e : mReadyBuffers) {
            if(e->metadata.rawType == RawType::HDR) {
                ++hdrBuffers;
            }
        }
        
        return hdrBuffers;
    }

    void RawBufferManager::discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.enqueue(buffer);
    }

    void RawBufferManager::discardBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers) {
        mUnusedBuffers.enqueue_bulk(buffers.begin(), buffers.size());
    }

    void RawBufferManager::returnBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers) {
        Lock lock(mMutex, "returnBuffers()");

        std::move(buffers.begin(), buffers.end(), std::back_inserter(mReadyBuffers));
    }

    void RawBufferManager::saveHdr(int numSaveBuffers,
                                   int64_t referenceTimestampNs,
                                   const RawCameraMetadata& metadata,
                                   const PostProcessSettings& settings,
                                   const std::string& outputPath)
    {
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        {
            Lock lock(mMutex, "saveHdr()");

            if (mReadyBuffers.empty() || numSaveBuffers < 1)
                return;

            std::vector<std::shared_ptr<RawImageBuffer>> zslBuffers, hdrBuffers;

            // Find the HDR buffers first
            for(auto & mReadyBuffer : mReadyBuffers) {
                if(mReadyBuffer->metadata.rawType == RawType::HDR) {
                    hdrBuffers.push_back(mReadyBuffer);
                }
            }

            // Sort them by timestmp
            std::sort(hdrBuffers.begin(), hdrBuffers.end(), [](std::shared_ptr<RawImageBuffer> a, std::shared_ptr<RawImageBuffer> b) {
                return a->metadata.timestampNs < b->metadata.timestampNs;
            });

            numSaveBuffers = numSaveBuffers - (int) hdrBuffers.size();
            numSaveBuffers = std::max(0, numSaveBuffers);
            
            int64_t hdrTimestamp = std::numeric_limits<int64_t>::max();
            
            // Pick images older than HDR images because the auto exposure changes after the first HDR capture
            if(!hdrBuffers.empty()) {
                hdrTimestamp = hdrBuffers.back()->metadata.timestampNs;
            }

            for(auto & mReadyBuffer : mReadyBuffers) {
                if(mReadyBuffer->metadata.rawType == RawType::ZSL &&
                   mReadyBuffer->metadata.timestampNs < hdrTimestamp)
                {
                    zslBuffers.push_back(mReadyBuffer);
                }
            }
            
            std::sort(zslBuffers.begin(), zslBuffers.end(), [](std::shared_ptr<RawImageBuffer> a, std::shared_ptr<RawImageBuffer> b) {
                return a->metadata.timestampNs < b->metadata.timestampNs;
            });

            numSaveBuffers = std::min(numSaveBuffers, (int) zslBuffers.size());
            int numToRemove = ((int)zslBuffers.size() - numSaveBuffers);
            
            zslBuffers.erase(zslBuffers.begin(), zslBuffers.begin() + numToRemove);

            // Set reference timestamp
            if(!zslBuffers.empty())
                referenceTimestampNs = zslBuffers.back()->metadata.timestampNs;
            else if(!hdrBuffers.empty())
                referenceTimestampNs = hdrBuffers.back()->metadata.timestampNs;
            else {
                logger::log("No buffers. Something is not right");
                return;
            }

            buffers.insert(buffers.end(), hdrBuffers.begin(), hdrBuffers.end());
            buffers.insert(buffers.end(), zslBuffers.begin(), zslBuffers.end());
            
            // Remove from the ready buffers until we copy them
            mReadyBuffers.erase(
                std::remove_if(
                    mReadyBuffers.begin(),
                    mReadyBuffers.end(),
                    [buffers](std::shared_ptr<RawImageBuffer>& e) { return std::find(buffers.begin(), buffers.end(), e) != buffers.end(); }),
                mReadyBuffers.end());
        }

        // Copy the buffers
        auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                referenceTimestampNs,
                true,
                buffers);

        // Return buffers
        auto it = buffers.begin();
        while(it != buffers.end()) {
            mUnusedBuffers.enqueue(*it);
            ++it;
        }

        // Save the container
        if(AlwaysSaveToDisk || mPendingContainers.size_approx() > NumContainersToKeepInMemory) {
            rawContainer->save(outputPath);
        }
        else {
            mPendingContainers.enqueue(rawContainer);
        }
    }

    void RawBufferManager::save(RawCameraMetadata& metadata,
                                int64_t referenceTimestampNs,
                                int numSaveBuffers,
                                const PostProcessSettings& settings,
                                const std::string& outputPath)
    {
        Measure measure("RawBufferManager::save()");
        
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        if(numSaveBuffers < 1)
            return;

        {
            Lock lock(mMutex, "save()");

            if(mReadyBuffers.empty())
                return;

            // Find reference frame
            int referenceIdx = static_cast<int>(mReadyBuffers.size()) - 1;

            for(int i = 0; i < mReadyBuffers.size(); i++) {
                if(mReadyBuffers[i]->metadata.timestampNs == referenceTimestampNs) {
                    referenceIdx = i;
                    buffers.push_back(mReadyBuffers[i]);
                    --numSaveBuffers;
                    break;
                }
            }

            // Update timestamp
            referenceTimestampNs = mReadyBuffers[referenceIdx]->metadata.timestampNs;

            // Add closest images
            int leftIdx  = referenceIdx - 1;
            int rightIdx = referenceIdx + 1;

            while(numSaveBuffers > 0 && (leftIdx > 0 || rightIdx < mReadyBuffers.size())) {
                int64_t leftDifference = std::numeric_limits<long>::max();
                int64_t rightDifference = std::numeric_limits<long>::max();

                if(leftIdx >= 0)
                    leftDifference = std::abs(mReadyBuffers[leftIdx]->metadata.timestampNs - mReadyBuffers[referenceIdx]->metadata.timestampNs);

                if(rightIdx < mReadyBuffers.size())
                    rightDifference = std::abs(mReadyBuffers[rightIdx]->metadata.timestampNs - mReadyBuffers[referenceIdx]->metadata.timestampNs);

                // Add closest buffer to reference
                if(leftDifference < rightDifference) {
                    buffers.push_back(mReadyBuffers[leftIdx]);
                    mReadyBuffers[leftIdx] = nullptr;

                    --leftIdx;
                }
                else {
                    buffers.push_back(mReadyBuffers[rightIdx]);
                    mReadyBuffers[rightIdx] = nullptr;

                    ++rightIdx;
                }

                --numSaveBuffers;
            }

            // Clear the reference frame
            mReadyBuffers[referenceIdx] = nullptr;

            // Clear out buffers we intend to copy
            auto it = mReadyBuffers.begin();
            while(it != mReadyBuffers.end()) {
                if(*it == nullptr) {
                    it = mReadyBuffers.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        // Copy the buffers
        auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                referenceTimestampNs,
                false,
                buffers);

        // Return buffers
        {
            Lock lock(mMutex, "save()");
            mReadyBuffers.insert(mReadyBuffers.end(), buffers.begin(), buffers.end());
        }

        // Save container
        if(AlwaysSaveToDisk || mPendingContainers.size_approx() > NumContainersToKeepInMemory) {
            rawContainer->save(outputPath);
        }
        else {
            mPendingContainers.enqueue(rawContainer);
        }
    }

    std::shared_ptr<RawContainer> RawBufferManager::popPendingContainer() {
        std::shared_ptr<RawContainer> container;
        mPendingContainers.try_dequeue(container);
        
        return container;
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeLatestBuffer() {
        Lock lock(mMutex, "consumeLatestBuffer()");

        if(mReadyBuffers.empty()) {
            return std::unique_ptr<LockedBuffers>(new LockedBuffers());
        }

        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        std::move(mReadyBuffers.end() - 1, mReadyBuffers.end(), std::back_inserter(buffers));
        mReadyBuffers.erase(mReadyBuffers.end() - 1, mReadyBuffers.end());

        return std::unique_ptr<LockedBuffers>(new LockedBuffers(buffers));
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeBuffer(int64_t timestampNs) {
        Lock lock(mMutex, "consumeBuffer()");

        auto it = std::find_if(
            mReadyBuffers.begin(), mReadyBuffers.end(),
            [&](const std::shared_ptr<RawImageBuffer>& x) { return x->metadata.timestampNs == timestampNs; }
        );

        if(it != mReadyBuffers.end()) {
            auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers( { *it }));
            mReadyBuffers.erase(it);

            return lockedBuffers;
        }

        return std::unique_ptr<LockedBuffers>(new LockedBuffers());
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeAllBuffers() {
        Lock lock(mMutex, "consumeAllBuffers()");

        auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers(mReadyBuffers));
        mReadyBuffers.clear();

        return lockedBuffers;
    }

    int64_t RawBufferManager::latestTimeStamp() {
        Lock lock(mMutex, "latestTimeStamp()");
        
        if(mReadyBuffers.empty())
            return -1;
        
        auto latest = mReadyBuffers.back();
        return latest->metadata.timestampNs;
    }

    void RawBufferManager::enableStreaming(const std::vector<int>& fds,
                                           const int audioFd,
                                           std::shared_ptr<AudioInterface> audioInterface,
                                           const bool enableCompression,
                                           const int numThreads,
                                           const RawCameraMetadata& metadata)
    {
        // Clear out buffers before streaming
        {
            consumeAllBuffers();
        }
        
        mStreamer->start(fds, audioFd, audioInterface, enableCompression, numThreads, metadata);
    }

    void RawBufferManager::setCropAmount(int horizontal, int vertical) {
        mStreamer->setCropAmount(horizontal, vertical);
    }

    void RawBufferManager::setVideoBin(bool bin) {
        mStreamer->setBin(bin);
    }

    float RawBufferManager::bufferSpaceUse() {
        Lock lock(mMutex, "bufferSpaceUse()");

        float bufferUseAmount = (mNumBuffers - (mReadyBuffers.size() + mUnusedBuffers.size_approx())) / (float) mNumBuffers;

        bufferUseAmount = std::max(0.0f, bufferUseAmount);
        bufferUseAmount = std::min(1.0f, bufferUseAmount);

        return bufferUseAmount;
    }

    void RawBufferManager::endStreaming() {
        mStreamer->stop();
    }
}
