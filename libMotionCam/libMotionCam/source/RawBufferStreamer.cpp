#include "motioncam/RawBufferStreamer.h"
#include "motioncam/RawContainer.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/RawBufferManager.h"
#include "motioncam/Logger.h"
#include "motioncam/Util.h"
#include "motioncam/Measure.h"

#include <zstd.h>

#include <memory>

namespace motioncam {
    const int NumCompressThreads = 2;
    const int NumWriteThreads    = 1;

    RawBufferStreamer::RawBufferStreamer() :
        mRunning(false),
        mMemoryUsage(0),
        mMaxMemoryUsageBytes(0),
        mCropVertical(0),
        mCropHorizontal(0)
    {
    }

    RawBufferStreamer::~RawBufferStreamer() {
        stop();
    }

    void RawBufferStreamer::start(const int fd, const int64_t maxMemoryUsageBytes, const RawCameraMetadata& cameraMetadata) {
        stop();
        
        mRunning = true;
        mMaxMemoryUsageBytes = maxMemoryUsageBytes;
        
        logger::log("Maximum memory usage is " + std::to_string(mMaxMemoryUsageBytes));
        
        // Create IO threads
        for(int i = 0; i < NumWriteThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doStream, this, fd, cameraMetadata));
            
            // Set priority on IO thread
            sched_param priority{};
            priority.sched_priority = 99;

            pthread_setschedparam(t->native_handle(), SCHED_FIFO, &priority);
            
            mIoThreads.push_back(std::move(t));
        }
        
        // Create compression threads
        for(int i = 0; i < NumCompressThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doCompress, this));
            
            mCompressThreads.push_back(std::move(t));
        }
    }

    bool RawBufferStreamer::add(const std::shared_ptr<RawImageBuffer>& frame) {
        if(mMemoryUsage > mMaxMemoryUsageBytes) {
            return false;
        }
        else {
            mRawBufferQueue.enqueue(frame);
            mMemoryUsage += frame->data->len();
        }

        return true;
    }

    void RawBufferStreamer::stop() {
        mRunning = false;

        for(auto & mCompressThread : mCompressThreads) {
            mCompressThread->join();
        }
        
        mCompressThreads.clear();

        for(auto & mIoThread : mIoThreads) {
            mIoThread->join();
        }
        
        mIoThreads.clear();
        mMemoryUsage = 0;
    }

    void RawBufferStreamer::setCropAmount(int horizontal, int vertical) {
        // Only allow cropping when not running
        if(!mRunning) {
            mCropVertical = vertical;
            mCropHorizontal = horizontal;
        }
    }

    std::shared_ptr<RawImageBuffer> RawBufferStreamer::crop(const std::shared_ptr<RawImageBuffer>& buffer) const {
        auto data = buffer->data->lock(true);

        const int horizontalCrop = static_cast<const int>(4 * (lround(0.5f * (mCropHorizontal/100.0f * buffer->width)) / 4));
        // Even vertical crop to match bayer pattern
        const int verticalCrop   = static_cast<const int>(2 * (lround(0.5f * (mCropVertical/100.0f * buffer->height)) / 2));

        auto croppedBuffer = std::make_shared<RawImageBuffer>();

        croppedBuffer->width        = static_cast<const int>(buffer->width - horizontalCrop*2);
        croppedBuffer->height       = static_cast<const int>(buffer->height - verticalCrop*2);
        croppedBuffer->metadata     = buffer->metadata;
        croppedBuffer->pixelFormat  = buffer->pixelFormat;
        croppedBuffer->isCompressed = false;

        std::vector<uint8_t> dstBuffer;

        const int ystart = verticalCrop;
        const int yend = buffer->height - ystart;

        if(buffer->pixelFormat == PixelFormat::RAW10) {
            croppedBuffer->rowStride = 10*croppedBuffer->width/8;
            dstBuffer.resize(croppedBuffer->rowStride * croppedBuffer->height);

            for(int y = ystart; y < yend; y++) {
                const int srcOffset = buffer->rowStride * y;
                const int dstOffset = croppedBuffer->rowStride * (y - ystart);

                const int xstart = 10*horizontalCrop/8;

                std::memcpy(dstBuffer.data()+dstOffset, data+srcOffset+xstart, croppedBuffer->rowStride);
            }
        }
        else if(buffer->pixelFormat == PixelFormat::RAW16) {
            croppedBuffer->rowStride = 2*croppedBuffer->width;
            dstBuffer.resize(croppedBuffer->rowStride * croppedBuffer->height);

            for(int y = ystart; y < yend; y++) {
                const int srcOffset = buffer->rowStride * y;
                const int dstOffset = croppedBuffer->rowStride * (y - ystart);

                const int xstart = 2*horizontalCrop;

                std::memcpy(dstBuffer.data()+dstOffset, data+srcOffset+xstart, croppedBuffer->rowStride);
            }
        }

        buffer->data->unlock();

        croppedBuffer->data->copyHostData(dstBuffer);

        return croppedBuffer;
    }

    std::shared_ptr<RawImageBuffer> RawBufferStreamer::compressBuffer(
            const std::shared_ptr<RawImageBuffer>& buffer, std::vector<uint8_t>& tmpBuffer) const
    {
        // Crop buffer
        auto croppedBuffer = crop(buffer);

        auto dstBound = ZSTD_compressBound(croppedBuffer->data->len());
        tmpBuffer.resize(dstBound);

        auto croppedBufferData = croppedBuffer->data->lock(true);

        // Compress buffer
        size_t writtenBytes =
                ZSTD_compress(&tmpBuffer[0],
                              tmpBuffer.size(),
                              croppedBufferData,
                              croppedBuffer->data->len(),
                              1);

        tmpBuffer.resize(writtenBytes);

        // Copy the data back into the cropped buffer
        std::memcpy(croppedBufferData, tmpBuffer.data(), tmpBuffer.size());

        croppedBuffer->data->shrink(tmpBuffer.size());

        croppedBuffer->isCompressed = true;
        croppedBuffer->data->unlock();

        return croppedBuffer;
    }

    void RawBufferStreamer::doCompress() {
        std::shared_ptr<RawImageBuffer> buffer;
        std::vector<uint8_t> tmpBuffer;

        while(mRunning) {
            if(!mRawBufferQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }

            // Keep track of memory usage
            mMemoryUsage -= buffer->data->len();
            mMemoryUsage += tmpBuffer.size();

            mCompressedBufferQueue.enqueue(compressBuffer(buffer, tmpBuffer));
            
            // Return the buffer
            RawBufferManager::get().discardBuffer(buffer);
        }
    }

    void RawBufferStreamer::doStream(const int fd, const RawCameraMetadata& cameraMetadata) {
        uint32_t writtenFrames = 0;
        
        util::ZipWriter writer(fd);
        std::shared_ptr<RawImageBuffer> buffer;

        // Every 15 minutes
        std::unique_ptr<RawContainer> container = std::unique_ptr<RawContainer>(new RawContainer(cameraMetadata));
        container->save(writer);

        while(mRunning) {
            if(!mCompressedBufferQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                logger::log("Out of buffers to write");
                continue;
            }
            
            RawContainer::append(writer, buffer);

            mMemoryUsage -= static_cast<int>(buffer->data->len());
            writtenFrames++;
        }
        
        //
        // Flush buffers
        //

        std::vector<uint8_t> tmpBuffer;

        while(mCompressedBufferQueue.try_dequeue(buffer)) {
            RawContainer::append(writer, buffer);
        }

        while(mRawBufferQueue.try_dequeue(buffer)) {
            RawContainer::append(writer, compressBuffer(buffer, tmpBuffer));

            RawBufferManager::get().discardBuffer(buffer);
        }

        writer.commit();
    }

    bool RawBufferStreamer::isRunning() const {
        return mRunning;
    }
}
