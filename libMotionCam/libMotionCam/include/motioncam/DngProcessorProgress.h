#ifndef DngProcessorProgress_h
#define DngProcessorProgress_h

#include <string>

namespace motioncam {
    class DngProcessorProgress {
    public:
        virtual int onNeedFd(int threadNumber) const = 0;
        virtual void onCompleted(int fd) const = 0;
        virtual bool onProgressUpdate(int progress) const = 0;
        virtual void onCompleted() const = 0;
        virtual void onError(const std::string& error) const = 0;
    };
}

#endif // DngProcessorProgress_h
