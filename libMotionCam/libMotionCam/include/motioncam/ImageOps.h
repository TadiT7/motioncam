#ifndef ImageOps_hpp
#define ImageOps_hpp

#include <math.h>
#include <vector>
#include <HalideBuffer.h>

#include "Math.h"

namespace motioncam {
    
    float estimateNoise(cv::Mat& input, float p=0.5f);
    
    float findMedian(cv::Mat& input, float p=0.5f);
    float findMedian(std::vector<float> nums);
    
    float calculateEnergy(cv::Mat& image);

    // Removing chromatic aberration by digital image processing
    // https://www.spiedigitallibrary.org/journals/optical-engineering/volume-49/issue-6/067002/Removing-chromatic-aberration-by-digital-image-processing/10.1117/1.3455506.short
    void defringe(Halide::Runtime::Buffer<uint16_t>& output, Halide::Runtime::Buffer<uint16_t>& input);
}

#endif /* ImageOps_hpp */
