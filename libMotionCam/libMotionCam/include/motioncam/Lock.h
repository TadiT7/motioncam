#ifndef Lock_h
#define Lock_h

#include <mutex>
#include "motioncam/Logger.h"

namespace motioncam {

    class Lock {
    public:
        Lock(std::recursive_mutex& l, const std::string& name) : mLock(l), mName(name) {
//            logger::debug("- TRY LOCK " + mName + " -");
            mLock.lock();
//            logger::debug("- LOCKED " + mName + " -");
        }
        
        ~Lock() {
            mLock.unlock();
//            logger::debug("- UNLOCK " + mName + " -");
        }
        
    private:
        const std::string& mName;
        std::recursive_mutex& mLock;
    };
}

#endif /* Lock_h */
