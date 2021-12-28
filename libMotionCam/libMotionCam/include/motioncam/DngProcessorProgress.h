#ifndef DngProcessorProgress_h
#define DngProcessorProgress_h

#include <string>

namespace motioncam {
    class DngProcessorProgress {
    public:
        virtual int onNeedFd(int frameNumber) = 0;
        virtual void onCompleted(int fd) = 0;
        virtual bool onProgressUpdate(int progress) = 0;
        virtual void onCompleted() = 0;
        virtual void onError(const std::string& error) = 0;
    };
}

#endif // DngProcessorProgress_h
