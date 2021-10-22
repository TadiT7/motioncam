#include "motioncam/RawBufferStreamer.h"
#include "motioncam/RawContainer.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/RawBufferManager.h"
#include "motioncam/Logger.h"
#include "motioncam/Util.h"
#include "motioncam/Measure.h"

#include <zstd.h>

namespace motioncam {
    const int NumCompressThreads = 2;
    const int NumWriteThreads    = 1;
    const int MaximumMemoryUsage = 1 * 1024 * 1024 * 1024; // Set maximum memory usage to 1 GB

    RawBufferStreamer::RawBufferStreamer() :
        mRunning(false),
        mMemoryUsage(0),
        mCropAmount(0)
    {
    }

    RawBufferStreamer::~RawBufferStreamer() {
        stop();
    }

    void RawBufferStreamer::start(const std::string& outputPath, const RawCameraMetadata& cameraMetadata) {
        stop();
        
        mRunning = true;
        
        size_t p = outputPath.find_last_of(".");
        if(p == std::string::npos)
            p = outputPath.size() - 1;
        
        auto outputName = outputPath.substr(0, p);
        
        // Create IO threads
        for(int i = 0; i < NumWriteThreads; i++) {
            std::string container = outputName + "_" + std::to_string(i) + ".zip";
            
            logger::log("Creating " + container);
            
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doStream, this, container, cameraMetadata));
            mIoThreads.push_back(std::move(t));
        }
        
        // Create compression threads
        for(int i = 0; i < NumCompressThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&RawBufferStreamer::doCompress, this));
            
            mCompressThreads.push_back(std::move(t));
        }
    }

    bool RawBufferStreamer::add(std::shared_ptr<RawImageBuffer> frame) {
        if(mMemoryUsage > MaximumMemoryUsage) {
            return false;
        }
        else {
            mRawBufferQueue.enqueue(frame);
        }

        return true;
    }

    void RawBufferStreamer::stop() {
        mRunning = false;

        for(int i = 0; i < mCompressThreads.size(); i++) {
            mCompressThreads[i]->join();
        }
        
        mCompressThreads.clear();

        for(int i = 0; i < mIoThreads.size(); i++) {
            mIoThreads[i]->join();
        }
        
        mIoThreads.clear();
        mMemoryUsage = 0;
    }

    void RawBufferStreamer::setCropAmount(int percentage) {
        // Only allow cropping when not running
        if(!mRunning)
            mCropAmount = percentage;
    }

    void RawBufferStreamer::doCompress() {
        std::shared_ptr<RawImageBuffer> buffer;
        std::vector<uint8_t> tmpBuffer;

        while(mRunning) {
            if(!mRawBufferQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(33))) {
                continue;
            }

            auto compressedBuffer = std::make_shared<RawImageBuffer>(*buffer);
            auto data = buffer->data->lock(false);

            float p = 0.5f * (mCropAmount / 100.0f);
            int skipRows = (int) (buffer->height * p + 0.5f);
            int croppedHeight = buffer->height - (2*skipRows);
            
            size_t startOffset = skipRows * buffer->rowStride;
            size_t newSize = buffer->data->len() - (startOffset*2);

            auto dstBound = ZSTD_compressBound(newSize);
            tmpBuffer.resize(dstBound);

            size_t writtenBytes =
                ZSTD_compress(&tmpBuffer[0],
                              tmpBuffer.size(),
                              data + startOffset,
                              newSize,
                              1);

            tmpBuffer.resize(writtenBytes);

            buffer->data->unlock();

            compressedBuffer->data->copyHostData(tmpBuffer);

            // Queue the compressed buffer
            compressedBuffer->width = buffer->width;
            compressedBuffer->height = croppedHeight;
            compressedBuffer->rowStride = buffer->rowStride;
            compressedBuffer->isCompressed = true;
            compressedBuffer->pixelFormat = buffer->pixelFormat;
            compressedBuffer->metadata = buffer->metadata;

            // Keep track of memory usage
            mMemoryUsage += static_cast<int>(tmpBuffer.size());
            
            mCompressedBufferQueue.enqueue(compressedBuffer);
                        
            // Return the buffer
            RawBufferManager::get().discardBuffer(buffer);
        }
    }

    void RawBufferStreamer::doStream(std::string outputContainerPath, RawCameraMetadata cameraMetadata) {
        // Initialise empty container
        RawContainer container(cameraMetadata);
        
        container.save(outputContainerPath);

        auto writer = util::ZipWriter(outputContainerPath, true);
        std::shared_ptr<RawImageBuffer> buffer;
        
        while(mRunning) {
            if(!mCompressedBufferQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(33))) {
                continue;
            }
            
            RawContainer::append(writer, buffer);

            mMemoryUsage -= static_cast<int>(buffer->data->len());
        }
        
        //
        // Flush buffers
        //

        while(mCompressedBufferQueue.try_dequeue(buffer)) {
            RawContainer::append(writer, buffer);
        }

        while(mRawBufferQueue.try_dequeue(buffer)) {
            RawContainer::append(writer, buffer);

            RawBufferManager::get().discardBuffer(buffer);
        }

        writer.commit();
    }

    bool RawBufferStreamer::isRunning() const {
        return mRunning;
    }
}
