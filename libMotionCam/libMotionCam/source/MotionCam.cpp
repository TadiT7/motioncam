#include "motioncam/MotionCam.h"
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/ImageProcessor.h"

#include "build_bayer.h"

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
            const std::string& filename) :
        bayerImage(bayerImage.clone()),
        cameraMetadata(cameraMetadata),
        frameMetadata(frameMetadata),
        filename(filename)
        {
        }
        
        cv::Mat bayerImage;
        const RawCameraMetadata& cameraMetadata;
        const RawImageMetadata& frameMetadata;
        const std::string filename;
        std::string error;
    };

    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Job>> JOB_QUEUE;
    std::atomic<bool> RUNNING;

    static void WriteDNG(int fd) {
        util::ZipWriter zipWriter(fd);
        
        while(RUNNING) {
            std::shared_ptr<Job> job;
            
            JOB_QUEUE.wait_dequeue_timed(job, std::chrono::milliseconds(100));
            if(!job)
                continue;
            
            try {
                util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, zipWriter, job->filename);
            }
            catch(std::runtime_error& e) {
                job->error = e.what();
            }
        }
        
        zipWriter.commit();
    }

    float ConvertVideoToDNG(const std::string& containerPath, const DngProcessorProgress& progress, const int numThreads) {
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
            int fd = progress.onNeedFd(i);
            if(fd < 0)
                continue;
            
            auto t = std::unique_ptr<std::thread>(new std::thread(&WriteDNG, fd));
            
            threads.push_back(std::move(t));
            
            fds.push_back(fd);
        }
        
        if(threads.empty())
            return 0;
        
        for(int i = 0; i < frames.size(); i++) {
            auto frame = container.loadFrame(frames[i]);
            
            if(frame->width <= 0 || frame->height <= 0) {
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
                    
            cv::Mat bayerImage(bayerBuffer.height(), bayerBuffer.width(), CV_16U, bayerBuffer.data());
            
            std::ostringstream str;
            
            str << std::setw(4) << std::setfill('0') << i;
            
            std::string dngFileName = "frame" + str.str() + ".dng";

            while(!JOB_QUEUE.try_enqueue(std::make_shared<Job>(bayerImage, container.getCameraMetadata(), frame->metadata, dngFileName))) {
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
        
        // Stop threads
        RUNNING = false;
        
        // Stop the threads
        for(int i = 0; i < threads.size(); i++)
            threads[i]->join();
        
        for(int i = 0; i < fds.size(); i++)
            progress.onCompleted(i);
        
//         Flush buffers
//        std::shared_ptr<Job> job;
//
//        while(JOB_QUEUE.try_dequeue(job)) {
//            util::WriteDng(job->bayerImage, job->cameraMetadata, job->frameMetadata, job->outputFd);
//        }

        progress.onCompleted();

        return frames.size() / (1e-5f + timestamp);
    }

    void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(containerPath, outputFilePath, progressListener);    
    }

    void ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener) {
        ImageProcessor::process(rawContainer, outputFilePath, progressListener);
    }
}
