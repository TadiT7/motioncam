#ifndef MotionCam_hpp
#define MotionCam_hpp

#include <string>

#include "motioncam/ImageProcessorProgress.h"
#include "motioncam/DngProcessorProgress.h"

namespace motioncam {
    class RawContainer;

    void ConvertVideoToDNG(std::vector<std::unique_ptr<RawContainer> >& containers,
                           const DngProcessorProgress& progress,
                           const int numThreads,
                           const int mergeFrames);

    void ConvertVideoToDNG(const std::vector<std::string>& inputFile,
                           const DngProcessorProgress& progress,
                           const int numThreads=4,
                           const int mergeFrames=0);

    void ConvertVideoToDNG(std::vector<int>& fds,
                           const DngProcessorProgress& progress,
                           const int numThreads=4,
                           const int mergeFrames=0);

    void ProcessImage(RawContainer& rawContainer, const std::string& outputFilePath, const ImageProcessorProgress& progressListener);
    void ProcessImage(const std::string& containerPath, const std::string& outputFilePath, const ImageProcessorProgress& progressListener);

    void GetMetadata(const std::string& filename, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments);
    void GetMetadata(const std::vector<int>& fds, float& outDurationMs, float& outFrameRate, int& outNumFrames, int& outNumSegments);
    void GetMetadata(
        const std::vector<std::unique_ptr<RawContainer>>& containers,
        float& outDurationMs,
        float& outFrameRate,
        int& outNumFrames,
        int& outNumSegments);
}

#endif /* MotionCam_hpp */
