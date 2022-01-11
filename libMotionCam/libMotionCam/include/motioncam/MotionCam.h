#ifndef MotionCam_hpp
#define MotionCam_hpp

#include <string>
#include <vector>
#include <memory>

#include "motioncam/ImageProcessorProgress.h"
#include "motioncam/DngProcessorProgress.h"

namespace motioncam {
    class RawContainer;
    struct ContainerFrame;
    struct Impl;

    class MotionCam {
    public:
        MotionCam();
        ~MotionCam();

        void convertVideoToDNG(std::vector<std::unique_ptr<RawContainer> >& containers,
                               DngProcessorProgress& progress,
                               const int numThreads,
                               const int mergeFrames,
                               const bool enableCompression,
                               const bool applyShadingMap);

        void convertVideoToDNG(const std::vector<std::string>& inputFile,
                               DngProcessorProgress& progress,
                               const int numThreads=4,
                               const int mergeFrames=0,
                               const bool enableCompression=true,
                               const bool applyShadingMap=true);

        void convertVideoToDNG(std::vector<int>& fds,
                               DngProcessorProgress& progress,
                               const int numThreads=4,
                               const int mergeFrames=0,
                               const bool enableCompression=true,
                               const bool applyShadingMap=true);

        static void ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener);
        static void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener);

        static void GetMetadata(const std::string& filename, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments);
        static void GetMetadata(const std::vector<std::string>& paths, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments);
        static void GetMetadata(const std::vector<int>& fds, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments);
        static void GetMetadata(
            const std::vector<std::unique_ptr<RawContainer>>& containers,
            float& outDurationMs,
            float& outFrameRate,
            int& outNumFrames,
            int& outNumSegments);

    private:
        static void GetOrderedFrames(
            const std::vector<std::unique_ptr<RawContainer>>& containers,
            std::vector<ContainerFrame>& outOrderedFrames);

        void writeDNG();

    private:
        std::unique_ptr<Impl> mImpl;
    };
}

#endif /* MotionCam_hpp */
