#include "motioncam/ImageOps.h"
#include "motioncam/Measure.h"

using std::vector;

namespace motioncam {    

    float findMedian(std::vector<float> nums) {
        std::nth_element(nums.begin(), nums.begin() + nums.size() / 2, nums.end());
        
        return nums[nums.size()/2];
    }

    float findMedian(cv::Mat& input, float p) {
        std::vector<float> nums;
        nums.reserve(input.cols * input.rows);
        
        for (int i = 0; i < input.rows; ++i) {
            nums.insert(nums.end(), input.ptr<float>(i), input.ptr<float>(i) + input.cols);
        }

        if(p <= 0.5)
            std::nth_element(nums.begin(), nums.begin() + nums.size() / 2, nums.end());
        else
            std::sort(nums.begin(), nums.end());
        
        return nums[nums.size()*p];
    }
    
    float estimateNoise(cv::Mat& input, float p) {
        cv::Mat d = cv::abs(input);
                
        float median = findMedian(d, p);
        d = cv::abs(d - median);

        return findMedian(d) / 0.6745;
    }
        
    float calculateEnergy(cv::Mat& image) {
        cv::Mat tmp;
        
        cv::Laplacian(image, tmp, CV_8U);
        cv::Scalar energy = cv::mean(tmp);
        
        return energy[0];
    }

    void defringeInternal(Halide::Runtime::Buffer<uint16_t>& out, Halide::Runtime::Buffer<uint16_t>& in, const int threshold)
    {
        cv::parallel_for_(cv::Range(0, 4), [&](const cv::Range& range)
        {
            int S = (int) std::ceil(in.height() / 4.0f);
            
            int y0 = range.start * S;
            int y1 = std::min(range.end * S, in.height() - 1);
            
            for(int y = y0; y < y1; y++) {
                uint16_t* inR = in.begin() + y*in.stride(1) + 0*in.stride(2);
                uint16_t* inG = in.begin() + y*in.stride(1) + 1*in.stride(2);
                uint16_t* inB = in.begin() + y*in.stride(1) + 2*in.stride(2);

                uint16_t* outR = out.begin() + y*out.stride(1) + 0*out.stride(2);
                uint16_t* outG = out.begin() + y*out.stride(1) + 1*out.stride(2);
                uint16_t* outB = out.begin() + y*out.stride(1) + 2*out.stride(2);

                std::memcpy(outG, inG, sizeof(uint16_t)*in.width());
                
                for(int x = 1; x < in.width() - 1; x++)
                {
                    int32_t Ggrad = inG[x + 1] - inG[x - 1];

                    outR[x] = inR[x];
                    outB[x] = inB[x];

                    if(abs(Ggrad) < threshold) {
                        continue;
                    }
                    
                    int32_t sign = std::copysign(1, Ggrad);

                    int lpos = x - 1;
                    int rpos = x + 1;

                    for (; lpos > 0; --lpos) {

                        int32_t R = (inR[lpos + 1] - inR[lpos - 1]) * sign;
                        int32_t G = (inG[lpos + 1] - inG[lpos - 1]) * sign;
                        int32_t B = (inB[lpos + 1] - inB[lpos - 1]) * sign;

                        if (std::max(std::max(B, G), R) < threshold)
                            break;
                    }

                    lpos -= 1;

                    for (; rpos < in.width() - 1; ++rpos)
                    {
                        int32_t R = (inR[rpos + 1] - inR[rpos - 1]) * sign;
                        int32_t G = (inG[rpos + 1] - inG[rpos - 1]) * sign;
                        int32_t B = (inB[rpos + 1] - inB[rpos - 1]) * sign;

                        if (std::max(std::max(B, G), R) < threshold)
                            break;
                    }

                    rpos += 1;

                    int32_t bgMax = std::max((int32_t) inB[lpos] - inG[lpos], (int32_t) inB[rpos] - inG[rpos]);
                    int32_t bgMin = std::min((int32_t) inB[lpos] - inG[lpos], (int32_t) inB[rpos] - inG[rpos]);
                    
                    int32_t rgMax = std::max((int32_t) inR[lpos] - inG[lpos], (int32_t) inR[rpos] - inG[rpos]);
                    int32_t rgMin = std::min((int32_t) inR[lpos] - inG[lpos], (int32_t) inR[rpos] - inG[rpos]);

                    for (int k = lpos; k <= rpos; ++k)
                    {
                        int32_t Bdiff = (int32_t) inB[k] - inG[k];
                        int32_t Rdiff = (int32_t) inR[k] - inG[k];

                        if(Bdiff > bgMax) {
                            outB[k] = cv::saturate_cast<uint16_t>(bgMax + inG[k]);
                        }
                        else if(Bdiff < bgMin)
                            outB[k] = cv::saturate_cast<uint16_t>(bgMin + inG[k]);
                        else
                            outB[k] = inB[k];
                        
                        if(Rdiff > rgMax)
                            outR[k] = cv::saturate_cast<uint16_t>(rgMax + inG[k]);
                        else if(Rdiff < rgMin)
                            outR[k] = cv::saturate_cast<uint16_t>(rgMin + inG[k]);
                        else
                            outR[k] = inR[k];
                    }

                    x = rpos - 2;
                }
            }
        });
    }

    void defringe(Halide::Runtime::Buffer<uint16_t>& output, Halide::Runtime::Buffer<uint16_t>& input) {
        const int threshold = 8000;

        defringeInternal(output, input, threshold);
    }
}
