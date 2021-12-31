#ifndef DngProcessorProgress_h
#define DngProcessorProgress_h

#include <string>

namespace motioncam {
    class DngProcessorProgress {
    public:
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
        virtual int onNeedFd(int frameNumber) = 0;
#elif defined(_WIN32)
        virtual std::string onNeedFd(int frameNumber) = 0;
#endif
        virtual bool onProgressUpdate(int progress) = 0;
        virtual void onCompleted() = 0;
        virtual void onError(const std::string& error) = 0;
    };
}

#endif // DngProcessorProgress_h
