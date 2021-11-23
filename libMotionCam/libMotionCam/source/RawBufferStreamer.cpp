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
        mCropAmount(0)
    {
    }

    RawBufferStreamer::~RawBufferStreamer() {
        stop();
    }

    void RawBufferStreamer::start(const std::string& outputPath, const int64_t maxMemoryUsageBytes, const RawCameraMetadata& cameraMetadata) {
        stop();
        
        mRunning = true;
        
        size_t p = outputPath.find_last_of('.');
        if(p == std::string::npos)
            p = outputPath.size() - 1;
        
        auto outputName = outputPath.substr(0, p);
        mMaxMemoryUsageBytes = maxMemoryUsageBytes;
        
        logger::log("Maximum memory usage is " + std::to_string(mMaxMemoryUsageBytes));
        
        // Create IO threads
        for(int i = 0; i < NumWriteThreads; i++) {
            auto t = std::make_unique<std::thread>(&RawBufferStreamer::doStream, this, outputName, cameraMetadata);
            
            // Set priority on IO thread
            sched_param priority{};
            priority.sched_priority = 99;

            pthread_setschedparam(t->native_handle(), SCHED_FIFO, &priority);
            
            mIoThreads.push_back(std::move(t));
        }
        
        // Create compression threads
        for(int i = 0; i < NumCompressThreads; i++) {
            auto t = std::make_unique<std::thread>(&RawBufferStreamer::doCompress, this);
            
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

    void RawBufferStreamer::setCropAmount(int percentage) {
        // Only allow cropping when not running
        if(!mRunning)
            mCropAmount = percentage;
    }

    std::shared_ptr<RawImageBuffer> RawBufferStreamer::compressBuffer(
            const std::shared_ptr<RawImageBuffer>& buffer, std::vector<uint8_t>& tmpBuffer) const
    {
        auto compressedBuffer = std::make_shared<RawImageBuffer>();
        auto data = buffer->data->lock(false);

        float p = 0.5f * (mCropAmount / 100.0f);
        int skipRows = (int) (std::lround(buffer->height * p));
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

        return compressedBuffer;
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

    void RawBufferStreamer::doStream(const std::string& containerName, const RawCameraMetadata& cameraMetadata) {
        int containerNum = 0;
        uint32_t writtenFrames = 0;

        std::unique_ptr<RawContainer> container;
        std::unique_ptr<util::ZipWriter> writer;

        std::shared_ptr<RawImageBuffer> buffer;
        
        while(mRunning) {
            if(writtenFrames % 900 == 0) {
                std::string containerOutputPath = containerName + "_" + std::to_string(containerNum) + ".zip";

                logger::log("Creating " + containerOutputPath);
                container = std::make_unique<RawContainer>(cameraMetadata);
                container->save(containerOutputPath);

                writer = std::make_unique<util::ZipWriter>(containerOutputPath, true);

                ++containerNum;
                writtenFrames = 1;
            }

            if(!mCompressedBufferQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                logger::log("Out of buffers to write");
                continue;
            }
            
            RawContainer::append(*writer, buffer);

            mMemoryUsage -= static_cast<int>(buffer->data->len());
            writtenFrames++;
        }
        
        //
        // Flush buffers
        //

        if(writer) {
            std::vector<uint8_t> tmpBuffer;

            while(mCompressedBufferQueue.try_dequeue(buffer)) {
                RawContainer::append(*writer, compressBuffer(buffer, tmpBuffer));
            }

            while(mRawBufferQueue.try_dequeue(buffer)) {
                RawContainer::append(*writer, compressBuffer(buffer, tmpBuffer));

                RawBufferManager::get().discardBuffer(buffer);
            }

            writer->commit();
        }
    }

    bool RawBufferStreamer::isRunning() const {
        return mRunning;
    }
}
