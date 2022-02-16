#ifndef MOTIONCAM_ANDROID_JAVAUTILS_H
#define MOTIONCAM_ANDROID_JAVAUTILS_H

#include <jni.h>
#include <HalideBuffer.h>

namespace motioncam {
    class JavaEnv {
    public:
        JavaEnv(JavaVM* javaVm);
        ~JavaEnv();

        JNIEnv* getEnv() const {
            return mJniEnv;
        }

    private:
        JavaVM* mVm;
        JNIEnv* mJniEnv;
        bool mShouldDetach;
    };

    jboolean CopyAlphaBitmap(JNIEnv* env, const Halide::Runtime::Buffer<uint8_t>& src, jobject& dst);
    jboolean CopyBitmap(JNIEnv* env, const Halide::Runtime::Buffer<uint8_t>& src, jobject& dst);
}

#endif //MOTIONCAM_ANDROID_JAVAUTILS_H
