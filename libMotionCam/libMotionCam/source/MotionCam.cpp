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
#include <unistd.h>

namespace motioncam {
    struct Job {
        Job(cv::Mat bayerImage,
            const RawCameraMetadata& cameraMetadata,
            const RawImageMetadata& frameMetadata,
            const int fd) :
        bayerImage(bayerImage.clone()),
        cameraMetadata(cameraMetadata),
        frameMetadata(frameMetadata),
        fd(fd)
        {
        }
        
        cv::Mat bayerImage;
        const RawCameraMetadata& cameraMetadata;
        const RawImageMetadata& frameMetadata;
        const int fd;
        std::string error;
    };

    struct ContainerFrame {
        std::string frameName;
        int64_t timestamp;
        size_t containerIndex;
    };

    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Job>> JOB_QUEUE;
    std::atomic<bool> RUNNING;

    static void WriteDNG() {
        while(RUNNING) {
            std::shared_ptr<Job> job;
            
            JOB_QUEUE.wait_dequeue_timed(job, std::chrono::milliseconds(100));
            if(!job)
                continue;
            
            try {
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->fd);
            }
            catch(std::runtime_error& e) {
                job->error = e.what();
                logger::log(std::string("WriteDNG error: ") + e.what());
            }
        }
    }

    void GetOrderedFrames(const std::vector<std::unique_ptr<RawContainer>>& containers, std::vector<ContainerFrame>& outOrderedFrames) {
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

    void ConvertVideoToDNG(const std::vector<std::string>& inputPaths, DngProcessorProgress& progress, const int numThreads, const int mergeFrames) {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto& inputPath : inputPaths) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(inputPath) ) );
        }

        ConvertVideoToDNG(c, progress, numThreads, mergeFrames);
    }

    void ConvertVideoToDNG(std::vector<int>& fds, DngProcessorProgress& progress, const int numThreads, const int mergeFrames) {
        std::vector<std::unique_ptr<RawContainer>> c;
        
        for(auto fd : fds) {
            c.push_back( std::unique_ptr<RawContainer>( new RawContainer(fd) ) );
        }
        
        ConvertVideoToDNG(c, progress, numThreads, mergeFrames);
    }

    void ConvertVideoToDNG(std::vector<std::unique_ptr<RawContainer>>& containers,
                           DngProcessorProgress& progress,
                           const int numThreads,
                           const int mergeFrames)
    {
        
        if(RUNNING)
            throw std::runtime_error("Already running");
        
        if(numThreads <= 0)
            return;

        // Get a list of all frames, ordered by timestamp
        std::vector<ContainerFrame> orderedFrames;
        
        GetOrderedFrames(containers, orderedFrames);
                
        // Create processing threads
        RUNNING = true;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        std::vector<int> fds;
        
        for(int i = 0; i < numThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&WriteDNG));
            
            threads.push_back(std::move(t));
        }
        
        RawCameraMetadata metadata = containers[0]->getCameraMetadata();

        if(mergeFrames > 0) {
            metadata.blackLevel[0] = 0;
            metadata.blackLevel[1] = 0;
            metadata.blackLevel[2] = 0;
            metadata.blackLevel[3] = 0;

            metadata.whiteLevel = EXPANDED_RANGE;
        }
        
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
            
            if(mergeFrames == 0) {
                auto data = frame->data->lock(false);
                auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());
                
                if(!createdBuffer) {
                    bayerBuffer = Halide::Runtime::Buffer<uint16_t>(frame->width , frame->height);
                    createdBuffer = true;
                }

                frame->data->unlock();
                
                build_bayer(inputBuffer, frame->rowStride, static_cast<int>(frame->pixelFormat), bayerBuffer);
            }
            else {
                std::vector<std::shared_ptr<RawImageBuffer>> nearestBuffers;
                
                int leftOffset = -1;
                int rightOffset = 1;
                
                // Get the nearest frames
                while(true) {
                    if(i + leftOffset >= 0) {
                        int leftIndex = i + leftOffset;
                        
                        auto& container = containers[orderedFrames[leftIndex].containerIndex];
                        auto left = container->loadFrame(orderedFrames[leftIndex].frameName);

                        if(!left) {
                            left = nullptr;
                        }

                        nearestBuffers.push_back(left);
                        
                        leftOffset--;

                        if(nearestBuffers.size() >= mergeFrames)
                            break;
                    }

                    if(i + rightOffset < orderedFrames.size()) {
                        int rightIndex = i + rightOffset;
                        
                        auto& container = containers[orderedFrames[rightIndex].containerIndex];
                        auto right = container->loadFrame(orderedFrames[rightIndex].frameName);

                        if(!right)
                            right = nullptr;
                        
                        nearestBuffers.push_back(right);

                        if(nearestBuffers.size() >= mergeFrames)
                            break;

                        rightOffset++;
                    }
                    
                    if(i + leftOffset < 0 && i + rightOffset >= orderedFrames.size())
                        break;
                }
                
                auto denoiseBuffer = ImageProcessor::denoise(frame, nearestBuffers, container->getCameraMetadata());
                
                auto blackLevel = container->getCameraMetadata().blackLevel;
                auto whiteLevel = container->getCameraMetadata().whiteLevel;
                
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
                             blackLevel[0],
                             blackLevel[1],
                             blackLevel[2],
                             blackLevel[3],
                             whiteLevel,
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
            
            int fd = progress.onNeedFd(i);
            if(fd < 0) {
                progress.onError("Did not get valid fd");
                break;
            }

            while(!JOB_QUEUE.try_enqueue(std::make_shared<Job>(bayerImage, metadata, frame->metadata, fd))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Finalise finished jobs
            std::shared_ptr<Job> job;
            
            // Wait until jobs are completed
            while(JOB_QUEUE.size_approx() > numThreads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            progress.onProgressUpdate( (i*100) /orderedFrames.size());
        }
        
        // Flush buffers
        int numTries = 10;
        
        while(JOB_QUEUE.size_approx() > 0 && numTries > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            --numTries;
        }

        RUNNING = false;

        // Stop the threads
        for(int i = 0; i < threads.size(); i++)
            threads[i]->join();
        
        // Clear the queue if there are items in there
        std::shared_ptr<Job> job;
        
        while(JOB_QUEUE.try_dequeue(job)) {
            logger::log("Discarding video frame!");
        }

        progress.onCompleted();
    }

    void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(containerPath, outputFilePath, progressListener);    
    }

    void ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(rawContainer, outputFilePath, progressListener);
    }

    void GetMetadata(const std::string& filename, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
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

    void GetMetadata(const std::vector<std::string>& paths, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
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

    void GetMetadata(const std::vector<int>& fds, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments) {
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

    void GetMetadata(
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
               
        double startTime = orderedFrames[0].timestamp / 1e9f;
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
        
        outDurationMs = (endTime - startTime) * 1000.0f;
    }
}
