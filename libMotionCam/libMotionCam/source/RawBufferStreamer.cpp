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

    inline uint16_t RAW10(uint8_t* data, int16_t x, int16_t y, const uint16_t stride, const int16_t width, const int16_t height) {
        x = std::min((int16_t)(width - 1), std::max((int16_t)0, x));
        y = std::min((int16_t)(height - 1), std::max((int16_t)0, y));
        
        const uint16_t X = (x / 4) * 4;

        const uint32_t yoffset = y * stride;
        const uint32_t xoffset = yoffset + (10*X/8);
        
        uint16_t p = x - X;
        uint8_t shift = 2*p;
        
        return (((uint16_t) data[xoffset + p]) << 2) | ((((uint16_t) data[xoffset + 4]) >> shift) & 0x03);
    }

    inline uint16_t RAW16(uint8_t* data, int16_t x, int16_t y, const uint16_t stride, const int16_t width, const int16_t height) {
        x = std::min((int16_t)(width - 1), std::max((int16_t)0, x));
        y = std::min((int16_t)(height - 1), std::max((int16_t)0, y));
        
        const uint32_t offset = (y*stride) + (x*2);
        
        return ((uint16_t) data[offset]) | (((uint16_t)data[offset+1]) << 8);
    }

    RawBufferStreamer::RawBufferStreamer() :
        mRunning(false),
        mCropVertical(0),
        mCropHorizontal(0),
        mBin(false)
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

    void RawBufferStreamer::setBin(bool bin) {
        mBin = bin;
    }

    void RawBufferStreamer::cropAndBin(RawImageBuffer& buffer) const {
        Measure m("cropAndBin");
        
        const int horizontalCrop = static_cast<const int>(4 * (lround(0.5f * (mCropHorizontal/100.0f * buffer.width)) / 4));

        // Even vertical crop to match bayer pattern
        const int verticalCrop   = static_cast<const int>(2 * (lround(0.5f * (mCropVertical/100.0f * buffer.height)) / 2));

        uint32_t croppedWidth  = static_cast<const int>(buffer.width - horizontalCrop*2);
        uint32_t croppedHeight = static_cast<const int>(buffer.height - verticalCrop*2);
        
        const int ystart = verticalCrop;
        const int yend = buffer.height - ystart;

        const uint32_t xstart = horizontalCrop;
        const uint32_t xend = buffer.width - horizontalCrop;

        const uint32_t dstStride = 2 * (croppedWidth / 2);

        std::vector<uint16_t> row0(croppedWidth / 2);
        std::vector<uint16_t> row1(croppedWidth / 2);

        auto data = buffer.data->lock(true);
        uint32_t dstOffset = 0;

        if(buffer.pixelFormat == PixelFormat::RAW10) {
            for(int16_t y = ystart; y < yend; y+=4) {
                for(int16_t x = xstart; x < xend; x+=4) {
                    
                    for(int16_t iy = y; iy < y + 2; iy++) {
                        for(int16_t ix = x; ix < x + 2; ix++) {
                            const uint16_t p0 = 1*RAW10(data, ix - 2,  iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p1 = 2*RAW10(data, ix,      iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p2 = 1*RAW10(data, ix + 2,  iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            
                            const uint16_t p3 = 2*RAW10(data, ix - 2,  iy,      buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p4 = 4*RAW10(data, ix,      iy,      buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p5 = 2*RAW10(data, ix + 2,  iy,      buffer.rowStride, buffer.width, buffer.height);
                            
                            const uint16_t p6 = 1*RAW10(data, ix - 2,  iy + 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p7 = 2*RAW10(data, ix,      iy + 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p8 = 1*RAW10(data, ix + 2,  iy + 2,  buffer.rowStride, buffer.width, buffer.height);

                            const uint16_t out = (p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8) / 16;
                            
                            uint16_t X = (x / 2) + (ix - x);
                            uint16_t Y = (y / 2) + (iy - y);
                                                        
                            if(Y % 2 == 0)
                                row0[X] = out;
                            else
                                row1[X] = out;
                        }
                    }
                }
                
                // Copy rows
                std::memcpy(data+dstOffset, row0.data(), dstStride);
                dstOffset += dstStride;

                std::memcpy(data+dstOffset, row1.data(), dstStride);
                dstOffset += dstStride;
            }
        }
        else if(buffer.pixelFormat == PixelFormat::RAW16) {
            for(int16_t y = ystart; y < yend; y+=4) {
                for(int16_t x = xstart; x < xend; x+=4) {
                    
                    for(int16_t iy = y; iy < y + 2; iy++) {
                        for(int16_t ix = x; ix < x + 2; ix++) {
                            const uint16_t p0 = 1*RAW16(data, ix - 2,  iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p1 = 2*RAW16(data, ix,      iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p2 = 1*RAW16(data, ix + 2,  iy - 2,  buffer.rowStride, buffer.width, buffer.height);
                            
                            const uint16_t p3 = 2*RAW16(data, ix - 2,  iy,      buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p4 = 4*RAW16(data, ix,      iy,      buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p5 = 2*RAW16(data, ix + 2,  iy,      buffer.rowStride, buffer.width, buffer.height);
                            
                            const uint16_t p6 = 1*RAW16(data, ix - 2,  iy + 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p7 = 2*RAW16(data, ix,      iy + 2,  buffer.rowStride, buffer.width, buffer.height);
                            const uint16_t p8 = 1*RAW16(data, ix + 2,  iy + 2,  buffer.rowStride, buffer.width, buffer.height);

                            const uint16_t out = (p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8) / 16;
                            
                            uint16_t X = (x / 2) + (ix - x);
                            uint16_t Y = (y / 2) + (iy - y);
                                                        
                            if(Y % 2 == 0)
                                row0[X] = out;
                            else
                                row1[X] = out;
                        }
                    }
                }
                
                // Copy rows
                std::memcpy(data+dstOffset, row0.data(), dstStride);
                dstOffset += dstStride;

                std::memcpy(data+dstOffset, row1.data(), dstStride);
                dstOffset += dstStride;
            }
        }
        else {
            // Not supported
            buffer.data->unlock();
            return;
        }

        buffer.width = croppedWidth / 2;
        buffer.height = croppedHeight / 2;
        buffer.pixelFormat = PixelFormat::RAW16;
        buffer.rowStride = buffer.width * 2;
        
        buffer.data->unlock();
                
        // Update valid range
        size_t end = buffer.height*buffer.rowStride;        
        buffer.data->setValidRange(0, end);
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

        uint32_t croppedRowStride;

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
            // Not supported
            buffer.data->unlock();
            return;
        }

        buffer.data->unlock();

        // Update buffer
        buffer.rowStride = croppedRowStride;
        buffer.width = croppedWidth;
        buffer.height = croppedHeight;

        buffer.data->setValidRange(0, buffer.rowStride * buffer.height);
    }

    size_t RawBufferStreamer::zcompress(RawImageBuffer& inputBuffer, std::vector<uint8_t>& tmpBuffer) const {
        size_t start, end, size;

        inputBuffer.data->getValidRange(start, end);

        size = end - start;

        size_t outputBounds = ZSTD_compressBound(size);
        tmpBuffer.resize(outputBounds);

        auto data = inputBuffer.data->lock(true);
        
        size_t outputSize = ZSTD_compress(
            tmpBuffer.data(), tmpBuffer.size(), data + start, size, ZSTD_fast);
        
        if(ZSTD_isError(outputSize)) {
            inputBuffer.data->unlock();
            return inputBuffer.data->len();
        }

        // This should hopefully always be true
        if(outputSize < inputBuffer.data->len()) {
            std::memcpy(data, tmpBuffer.data(), outputSize);
            
            inputBuffer.data->setValidRange(0, outputSize);
            inputBuffer.isCompressed = true;
            
            inputBuffer.data->unlock();
            
            return outputSize;
        }
        
        inputBuffer.data->unlock();
        return inputBuffer.data->len();
    }

    void RawBufferStreamer::doProcess() {
        std::shared_ptr<RawImageBuffer> buffer;
        
        while(mRunning) {
            if(!mUnprocessedBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }
            
            // Crop the buffer
            if(mBin)
                cropAndBin(*buffer);
            else
                crop(*buffer);
            
            // Add to the ready list
            mReadyBuffers.enqueue(buffer);
        }

    }

    void RawBufferStreamer::doCompress() {
        std::shared_ptr<RawImageBuffer> buffer;
        std::vector<uint8_t> tmpBuffer;

        std::vector<std::shared_ptr<RawImageBuffer>> buffers;
        
        while(mRunning) {
            // Pull buffers out of the ready queue and compress them
            if(!mReadyBuffers.wait_dequeue_timed(buffer, std::chrono::milliseconds(67))) {
                continue;
            }

            zcompress(*buffer, tmpBuffer);
            
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
            if(mBin)
                cropAndBin(*buffer);
            else
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
