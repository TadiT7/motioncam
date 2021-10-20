#ifndef RawSequenceConverter_hpp
#define RawSequenceConverter_hpp

#include <string>

namespace motioncam {
#ifdef DNG_SUPPORT
    void ConvertToDNG(const std::string& containerPath, const std::string& outputPath);
#endif
}

#endif /* RawSequenceConverter_hpp */
