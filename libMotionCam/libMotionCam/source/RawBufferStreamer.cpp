#include "motioncam/RawBufferStreamer.h"
#include "motioncam/RawContainer.h"
#include "motioncam/RawBufferManager.h"
#include "motioncam/Logger.h"
#include "motioncam/Util.h"
#include "motioncam/Measure.h"
#include "motioncam/AudioInterface.h"
#include "motioncam/RawImageBuffer.h"
#include "motioncam/RawCameraMetadata.h"
#include "motioncam/RawEncoder.h"

#include <tinywav.h>
#include <memory>

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
    #include <unistd.h>
#endif

namespace motioncam {
    const int SoundSampleRateHz       = 48000;
    const int SoundChannelCount       = 2;

    RawBufferStreamer::RawBufferStreamer() :
        mRunning(false),
        mAudioFd(-1),
        mCropHeight(0),
        mCropWidth(0),
        mBin(false),
        mWrittenFrames(0),
        mAcceptedFrames(0),
        mWrittenBytes(0)
    {
    }

    RawBufferStreamer::~RawBufferStreamer() {
        stop();
    }

    void RawBufferStreamer::start(const std::vector<int>& fds,
                                  const int& audioFd,
                                  const std::shared_ptr<AudioInterface>& audioInterface,
                                  const int numThreads,
                                  const RawCameraMetadata& cameraMetadata) {
        stop();
        
        if(fds.empty()) {
            logger::log("No file descriptors found");
            return;
        }
        
        mRunning = true;
        mWrittenFrames = 0;
        mWrittenBytes = 0;
        mAcceptedFrames = 0;
        
        // Start audio interface
        if(audioInterface && audioFd >= 0) {
            mAudioInterface = audioInterface;
            mAudioFd = audioFd;
            
            mAudioInterface->start(SoundSampleRateHz, SoundChannelCount);
        }

        mStartTime = std::chrono::steady_clock::now();
        
        // Create IO threads with maximum priority
        for(int i = 0; i < fds.size(); i++) {
            auto ioThread = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doStream, this, fds[i], cameraMetadata, (int)fds.size()));

        #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
            // Update priority
            sched_param priority{};
            priority.sched_priority = 99;

            pthread_setschedparam(ioThread->native_handle(), SCHED_FIFO, &priority);
        #endif

            mIoThreads.push_back(std::move(ioThread));
        }
                
        // Create process threads
        int processThreads = (std::max)(numThreads, 1);

        for(int i = 0; i < processThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doProcess, this));
            
            mProcessThreads.push_back(std::move(t));
        }
    }

    void RawBufferStreamer::add(const std::shared_ptr<RawImageBuffer>& frame) {
        mUnprocessedBuffers.enqueue(frame);
        mAcceptedFrames++;
    }

    void RawBufferStreamer::stop() {
        mRunning = false;

        if(mAudioInterface) {
            mAudioInterface->stop();
            
            // Flush to disk
            uint32_t numFrames = 0;
            auto& buffer = mAudioInterface->getAudioData(numFrames);

            FILE* audioFile = fdopen(mAudioFd, "w");
            if(audioFile != nullptr) {
                TinyWav tw = {nullptr};

                if(tinywav_open_write_f(
                    &tw,
                    mAudioInterface->getChannels(),
                    mAudioInterface->getSampleRate(),
                    TW_INT16,
                    TW_INTERLEAVED,
                    audioFile) == 0)
                {
                    tinywav_write_f(&tw, (void*)buffer.data(), (int)(numFrames));
                }
                
                tinywav_close_write(&tw);
            }
            else {
                #if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
                    close(mAudioFd);
                #endif
            }
        }
        
        mAudioInterface = nullptr;
        mAudioFd = -1;

        for(auto& thread : mProcessThreads) {
            thread->join();
        }
        
        mProcessThreads.clear();

        for(auto& thread : mIoThreads) {
            thread->join();
        }
        
        mIoThreads.clear();
    }

    void RawBufferStreamer::setCropAmount(int width, int height) {
        // Only allow cropping when not running
        if(!mRunning) {
            mCropHeight = height;
            mCropWidth = width;
        }
    }

    void RawBufferStreamer::setBin(bool bin) {
        mBin = bin;
    }

    void RawBufferStreamer::cropAndBin(RawImageBuffer& buffer) const {
        //Measure m("cropAndBin");
        
        const int horizontalCrop = static_cast<const int>(4 * (lround(0.5f * (mCropWidth/100.0 * buffer.width)) / 4));

        // Even vertical crop to match bayer pattern
        const int verticalCrop   = static_cast<const int>(2 * (lround(0.5f * (mCropHeight/100.0 * buffer.height)) / 2));

        const int croppedWidth  = static_cast<const int>(buffer.width - horizontalCrop*2);
        const int croppedHeight = static_cast<const int>(buffer.height - verticalCrop*2);
        
        const int ystart = verticalCrop;
        const int yend = buffer.height - ystart;

        const int xstart = horizontalCrop;
        const int xend = buffer.width - horizontalCrop;

        auto data = buffer.data->lock(true);
        size_t end;

        if(buffer.pixelFormat == PixelFormat::RAW10) {
            end = encoder::encodeAndBin(data, encoder::ANDROID_RAW10, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else if(buffer.pixelFormat == PixelFormat::RAW12) {
            end = encoder::encodeAndBin(data, encoder::ANDROID_RAW12, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else if(buffer.pixelFormat == PixelFormat::RAW16) {
            end = encoder::encodeAndBin(data, encoder::ANDROID_RAW16, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else {
            // Not supported
            buffer.data->unlock();
            return;
        }

        buffer.data->unlock();

        buffer.width = croppedWidth / 2;
        buffer.height = croppedHeight / 2;
        buffer.isBinned = true;
        buffer.pixelFormat = PixelFormat::RAW16;
        buffer.isCompressed = true;
        buffer.compressionType = CompressionType::MOTIONCAM;
        buffer.rowStride = 2 * buffer.width;
        
        // Update valid range
        buffer.data->setValidRange(0, end);
    }

    void RawBufferStreamer::crop(RawImageBuffer& buffer) const {
        //Measure m("crop");

        const int horizontalCrop = static_cast<const int>(4 * (lround(0.5 * (mCropWidth/100.0 * buffer.width)) / 4));

        // Even vertical crop to match bayer pattern
        const int verticalCrop   = static_cast<const int>(2 * (lround(0.5 * (mCropHeight/100.0 * buffer.height)) / 2));

        const int croppedWidth  = static_cast<const int>(buffer.width - horizontalCrop*2);
        const int croppedHeight = static_cast<const int>(buffer.height - verticalCrop*2);
        
        auto data = buffer.data->lock(true);

        const int xstart = horizontalCrop;
        const int xend = buffer.width - xstart;

        const int ystart = verticalCrop;
        const int yend = buffer.height - ystart;
        
        size_t end = 0;
        
        if(buffer.pixelFormat == PixelFormat::RAW10) {
            end = encoder::encode(data, encoder::ANDROID_RAW10, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else if(buffer.pixelFormat == PixelFormat::RAW12) {
            end = encoder::encode(data, encoder::ANDROID_RAW12, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else if(buffer.pixelFormat == PixelFormat::RAW16) {
            end = encoder::encode(data, encoder::ANDROID_RAW16, xstart, xend, ystart, yend, buffer.rowStride);
        }
        else {
            // Not supported
            buffer.data->unlock();
            return;
        }

        buffer.data->unlock();

        // Update buffer
        buffer.pixelFormat = PixelFormat::RAW16;
        buffer.rowStride = croppedWidth*2;
        buffer.width = croppedWidth;
        buffer.height = croppedHeight;
        buffer.isCompressed = true;
        buffer.isBinned = false;
        buffer.compressionType = CompressionType::MOTIONCAM;

        buffer.data->setValidRange(0, end);
    }

    void RawBufferStreamer::processBuffer(const std::shared_ptr<RawImageBuffer>& buffer) const {
        if(mBin)
            cropAndBin(*buffer);
        else {
            crop(*buffer);
        }
    }

    void RawBufferStreamer::doProcess() {
        std::shared_ptr<RawImageBuffer> buffer;
        
        while(mRunning) {
            if(!mUnprocessedBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }
            
            processBuffer(buffer);
            
            // Add to the ready list
            mReadyBuffers.enqueue(buffer);
        }

    }

    void RawBufferStreamer::doStream(const int fd, const RawCameraMetadata& cameraMetadata, const int numContainers) {
        std::shared_ptr<RawImageBuffer> buffer;
        size_t start, end;

        auto container = RawContainer::Create(fd, cameraMetadata, numContainers);

        while(mRunning) {
            if(!mReadyBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                continue;
            }

            container->add(*buffer, true);

            start = end = 0;
            buffer->data->getValidRange(start, end);

            // Return the buffer after it has been written
            RawBufferManager::get().discardBuffer(buffer);

            mWrittenBytes += (end - start);
            mWrittenFrames++;
        }

        //
        // Flush buffers
        //

        // Ready buffers
        while(mReadyBuffers.try_dequeue(buffer)) {
            container->add(*buffer, true);
            
            buffer->data->getValidRange(start, end);
            mWrittenBytes += (end - start);
            mWrittenFrames++;

            RawBufferManager::get().discardBuffer(buffer);
        }

        // Unprocessed buffers
        while(mUnprocessedBuffers.try_dequeue(buffer)) {
            processBuffer(buffer);
            
            container->add(*buffer, true);

            buffer->data->getValidRange(start, end);
            mWrittenBytes += (end - start);
            mWrittenFrames++;

            RawBufferManager::get().discardBuffer(buffer);
        }

        container->commit();
    }

    bool RawBufferStreamer::isRunning() const {
        return mRunning;
    }

    float RawBufferStreamer::estimateFps() const {
        auto now = std::chrono::steady_clock::now();
        float durationSecs = std::chrono::duration <float>(now - mStartTime).count();
        
        return mAcceptedFrames / (1e-5f + durationSecs);
    }

    size_t RawBufferStreamer::writenOutputBytes() const {
        return mWrittenBytes;
    }

    int RawBufferStreamer::droppedFrames() const {
        return mDroppedFrames;
    }
}
