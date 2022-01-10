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
        Job(const cv::Mat& bayerImage,
            const RawCameraMetadata& cameraMetadata,
            const RawImageMetadata& frameMetadata,
            const bool enableCompression,
            const int fd,
            const std::string& outputPath) :
        bayerImage(bayerImage.clone()),
        cameraMetadata(cameraMetadata),
        frameMetadata(frameMetadata),
        enableCompression(enableCompression),
        fd(fd),
        outputPath(outputPath)
        {
        }
        
        cv::Mat bayerImage;
        const RawCameraMetadata& cameraMetadata;
        const RawImageMetadata& frameMetadata;
        const bool enableCompression;
        const int fd;
        const std::string outputPath;
        std::string error;
    };

    struct ContainerFrame {
        std::string frameName;
        int64_t timestamp;
        size_t containerIndex;
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
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, false, job->enableCompression, job->fd);
#elif defined(_WIN32)
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, false, job->enableCompression, job->outputPath);
#endif
            }
            catch(std::runtime_error& e) {
                job->error = e.what();
                logger::log(std::string("WriteDNG error: ") + e.what());
            }
        }
    }

    void GetNearestBuffers(
            const std::vector<std::unique_ptr<RawContainer>>& containers,
            const std::vector<ContainerFrame>& orderedFrames,
            const int startIdx,
            const int numBuffers,
            std::vector<std::shared_ptr<RawImageBuffer>>& outNearestBuffers)
    {
        int leftOffset = -1;
        int rightOffset = 1;

        // Get the nearest frames
        outNearestBuffers.clear();

        while(true) {
            if(startIdx + leftOffset >= 0) {
                int leftIndex = startIdx + leftOffset;

                auto& container = containers[orderedFrames[leftIndex].containerIndex];
                auto left = container->loadFrame(orderedFrames[leftIndex].frameName);

                if(!left) {
                    left = nullptr;
                }

                outNearestBuffers.push_back(left);

                leftOffset--;

                if(outNearestBuffers.size() >= numBuffers)
                    break;
            }

            if(startIdx + rightOffset < orderedFrames.size()) {
                int rightIndex = startIdx + rightOffset;

                auto& container = containers[orderedFrames[rightIndex].containerIndex];
                auto right = container->loadFrame(orderedFrames[rightIndex].frameName);

                if(!right)
                    right = nullptr;

                outNearestBuffers.push_back(right);

                if(outNearestBuffers.size() >= numBuffers)
                    break;

                rightOffset++;
            }

            if(startIdx + leftOffset < 0 && startIdx + rightOffset >= orderedFrames.size())
                break;
        }
    }

    void MotionCam::GetOrderedFrames(
            const std::vector<std::unique_ptr<RawContainer>>& containers, std::vector<ContainerFrame>& outOrderedFrames)
    {
        // Get a list of all frames, ordered by timestamp
        for(size_t i = 0; i < containers.size(); i++) {
            auto& container = containers[i];
            
            for(auto& frameName : container->getFrames()) {
                auto frame = container->getFrame(frameName);
                outOrderedFrames.push_back({ frameName, frame->metadata.timestampNs, i} );
            }
        }
        
        std::sort(outOrderedFrames.begin(), outOrderedFrames.end(), [](ContainerFrame& a, ContainerFrame& b) {
            return a.timestamp < b.timestamp;
        });
    }

    void MotionCam::convertVideoToDNG(const std::vector<std::string>& inputPaths,
                                      DngProcessorProgress& progress,
                                      const int numThreads,
                                      const int mergeFrames,
                                      const bool enableCompression)
    {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto& inputPath : inputPaths) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(inputPath) ) );
        }

        convertVideoToDNG(c, progress, numThreads, mergeFrames, enableCompression);
    }

    void MotionCam::convertVideoToDNG(std::vector<int>& fds,
                                      DngProcessorProgress& progress,
                                      const int numThreads,
                                      const int mergeFrames,
                                      const bool enableCompression)
    {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto fd : fds) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(fd) ) );
        }
        
        convertVideoToDNG(c, progress, numThreads, mergeFrames, enableCompression);
    }

    void MotionCam::convertVideoToDNG(
        std::vector<std::unique_ptr<RawContainer>>& containers,
        DngProcessorProgress& progress,
        const int numThreads,
        const int mergeFrames,
        const bool enableCompression)
    {
        
        if(mImpl->running)
            throw std::runtime_error("Already running");
        
        if(numThreads <= 0)
            return;

        // Get a list of all frames, ordered by timestamp
        std::vector<ContainerFrame> orderedFrames;
        
        GetOrderedFrames(containers, orderedFrames);
                
        // Create processing threads
        mImpl->running = true;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        std::vector<int> fds;
        
        for(int i = 0; i < numThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&MotionCam::writeDNG, this));
            
            threads.push_back(std::move(t));
        }
        
        RawCameraMetadata metadata = containers[0]->getCameraMetadata();

        metadata.blackLevel[0] = 0;
        metadata.blackLevel[1] = 0;
        metadata.blackLevel[2] = 0;
        metadata.blackLevel[3] = 0;
        metadata.whiteLevel = EXPANDED_RANGE;
        
        Halide::Runtime::Buffer<uint16_t> bayerBuffer;
        bool createdBuffer = false;
        
        for(int i = 0; i < orderedFrames.size(); i++) {
            auto& container = containers[orderedFrames[i].containerIndex];
            auto frame = container->loadFrame(orderedFrames[i].frameName);
            
            if(!frame) {
                continue;
            }
            
            if(frame->width <= 0 || frame->height <= 0) {
                continue;
            }
            
            // Construct shading map
            Halide::Runtime::Buffer<float> shadingMapBuffer[4];
            
            for(int i = 0; i < 4; i++) {
                shadingMapBuffer[i] = Halide::Runtime::Buffer<float>(
                    (float*) frame->metadata.lensShadingMap[i].data,
                    frame->metadata.lensShadingMap[i].cols,
                    frame->metadata.lensShadingMap[i].rows);
            }

            auto blackLevel = container->getCameraMetadata().blackLevel;
            auto whiteLevel = container->getCameraMetadata().whiteLevel;

            if(mergeFrames == 0) {
                auto data = frame->data->lock(false);
                auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());
                
                if(!createdBuffer) {
                    bayerBuffer = Halide::Runtime::Buffer<uint16_t>(frame->width , frame->height);
                    createdBuffer = true;
                }

                frame->data->unlock();
               
                build_bayer(inputBuffer,
                            shadingMapBuffer[0],
                            shadingMapBuffer[1],
                            shadingMapBuffer[2],
                            shadingMapBuffer[3],
                            frame->width / 2,
                            frame->height / 2,
                            frame->rowStride,
                            static_cast<int>(frame->pixelFormat),
                            static_cast<int>(metadata.sensorArrangment),
                            blackLevel[0],
                            blackLevel[1],
                            blackLevel[2],
                            blackLevel[3],
                            whiteLevel,
                            EXPANDED_RANGE,
                            bayerBuffer);
            }
            else {
                std::vector<std::shared_ptr<RawImageBuffer>> nearestBuffers;

                // Get number of nearest buffers
                GetNearestBuffers(containers, orderedFrames, i, mergeFrames, nearestBuffers);
                
                auto denoiseBuffer = ImageProcessor::denoise(frame, nearestBuffers, container->getCameraMetadata());
                                
                // Convert from RAW10/16 -> bayer image
                if(!createdBuffer) {
                    const int rawWidth  = frame->width / 2;
                    const int rawHeight = frame->height / 2;

                    const int T = pow(2, EXTEND_EDGE_AMOUNT);

                    const int offsetX = static_cast<int>(T * ceil(rawWidth / (double) T) - rawWidth);
                    const int offsetY = static_cast<int>(T * ceil(rawHeight / (double) T) - rawHeight);

                    bayerBuffer = Halide::Runtime::Buffer<uint16_t>((denoiseBuffer.width()-offsetX)*2 , (denoiseBuffer.height()-offsetY)*2);
                    
                    bayerBuffer.translate(0, offsetX);
                    bayerBuffer.translate(1, offsetY);
                    
                    createdBuffer = true;
                }
                
                build_bayer2(denoiseBuffer,
                             shadingMapBuffer[0],
                             shadingMapBuffer[1],
                             shadingMapBuffer[2],
                             shadingMapBuffer[3],
                             frame->width / 2,
                             frame->height / 2,
                             blackLevel[0],
                             blackLevel[1],
                             blackLevel[2],
                             blackLevel[3],
                             whiteLevel,
                             static_cast<int>(metadata.sensorArrangment),
                             1.0f / nearestBuffers.size(),
                             EXPANDED_RANGE,
                             bayerBuffer);
            }

            // Release previous frames
            int m = i - mergeFrames;
            
            while(m >= 0) {
                auto& container = containers[orderedFrames[m].containerIndex];
                auto prevFrame = container->getFrame(orderedFrames[m].frameName);
                
                prevFrame->data->release();
                
                --m;
            }
                                
            cv::Mat bayerImage(bayerBuffer.height(), bayerBuffer.width(), CV_16U, bayerBuffer.data());
            
            int fd = -1;
            std::string outputPath;

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
            fd = progress.onNeedFd(i);
            if(fd < 0) {
                progress.onError("Did not get valid fd");
                break;
            }
#elif defined(_WIN32)
            outputPath = progress.onNeedFd(i);
#endif
            while(!mImpl->jobQueue.try_enqueue(std::make_shared<Job>(bayerImage, metadata, frame->metadata, enableCompression, fd, outputPath))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Finalise finished jobs
            std::shared_ptr<Job> job;
            
            // Wait until jobs are completed
            while(mImpl->jobQueue.size_approx() > numThreads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            int p =  (i*100) / orderedFrames.size();
            
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
        std::vector<ContainerFrame> orderedFrames;
        
        GetOrderedFrames(containers, orderedFrames);
                
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
