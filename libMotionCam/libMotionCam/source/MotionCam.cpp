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

    float ConvertVideoToDNG(const std::string& containerPath, const DngProcessorProgress& progress, const int numThreads, const int mergeFrames) {
        if(RUNNING)
            throw std::runtime_error("Already running");
        
        RawContainer container(containerPath);
        
        auto frames = container.getFrames();
        
        // Sort frames by timestamp
        std::sort(frames.begin(), frames.end(), [&](std::string& a, std::string& b) {
            return container.getFrame(a)->metadata.timestampNs < container.getFrame(b)->metadata.timestampNs;
        });
                
        int64_t timestampOffset = 0;
        float timestamp = 0;

        // Create processing threads
        RUNNING = true;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        std::vector<int> fds;
        
        for(int i = 0; i < numThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&WriteDNG));
            
            threads.push_back(std::move(t));
        }
        
        if(threads.empty())
            return 0;
        
        RawCameraMetadata metadata = container.getCameraMetadata();

        if(mergeFrames > 0) {
            metadata.blackLevel[0] = 0;
            metadata.blackLevel[1] = 0;
            metadata.blackLevel[2] = 0;
            metadata.blackLevel[3] = 0;
            
            metadata.whiteLevel = EXPANDED_RANGE;
        }
        
        for(int i = 0; i < frames.size(); i++) {
            auto frame = container.loadFrame(frames[i]);
            
            if(frame->width <= 0 || frame->height <= 0) {
                continue;
            }
            
            // Convert from RAW10/16 -> bayer image
            auto bayerBuffer = Halide::Runtime::Buffer<uint16_t>(frame->width, frame->height);
                        
            if(mergeFrames == 0) {
                auto data = frame->data->lock(false);
                auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());

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
                        auto left = container.loadFrame(frames[i + leftOffset]);
                        nearestBuffers.push_back(left);

                        leftOffset--;

                        if(nearestBuffers.size() >= mergeFrames)
                            break;
                    }

                    if(i + rightOffset < frames.size()) {
                        auto right = container.loadFrame(frames[i + rightOffset]);
                        nearestBuffers.push_back(right);

                        if(nearestBuffers.size() >= mergeFrames)
                            break;

                        rightOffset++;
                    }
                    
                    if(i + leftOffset < 0 && i + rightOffset >= frames.size())
                        break;
                }
                
                auto denoiseBuffer = ImageProcessor::denoise(frame, nearestBuffers, container.getCameraMetadata());
                
                auto blackLevel = container.getCameraMetadata().blackLevel;
                auto whiteLevel = container.getCameraMetadata().whiteLevel;
                
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
                auto prevFrame = container.getFrame(frames[m]);
                prevFrame->data->release();
                
                --m;
            }
            
            if(timestampOffset <= 0) {
                timestampOffset = frame->metadata.timestampNs;
            }
            
            timestamp = (frame->metadata.timestampNs - timestampOffset) / (1000.0f*1000.0f*1000.0f);
                    
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
            
            progress.onProgressUpdate((i*100)/frames.size());
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
        
        for(int i = 0; i < fds.size(); i++)
            progress.onCompleted(i);

        // Clear the queue if there are items in there
        std::shared_ptr<Job> job;
        
        while(JOB_QUEUE.try_dequeue(job)) {
            logger::log("Discarding video frame!");
        }

        progress.onCompleted();

        return frames.size() / (1e-5f + timestamp);
    }

    void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(containerPath, outputFilePath, progressListener);    
    }

    void ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(rawContainer, outputFilePath, progressListener);
    }

    void GetMetadata(const std::string& containerPath, float& outFrameRate, int& outNumFrames) {
        RawContainer container(containerPath);
        
        auto frames = container.getFrames();
        
        // Sort frames by timestamp
        std::sort(frames.begin(), frames.end(), [&](std::string& a, std::string& b) {
            return container.getFrame(a)->metadata.timestampNs < container.getFrame(b)->metadata.timestampNs;
        });
               
        float timestampOffset = 0;
        float time = 0;

        for(int i = 0; i < frames.size(); i++) {
            auto frame = container.getFrame(frames[i]);
            
            if(timestampOffset <= 0) {
                timestampOffset = frame->metadata.timestampNs;
            }
            
            time = (frame->metadata.timestampNs - timestampOffset) / (1000.0f*1000.0f*1000.0f);
        }
        
        outNumFrames = (int) frames.size();
                
        if(time == 0)
            outFrameRate = 0;
        else
            outFrameRate  = frames.size() / time;
    }
}
