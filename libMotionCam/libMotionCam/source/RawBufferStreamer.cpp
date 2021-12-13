#include "motioncam/RawBufferStreamer.h"
#include "motioncam/RawContainer.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/RawBufferManager.h"
#include "motioncam/Logger.h"
#include "motioncam/Util.h"
#include "motioncam/Measure.h"

#include <zstd.h>
#include <zstd_errors.h>

#include <memory>

namespace motioncam {
    const int NumCompressThreads = 1;
    const int NumProcessThreads  = 2;

    RawBufferStreamer::RawBufferStreamer() :
        mRunning(false),
        mCropVertical(0),
        mCropHorizontal(0)
    {
    }

    RawBufferStreamer::~RawBufferStreamer() {
        stop();
    }

    void RawBufferStreamer::start(const int fd, const RawCameraMetadata& cameraMetadata) {
        stop();
        
        mRunning = true;
        
        // Create IO threads with maximum priority
        mIoThread = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doStream, this, fd, cameraMetadata));

        // Update priority
        sched_param priority{};
        priority.sched_priority = 99;

        pthread_setschedparam(mIoThread->native_handle(), SCHED_FIFO, &priority);
        
        // Create process threads
        for(int i = 0; i < NumProcessThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doProcess, this));
            
            mProcessThreads.push_back(std::move(t));
        }

        // Create compression threads
        for(int i = 0; i < NumCompressThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doCompress, this));
            
            mCompressThreads.push_back(std::move(t));
        }
    }

    void RawBufferStreamer::add(const std::shared_ptr<RawImageBuffer>& frame) {
        mUnprocessedBuffers.enqueue(frame);
    }

    void RawBufferStreamer::stop() {
        mRunning = false;

        for(auto& thread : mCompressThreads) {
            thread->join();
        }

        for(auto& thread : mProcessThreads) {
            thread->join();
        }

        mCompressThreads.clear();
        mProcessThreads.clear();

        if(mIoThread)
            mIoThread->join();

        mIoThread = nullptr;
    }

    void RawBufferStreamer::setCropAmount(int horizontal, int vertical) {
        // Only allow cropping when not running
        if(!mRunning) {
            mCropVertical = vertical;
            mCropHorizontal = horizontal;
        }
    }

    void RawBufferStreamer::cropAndBin(RawImageBuffer& buffer) const {
    
    }

    void RawBufferStreamer::crop(RawImageBuffer& buffer) const {
        // Nothing to do
        if(mCropHorizontal == 0 && mCropVertical == 0)
            return;

        const int horizontalCrop = static_cast<const int>(4 * (lround(0.5f * (mCropHorizontal/100.0f * buffer.width)) / 4));

        // Even vertical crop to match bayer pattern
        const int verticalCrop   = static_cast<const int>(2 * (lround(0.5f * (mCropVertical/100.0f * buffer.height)) / 2));

        uint32_t croppedWidth  = static_cast<const int>(buffer.width - horizontalCrop*2);
        uint32_t croppedHeight = static_cast<const int>(buffer.height - verticalCrop*2);
        
        auto data = buffer.data->lock(true);

        const int ystart = verticalCrop;
        const int yend = buffer.height - ystart;

        uint32_t croppedRowStride = buffer.rowStride;

        if(buffer.pixelFormat == PixelFormat::RAW10) {
            croppedRowStride = 10*croppedWidth/8;

            for(int y = ystart; y < yend; y++) {
                const int srcOffset = buffer.rowStride * y;
                const int dstOffset = croppedRowStride * (y - ystart);

                const int xstart = 10*horizontalCrop/8;

                std::memmove(data+dstOffset, data+srcOffset+xstart, croppedRowStride);
            }
        }
        else if(buffer.pixelFormat == PixelFormat::RAW16) {
            croppedRowStride = 2*croppedWidth;

            for(int y = ystart; y < yend; y++) {
                const int srcOffset = buffer.rowStride * y;
                const int dstOffset = croppedRowStride * (y - ystart);

                const int xstart = 2*horizontalCrop;

                std::memmove(data+dstOffset, data+srcOffset+xstart, croppedRowStride);
            }
        }
        else {
            // Unsupported pixel format
            croppedWidth = buffer.width;
            croppedHeight = buffer.height;
        }

        buffer.data->unlock();

        // Update buffer
        buffer.rowStride = croppedRowStride;
        buffer.width = croppedWidth;
        buffer.height = croppedHeight;

        buffer.data->setValidRange(0, buffer.rowStride * buffer.height);
    }

    uint32_t RawBufferStreamer::zcompress(RawImageBuffer& inputBuffer) const {
        ZSTD_CCtx* const context = ZSTD_createCCtx();

        ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, ZSTD_fast);
        ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1);
        ZSTD_CCtx_setParameter(context, ZSTD_c_nbWorkers, 2);

        const size_t inputChunkSize = ZSTD_CStreamInSize();
        const size_t outputChunkSize = ZSTD_CStreamOutSize();

        std::vector<uint8_t> outChunk(outputChunkSize);

        size_t start, end;
        
        inputBuffer.data->getValidRange(start, end);

        // Update source size
        size_t err = ZSTD_CCtx_setPledgedSrcSize(context, end - start);
        if(ZSTD_isError(err))
            return 0;

        size_t srcOffset = start;
        size_t dstOffset = 0;

        auto* data = inputBuffer.data->lock(true);

        while(true) {
            const bool lastChunk = srcOffset + inputChunkSize >= end;
            const size_t limit = lastChunk ? end - srcOffset : inputChunkSize;
            const ZSTD_EndDirective mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;

            ZSTD_inBuffer input = { static_cast<void*>(data + srcOffset), limit, 0 };
            int finished;

            do {
                ZSTD_outBuffer output = { outChunk.data(), outChunk.size(), 0 };
                size_t const remaining = ZSTD_compressStream2(context, &output , &input, mode);

                // Failed
                err = ZSTD_isError(remaining);
                if(err != ZSTD_error_no_error) {
                    inputBuffer.data->setValidRange(0, 0);
                    inputBuffer.data->unlock();
                    return 0;
                }
                
                std::memcpy(data + dstOffset, outChunk.data(), output.pos);

                srcOffset += input.pos;
                dstOffset += output.pos;
    
                finished = lastChunk ? (remaining == 0) : (input.pos == input.size);
            }
            while (!finished);
            
            if(lastChunk)
                break;
        }
        
        inputBuffer.data->unlock();
        
        // Update valid range for compressed data
        inputBuffer.data->setValidRange(0, dstOffset);
        inputBuffer.isCompressed = true;
  
        ZSTD_freeCCtx(context);

        return static_cast<uint32_t>(dstOffset);
    }

    void RawBufferStreamer::doProcess() {
        std::shared_ptr<RawImageBuffer> buffer;
        
        while(mRunning) {
            if(!mUnprocessedBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }
            
            // Crop the buffer
            crop(*buffer);
            
            // Add to the ready list
            mReadyBuffers.enqueue(buffer);
        }

    }

    void RawBufferStreamer::doCompress() {
        std::shared_ptr<RawImageBuffer> buffer;

        while(mRunning) {
            // Pull buffers out of the ready queue and compress them
            if(!mReadyBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }

            // Compress
            zcompress(*buffer);
            
            // Add to compressed queue so we don't compress again
            mCompressedBuffers.enqueue(buffer);
        }
    }

    void RawBufferStreamer::doStream(const int fd, const RawCameraMetadata& cameraMetadata) {
        uint32_t writtenFrames = 0;
        
        util::ZipWriter writer(fd);
        std::shared_ptr<RawImageBuffer> buffer;

        std::unique_ptr<RawContainer> container = std::unique_ptr<RawContainer>(new RawContainer(cameraMetadata));
        container->save(writer);
        
        while(mRunning) {
            if(!mCompressedBuffers.try_dequeue(buffer)) {
                if(!mReadyBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                    continue;
                }
            }

            RawContainer::append(writer, *buffer);

            // Return the buffer after it has been written
            RawBufferManager::get().discardBuffer(buffer);

            writtenFrames++;
        }
        
        //
        // Flush buffers
        //

        std::vector<uint8_t> tmpBuffer;

        // Compressed first
        while(mCompressedBuffers.try_dequeue(buffer)) {
            RawContainer::append(writer, *buffer);

            RawBufferManager::get().discardBuffer(buffer);
        }

        // Ready buffers
        while(mReadyBuffers.try_dequeue(buffer)) {
            RawContainer::append(writer, *buffer);
            
            RawBufferManager::get().discardBuffer(buffer);
        }

        // Unprocessed buffers
        while(mUnprocessedBuffers.try_dequeue(buffer)) {
            crop(*buffer);
            
            RawContainer::append(writer, *buffer);

            RawBufferManager::get().discardBuffer(buffer);
        }

        writer.commit();
    }

    bool RawBufferStreamer::isRunning() const {
        return mRunning;
    }
}
