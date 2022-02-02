#include "motioncam/MotionCam.h"
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/ImageProcessor.h"
#include "motioncam/Logger.h"

#include "build_bayer.h"
#include "build_bayer2.h"

#include <HalideBuffer.h>

#include <queue/concurrentqueue.h>
#include <queue/blockingconcurrentqueue.h>

#include <chrono>
#include <thread>

namespace motioncam {
    struct Job {
        Job(const cv::Mat&& bayerImage,
            const RawCameraMetadata&& cameraMetadata,
            const RawImageMetadata&& frameMetadata,
            const bool enableCompression,
            const bool saveShadingMap,
            const int fd,
            const std::string& outputPath) :
        bayerImage(bayerImage),
        cameraMetadata(cameraMetadata),
        frameMetadata(frameMetadata),
        enableCompression(enableCompression),
        saveShadingMap(saveShadingMap),
        fd(fd),
        outputPath(outputPath)
        {
        }
        
        cv::Mat bayerImage;
        const RawCameraMetadata cameraMetadata;
        const RawImageMetadata frameMetadata;
        const bool enableCompression;
        const bool saveShadingMap;
        const int fd;
        const std::string outputPath;
        std::string error;
    };

    struct Impl {
        Impl() : running(false) {
        }

        moodycamel::BlockingConcurrentQueue<std::shared_ptr<Job>> jobQueue;
        std::atomic<bool> running;
    };

    MotionCam::MotionCam() : mImpl(new Impl()) {
    }

    MotionCam::~MotionCam() {
    }

    void MotionCam::writeDNG() {
        while(mImpl->running) {
            std::shared_ptr<Job> job;

            mImpl->jobQueue.wait_dequeue_timed(job, std::chrono::milliseconds(100));
            if(!job)
                continue;
            
            try {
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->saveShadingMap, job->enableCompression, job->fd);
#elif defined(_WIN32)
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->saveShadingMap, job->enableCompression, job->outputPath);
#endif
            }
            catch(std::runtime_error& e) {
                job->error = e.what();
                logger::log(std::string("WriteDNG error: ") + e.what());
            }
        }
    }

    void MotionCam::convertVideoToDNG(const std::vector<std::string>& inputPaths,
                                      DngProcessorProgress& progress,
                                      const std::vector<float>& denoiseWeights,
                                      const int numThreads,
                                      const int mergeFrames,
                                      const bool enableCompression,
                                      const bool applyShadingMap,
                                      const int fromFrameNumber,
                                      const int toFrameNumber)
    {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto& inputPath : inputPaths) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(inputPath) ) );
        }

        convertVideoToDNG(c, progress, denoiseWeights, numThreads, mergeFrames, enableCompression, applyShadingMap, fromFrameNumber, toFrameNumber);
    }

    void MotionCam::convertVideoToDNG(std::vector<int>& fds,
                                      DngProcessorProgress& progress,
                                      const std::vector<float>& denoiseWeights,
                                      const int numThreads,
                                      const int mergeFrames,
                                      const bool enableCompression,
                                      const bool applyShadingMap,
                                      const int fromFrameNumber,
                                      const int toFrameNumber)
    {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto fd : fds) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(fd) ) );
        }
        
        convertVideoToDNG(c, progress, denoiseWeights, numThreads, mergeFrames, enableCompression, applyShadingMap, fromFrameNumber, toFrameNumber);
    }

    Halide::Runtime::Buffer<uint16_t> getOutputBuffer(int width, int height, bool hasExtendedEdges) {
        return Halide::Runtime::Buffer<uint16_t>(width, height);
    }

    void MotionCam::convertVideoToDNG(std::vector<std::unique_ptr<RawContainer>>& containers,
                                      DngProcessorProgress& progress,
                                      const std::vector<float>& denoiseWeights,
                                      const int numThreads,
                                      const int mergeFrames,
                                      const bool enableCompression,
                                      const bool applyShadingMap,
                                      const int fromFrameNumber,
                                      const int toFrameNumber)
    {
        
        if(mImpl->running)
            throw std::runtime_error("Already running");
        
        if(numThreads <= 0)
            return;

        // Get a list of all frames, ordered by timestamp
        std::vector<util::ContainerFrame> orderedFrames;
        
        util::GetOrderedFrames(containers, orderedFrames);
                
        // Create processing threads
        mImpl->running = true;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        std::vector<int> fds;
        
        for(int i = 0; i < numThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&MotionCam::writeDNG, this));
            
            threads.push_back(std::move(t));
        }
        
        //RawCameraMetadata metadata = containers[0]->getCameraMetadata();
                
        int startIdx = fromFrameNumber;
        int endIdx = toFrameNumber;
        
        if(startIdx < 0)
            startIdx = 0;
        
        if(endIdx < 0)
            endIdx = (int) orderedFrames.size() - 1;
        
        if(startIdx > endIdx)
            startIdx = endIdx;

        startIdx = std::min((int)orderedFrames.size() - 1, std::max(0, startIdx));
        endIdx = std::min((int)orderedFrames.size() - 1, std::max(0, endIdx));
                
        for(int frameIdx = startIdx; frameIdx <= endIdx; frameIdx++) {
            auto& container = containers[orderedFrames[frameIdx].containerIndex];
            auto frame = container->loadFrame(orderedFrames[frameIdx].frameName);
            
            if(!frame) {
                continue;
            }
            
            if(frame->width <= 0 || frame->height <= 0) {
                continue;
            }

            auto cameraMetadata = containers[0]->getCameraMetadata();

            auto originalWhiteLevel = containers[0]->getCameraMetadata().getWhiteLevel(frame->metadata);
            auto originalBlackLevel = containers[0]->getCameraMetadata().getBlackLevel(frame->metadata);

            // Construct shading map
            std::vector<Halide::Runtime::Buffer<float>> shadingMapBuffer;
            std::vector<float> shadingMapScale;

            if(applyShadingMap) {
                float shadingMapMaxScale;

                ImageProcessor::getNormalisedShadingMap(frame->metadata, shadingMapBuffer, shadingMapScale, shadingMapMaxScale);
            }
            else {
                auto shadingMap = frame->metadata.shadingMap();
                
                for(int i = 0; i < 4; i++) {
                    auto buffer = Halide::Runtime::Buffer<float>(shadingMap[i].cols, shadingMap[i].rows);
                    buffer.fill(1.0f);
                    
                    shadingMapBuffer.push_back(buffer);
                    shadingMapScale.push_back(1.0f);
                }
            }

            std::vector<std::shared_ptr<RawImageBuffer>> nearestBuffers;
            Halide::Runtime::Buffer<uint16_t> bayerBuffer(frame->width, frame->height);

            if(mergeFrames == 0) {
                auto data = frame->data->lock(false);
                auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());
                
                frame->data->unlock();
               
                // If all denoising is disabled, just build the bayer buffer
                float weightSum = 0.0f;
                for(size_t i = 0; i < denoiseWeights.size(); i++) {
                    weightSum += denoiseWeights[i];
                }
                
                if(weightSum > 1e-5f) {
                    auto denoiseBuffers = ImageProcessor::denoise(frame, nearestBuffers, denoiseWeights, container->getCameraMetadata());
                    
                    build_bayer2(denoiseBuffers[0],
                                 denoiseBuffers[1],
                                 denoiseBuffers[2],
                                 denoiseBuffers[3],
                                 shadingMapBuffer[0],
                                 shadingMapBuffer[1],
                                 shadingMapBuffer[2],
                                 shadingMapBuffer[3],
                                 frame->width / 2,
                                 frame->height / 2,
                                 static_cast<int>(cameraMetadata.sensorArrangment),
                                 EXPANDED_RANGE,
                                 frame->metadata.asShot[0],
                                 frame->metadata.asShot[1],
                                 frame->metadata.asShot[2],
                                 shadingMapScale[0],
                                 shadingMapScale[1],
                                 shadingMapScale[2],
                                 bayerBuffer);
                }
                else {
                    build_bayer(inputBuffer,
                                shadingMapBuffer[0],
                                shadingMapBuffer[1],
                                shadingMapBuffer[2],
                                shadingMapBuffer[3],
                                frame->width / 2,
                                frame->height / 2,
                                frame->rowStride,
                                static_cast<int>(frame->pixelFormat),
                                static_cast<int>(cameraMetadata.sensorArrangment),
                                originalBlackLevel[0],
                                originalBlackLevel[1],
                                originalBlackLevel[2],
                                originalBlackLevel[3],
                                originalWhiteLevel,
                                frame->metadata.asShot[0],
                                frame->metadata.asShot[1],
                                frame->metadata.asShot[2],
                                shadingMapScale[0],
                                shadingMapScale[1],
                                shadingMapScale[2],
                                EXPANDED_RANGE,
                                bayerBuffer);
                }
            }
            else {
                // Get number of nearest buffers
                util::GetNearestBuffers(containers, orderedFrames, frameIdx, mergeFrames, nearestBuffers);
                
                auto denoiseBuffers = ImageProcessor::denoise(frame, nearestBuffers, denoiseWeights, container->getCameraMetadata());
                                
                build_bayer2(denoiseBuffers[0],
                             denoiseBuffers[1],
                             denoiseBuffers[2],
                             denoiseBuffers[3],
                             shadingMapBuffer[0],
                             shadingMapBuffer[1],
                             shadingMapBuffer[2],
                             shadingMapBuffer[3],
                             frame->width / 2,
                             frame->height / 2,
                             static_cast<int>(cameraMetadata.sensorArrangment),
                             EXPANDED_RANGE,
                             frame->metadata.asShot[0],
                             frame->metadata.asShot[1],
                             frame->metadata.asShot[2],
                             shadingMapScale[0],
                             shadingMapScale[1],
                             shadingMapScale[2],
                             bayerBuffer);
            }

            // Release previous frames
            int m = frameIdx - mergeFrames;
            
            while(m >= 0) {
                auto& container = containers[orderedFrames[m].containerIndex];
                auto prevFrame = container->getFrame(orderedFrames[m].frameName);
                
                prevFrame->data->release();
                
                --m;
            }

            // Override the black/white levels of the output to match the new bayer image
            auto frameMetadata = frame->metadata;
            
            frameMetadata.dynamicBlackLevel = { 0, 0, 0, 0 };
            frameMetadata.dynamicWhiteLevel = EXPANDED_RANGE;
            
            cameraMetadata.updateBayerOffsets(frameMetadata.dynamicBlackLevel, frameMetadata.dynamicWhiteLevel);
            
            cv::Mat bayerImage(bayerBuffer.height(), bayerBuffer.width(), CV_16U, bayerBuffer.data());

            // Clone the buffer because the halide buffer will go away
            bayerImage = bayerImage.clone();

            int fd = -1;
            std::string outputPath;

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
            fd = progress.onNeedFd(frameIdx);
            if(fd < 0) {
                progress.onError("Did not get valid fd");
                break;
            }
#elif defined(_WIN32)
            outputPath = progress.onNeedFd(i);
#endif

            auto newJob = std::make_shared<Job>(std::move(bayerImage),
                                                std::move(cameraMetadata),
                                                std::move(frameMetadata),
                                                !applyShadingMap,
                                                enableCompression,
                                                fd,
                                                outputPath);
            
            while(!mImpl->jobQueue.try_enqueue(newJob))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Finalise finished jobs
            std::shared_ptr<Job> job;
            
            // Wait until jobs are completed
            while(mImpl->jobQueue.size_approx() > numThreads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            int p =  (frameIdx*100) / orderedFrames.size();
            
            if(!progress.onProgressUpdate(p)) {
                // Cancel requested. Stop here.
                break;
            }
        }
        
        // Flush buffers
        int numTries = 10;
        
        while(mImpl->jobQueue.size_approx() > 0 && numTries > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            --numTries;
        }

        mImpl->running = false;

        // Stop the threads
        for(int i = 0; i < threads.size(); i++)
            threads[i]->join();
        
        // Clear the queue if there are items in there
        std::shared_ptr<Job> job;
        
        while(mImpl->jobQueue.try_dequeue(job)) {
            logger::log("Discarding video frame!");
        }

        progress.onCompleted();
    }

    void MotionCam::ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(containerPath, outputFilePath, progressListener);    
    }

    void MotionCam::ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(rawContainer, outputFilePath, progressListener);
    }

    void MotionCam::GetMetadata(const std::string& filename, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
        std::vector<std::unique_ptr<RawContainer>> containers;
        
        try {
            containers.push_back( std::unique_ptr<RawContainer>( new RawContainer(filename) ) );
        }
        catch(std::exception& e) {
            outFrameRate = - 1;
            outNumFrames = -1;
            outDurationMs = -1;
            outNumSegments = 0;

            return;
        }
        
        GetMetadata(containers, outDurationMs, outFrameRate, outNumFrames, outNumSegments);
    }

    void MotionCam::GetMetadata(const std::vector<std::string>& paths, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
        std::vector<std::unique_ptr<RawContainer>> containers;

        try {
            for(size_t i = 0; i < paths.size(); i++)
                containers.push_back( std::unique_ptr<RawContainer>( new RawContainer(paths[i]) ) );
        }
        catch(std::exception& e) {
            outFrameRate = - 1;
            outNumFrames = -1;
            outDurationMs = -1;
            outNumSegments = 0;

            return;
        }

        GetMetadata(containers, outDurationMs, outFrameRate, outNumFrames, outNumSegments);
    }

    void MotionCam::GetMetadata(const std::vector<int>& fds, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
        // Try to get metadata from all segments
        std::vector<std::unique_ptr<RawContainer>> containers;
        
        for(const int fd : fds) {
            try {
                containers.push_back( std::unique_ptr<RawContainer>( new RawContainer(fd) ) );
            }
            catch(std::exception& e) {
                outFrameRate = - 1;
                outNumFrames = -1;
                outDurationMs = -1;
                outNumSegments = 0;

                return;
            }
        }
        
        GetMetadata(containers, outDurationMs, outFrameRate, outNumFrames, outNumSegments);
    }

    void MotionCam::GetMetadata(
        const std::vector<std::unique_ptr<RawContainer>>& containers,
        float& outDurationMs,
        float& outFrameRate,
        int& outNumFrames,
        int& outNumSegments)
    {
        std::vector<util::ContainerFrame> orderedFrames;
        
        util::GetOrderedFrames(containers, orderedFrames);
                
        if(orderedFrames.empty()) {
            outNumFrames = 0;
            outFrameRate = 0;
            outDurationMs = -1;
            outNumSegments = 0;
            
            return;
        }
               
        double startTime = orderedFrames[0].timestamp / 1e9;
        double endTime = orderedFrames[orderedFrames.size() - 1].timestamp / 1e9;
        
        if(endTime - startTime <= 0)
            outFrameRate = 0;
        else
            outFrameRate = orderedFrames.size() / (endTime - startTime);

        outNumFrames = (int) orderedFrames.size();
        
        // Set number of segments
        outNumSegments = 0;
        for(auto& container : containers) {
            outNumSegments = std::max(outNumSegments, container->getNumSegments());
        }
        
        outDurationMs = static_cast<float>((endTime - startTime) * 1000.0f);
    }
}
