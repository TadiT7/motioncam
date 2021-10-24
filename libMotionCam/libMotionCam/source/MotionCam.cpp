#include "motioncam/MotionCam.h"
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/ImageProcessor.h"
#include "build_bayer.h"

#include <HalideBuffer.h>
#include <queue/blockingconcurrentqueue.h>

#include <chrono>
#include <thread>

namespace motioncam {
    struct Job {
        Job(cv::Mat bayerImage,
            const RawCameraMetadata& cameraMetadata,
            const RawImageMetadata& frameMetadata,
            const std::string& outputPath) :
        bayerImage(bayerImage.clone()),
        cameraMetadata(cameraMetadata),
        frameMetadata(frameMetadata),
        outputPath(outputPath)
        {
        }
                
        cv::Mat bayerImage;
        const RawCameraMetadata& cameraMetadata;
        const RawImageMetadata& frameMetadata;
        std::string outputPath;
    };

    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Job>> QUEUE;
    std::atomic<bool> RUNNING;

    static void WriteDNG() {
        while(RUNNING) {
            std::shared_ptr<Job> job;
            
            if(QUEUE.wait_dequeue_timed(job, std::chrono::milliseconds(100))) {
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->outputPath);
            }
        }
    }

    void ConvertVideoToDNG(const std::string& containerPath, const std::string& outputPath, const int numThreads) {
        if(RUNNING)
            throw std::runtime_error("Already running");
        
        std::cout << "Opening " << containerPath << std::endl;
                
        RawContainer container(containerPath);
        
        auto frames = container.getFrames();
        
        // Sort frames by timestamp
        std::sort(frames.begin(), frames.end(), [&](std::string& a, std::string& b) {
            return container.getFrame(a)->metadata.timestampNs < container.getFrame(b)->metadata.timestampNs;
        });
        
        std::cout << "Found " << frames.size() << " frames" << std::endl;
        
        int64_t timestampOffset = 0;
        float timestamp = 0;

        // Create processing threads
        RUNNING = true;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        
        for(int i = 0; i < numThreads; i++) {
            auto t = std::unique_ptr<std::thread>(new std::thread(&WriteDNG));
            
            threads.push_back(std::move(t));
        }

        for(int i = 0; i < frames.size(); i++) {
            auto frame = container.loadFrame(frames[i]);
            
            if(frame->width <= 0 || frame->height <= 0) {
                std::cout << "Skipping " << frames[i] << std::endl;
                continue;
            }
            
            // Convert from RAW10/16 -> bayer image
            auto* data = frame->data->lock(false);

            auto inputBuffer = Halide::Runtime::Buffer<uint8_t>(data, (int) frame->data->len());
            auto bayerBuffer = Halide::Runtime::Buffer<uint16_t>(frame->width, frame->height);
            
            build_bayer(inputBuffer, frame->rowStride, static_cast<int>(frame->pixelFormat), bayerBuffer);

            frame->data->unlock();
            frame->data->release();
            
            if(timestampOffset <= 0) {
                timestampOffset = frame->metadata.timestampNs;
            }
            
            timestamp = (frame->metadata.timestampNs - timestampOffset) / (1000.0f*1000.0f*1000.0f);
        
            std::ostringstream str;
            str << std::setw(4) << std::setfill('0') << i;
            
            cv::Mat bayerImage(bayerBuffer.height(), bayerBuffer.width(), CV_16U, bayerBuffer.data());
            std::string outputDngPath = outputPath + "/frame" + str.str() + ".dng";

            std::cout << "[" << i+1 << "/" << frames.size() << "] [" << std::fixed << std::setprecision(2) << timestamp << "] " << outputDngPath << std::endl;
            
            while(!QUEUE.try_enqueue(std::make_shared<Job>(bayerImage, container.getCameraMetadata(), frame->metadata, outputDngPath))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            while(QUEUE.size_approx() > numThreads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Stop threads
        RUNNING = false;
        for(int i = 0; i < threads.size(); i++)
            threads[i]->join();

        // Flush buffers
        std::shared_ptr<Job> job;
        
        while(QUEUE.try_dequeue(job)) {
            util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->outputPath);
        }

        float fps = frames.size() / (1e-5f + timestamp);
        
        std::cout << "Estimated FPS: " << std::setprecision(2) << fps << std::endl;
        std::cout << "Done" << std::endl;
    }

    void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(containerPath, outputFilePath, progressListener);    
    }
}
