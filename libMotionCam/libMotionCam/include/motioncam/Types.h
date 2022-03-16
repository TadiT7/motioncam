#ifndef Types_h
#define Types_h

#include <opencv2/opencv.hpp>

namespace motioncam {
    typedef cv::Vec2f XYCoord;
    typedef cv::Vec3f XYZCoord;

    enum class ColorFilterArrangment : int {
        RGGB = 0,
        GRBG,
        GBRG,
        BGGR,
        RGB,
        MONO,
        INVALID
    };

    // This needs to match the generator input
    enum class PixelFormat : int {
        RAW10 = 0,
        RAW12,
        RAW16,
        YUV_420_888,
        INVALID
    };

    enum class ScreenOrientation : int {
        PORTRAIT = 0,
        REVERSE_PORTRAIT,
        LANDSCAPE,
        REVERSE_LANDSCAPE,
        INVALID
    };

    enum class RawType : int {
        ZSL,
        HDR,
        INVALID
    };

    enum class CompressionType : int {
        UNCOMPRESSED,
        ZSTD,
        V8NZENC,
        P4NZENC,
        BITNZPACK,
        BITNZPACK_2,
        MOTIONCAM,
        INVALID
    };
}

#endif /* Types_h */
