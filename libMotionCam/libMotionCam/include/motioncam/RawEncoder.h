#ifndef RawEncoder_h
#define RawEncoder_h

#include <stddef.h>
#include <stdint.h>

namespace motioncam {
    namespace encoder {    
        enum PixelFormat {
            ANDROID_RAW10 = 0,
            ANDROID_RAW12,
            ANDROID_RAW16
        };

        size_t encode(uint8_t* data,
                      PixelFormat pixelFormat,
                      const int xstart,
                      const int xend,
                      const int ystart,
                      const int yend,
                      const int rowStride);
    
        size_t encodeAndBin(uint8_t* data,
                            PixelFormat pixelFormat,
                            const int xstart,
                            const int xend,
                            const int ystart,
                            const int yend,
                            const int rowStride);
    
        size_t decode(uint16_t* output, const int width, const int height, const uint8_t* input, const size_t len);
    }
}

#endif
