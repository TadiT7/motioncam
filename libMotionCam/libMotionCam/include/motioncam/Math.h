#ifndef Math_hpp
#define Math_hpp

#include <cmath>
#include <opencv2/opencv.hpp>

namespace motioncam {
    namespace math {
        
        template<typename T>
        inline static T clamp(T min, T x, T max)
        {
            return std::max(min, std::min(x, max));
        }
        
        inline static float max(const cv::Vec3f& coord) {
            return std::max({coord[0], coord[1], coord[2]});
        }

        inline static float max(const cv::Vec4f& coord) {
            return std::max({coord[0], coord[1], coord[2], coord[3]});
        }
    }
}

#endif /* Math_hpp */
