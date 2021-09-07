#include "motioncam/RawBufferManager.h"

#include <utility>
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"

namespace motioncam {

    static std::vector<std::shared_ptr<RawImageBuffer>> FindNearestBuffers(
        const std::vector<std::shared_ptr<RawImageBuffer>>& buffers, RawType type, int64_t timestampNs, int numBuffers) {
     
        if(numBuffers <= 0)
            return std::vector<std::shared_ptr<RawImageBuffer>>();
        
        std::vector<std::shared_ptr<RawImageBuffer>> sortedBuffers = buffers;
        
        // Sort by distance to reference timestamp
        std::sort(sortedBuffers.begin(), sortedBuffers.end(), [timestampNs](auto a, auto b) {
            return std::abs(a->metadata.timestampNs - timestampNs) < std::abs(b->metadata.timestampNs - timestampNs);
        });
        
        // Filter out by type
        sortedBuffers.erase(
            std::remove_if(sortedBuffers.begin(), sortedBuffers.end(), [type](auto x) { return x->metadata.rawType != type; }),
            sortedBuffers.end());
        
        numBuffers = std::min((int) sortedBuffers.size(), numBuffers);
        
        return std::vector<std::shared_ptr<RawImageBuffer>>(sortedBuffers.begin(), sortedBuffers.begin() + numBuffers);
    }

    RawBufferManager::RawBufferManager() :
        mMemoryUseBytes(0),
        mNumBuffers(0)
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
    }

    int RawBufferManager::numBuffers() const {
        return mNumBuffers;
    }

    int RawBufferManager::memoryUseBytes() const {
        return mMemoryUseBytes;
    }

    void RawBufferManager::reset() {
        std::shared_ptr<RawImageBuffer> buffer;
        while(mUnusedBuffers.try_dequeue(buffer)) {
        }

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            mReadyBuffers.clear();
        }
        
        mNumBuffers = 0;
        mMemoryUseBytes = 0;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::dequeueUnusedBuffer() {
        std::shared_ptr<RawImageBuffer> buffer;

        if(mUnusedBuffers.try_dequeue(buffer)) {
            return buffer;
        }
        
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if(!mReadyBuffers.empty()) {
                buffer = mReadyBuffers.front();
                mReadyBuffers.erase(mReadyBuffers.begin());

                return buffer;
            }
        }

        return nullptr;
    }

    void RawBufferManager::enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        
        mReadyBuffers.push_back(buffer);
    }

    int RawBufferManager::numHdrBuffers() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        
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
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        std::move(buffers.begin(), buffers.end(), std::back_inserter(mReadyBuffers));
    }

    void RawBufferManager::save(
            RawType type,
            int numSaveBuffers,
            int64_t referenceTimestampNs,
            const RawCameraMetadata& metadata,
            const PostProcessSettings& settings,
            const std::string& outputPath)
    {
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if (mReadyBuffers.empty() || numSaveBuffers <= 0)
                return;

            std::vector<std::shared_ptr<RawImageBuffer>> typedBuffers, remainingBuffers;
            
            // Sort buffers into requested type and remaining types
            for(int i = 0; i < mReadyBuffers.size(); i++) {
                if(mReadyBuffers[i]->metadata.rawType == type && numSaveBuffers > 0) {
                    typedBuffers.push_back(mReadyBuffers[i]);
                }
                else if(mReadyBuffers[i]->metadata.timestampNs <= referenceTimestampNs) {
                    remainingBuffers.push_back(mReadyBuffers[i]);
                }
            }
            
            // Sort remaining buffers
            std::sort(remainingBuffers.begin(), remainingBuffers.end(), [](auto a, auto b) {
                return a->metadata.timestampNs > b->metadata.timestampNs;
            });
            
            numSaveBuffers = std::min((int) remainingBuffers.size(), std::max(0, numSaveBuffers - (int) typedBuffers.size()));

            // Keep just the remaining buffers closest to the reference time stamp
            remainingBuffers.resize(numSaveBuffers);
            
            if(!remainingBuffers.empty()) {
                // Front will be closest to reference
                referenceTimestampNs = remainingBuffers.front()->metadata.timestampNs;
            }
            else if(!typedBuffers.empty()) {
                int64_t closestDiff = 1e10;
                int64_t timestamp = referenceTimestampNs;
                
                // Get closest to reference
                for(int i = 0; i < typedBuffers.size(); i++) {
                    auto diff = std::abs(typedBuffers[i]->metadata.timestampNs - referenceTimestampNs);
                    
                    if(diff < closestDiff) {
                        timestamp = typedBuffers[i]->metadata.timestampNs;
                        closestDiff = diff;
                    }
                }
                
                referenceTimestampNs = timestamp;
            }
            
            // Combine the buffers
            buffers.insert(buffers.end(), typedBuffers.begin(), typedBuffers.end());
            buffers.insert(buffers.end(), remainingBuffers.begin(), remainingBuffers.end());
                        
            if(buffers.empty()) {
                logger::log("No buffers to save!");
                return;
            }
            
            // Remove from the ready buffers until we copy them
            mReadyBuffers.erase(
                std::remove_if(
                    mReadyBuffers.begin(),
                    mReadyBuffers.end(),
                    [buffers](auto& e) { return std::find(buffers.begin(), buffers.end(), e) != buffers.end(); }),
                mReadyBuffers.end());
        }

        // Copy the buffers
        auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                referenceTimestampNs,
                type == RawType::HDR,
                buffers);

        // Return buffers
        auto it = buffers.begin();
        while(it != buffers.end()) {
            mUnusedBuffers.enqueue(*it);
            ++it;
        }

        // Save the container
        if(mPendingContainers.size_approx() > 1) {
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

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if(mReadyBuffers.empty())
                return;

            // Find reference frame
            int referenceIdx = static_cast<int>(mReadyBuffers.size()) - 1;

            for(int i = 0; i < mReadyBuffers.size(); i++) {
                if(mReadyBuffers[i]->metadata.timestampNs == referenceTimestampNs) {
                    referenceIdx = i;
                    buffers.push_back(mReadyBuffers[i]);
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
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            mReadyBuffers.insert(mReadyBuffers.end(), buffers.begin(), buffers.end());
        }

        // Save container
        if(mPendingContainers.size_approx() > 1) {
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
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        if(mReadyBuffers.empty()) {
            return std::unique_ptr<LockedBuffers>(new LockedBuffers());
        }

        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        std::move(mReadyBuffers.end() - 1, mReadyBuffers.end(), std::back_inserter(buffers));
        mReadyBuffers.erase(mReadyBuffers.end() - 1, mReadyBuffers.end());

        return std::unique_ptr<LockedBuffers>(new LockedBuffers(buffers));
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeBuffer(int64_t timestampNs) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto it = std::find_if(
            mReadyBuffers.begin(), mReadyBuffers.end(),
            [&](const auto& x) { return x->metadata.timestampNs == timestampNs; }
        );

        if(it != mReadyBuffers.end()) {
            auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers( { *it }));
            mReadyBuffers.erase(it);

            return lockedBuffers;
        }

        return std::unique_ptr<LockedBuffers>(new LockedBuffers());
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeAllBuffers() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers(mReadyBuffers));
        mReadyBuffers.clear();

        return lockedBuffers;
    }

    int64_t RawBufferManager::latestTimeStamp() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        
        if(mReadyBuffers.empty())
            return -1;
        
        auto latest = mReadyBuffers.back();
        return latest->metadata.timestampNs;
    }
}
