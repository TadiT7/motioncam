#include "JavaUtils.h"
#include "camera/Logger.h"

#include <android/bitmap.h>

namespace {
    JNIEnv *GetEnv(JavaVM* javaVm, bool& needDetach) {
        JNIEnv *env = nullptr;
        jint envStatus = javaVm->GetEnv((void **) &env, JNI_VERSION_1_6);
        needDetach = false;
        if (envStatus == JNI_EDETACHED) {
            needDetach = true;

            if (javaVm->AttachCurrentThread(&env, nullptr) != 0) {
                LOGE("Failed to attach to thread.");
                return nullptr;
            }
        }

        return env;
    }
}

namespace motioncam {
    JavaEnv::JavaEnv(JavaVM* javaVm) : mVm(javaVm), mShouldDetach(false)
    {
        mJniEnv = GetEnv(javaVm, mShouldDetach);
    }

    JavaEnv::~JavaEnv()
    {
        if(mShouldDetach)
            mVm->DetachCurrentThread();
    }

    jboolean CopyAlphaBitmap(JNIEnv* env, const Halide::Runtime::Buffer<uint8_t>& src, jobject& dst) {
        AndroidBitmapInfo bitmapInfo;

        int result = AndroidBitmap_getInfo(env, dst, &bitmapInfo);

        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_getInfo() failed, error=%d", result);
            return JNI_FALSE;
        }

        if( bitmapInfo.format != ANDROID_BITMAP_FORMAT_A_8      ||
            bitmapInfo.stride != src.width()                    ||
            bitmapInfo.width  != src.width()                    ||
            bitmapInfo.height != src.height())
        {
            LOGE("Invalid bitmap format format=%d, stride=%d, width=%d, height=%d, output.width=%d, output.height=%d",
                 bitmapInfo.format, bitmapInfo.stride, bitmapInfo.width, bitmapInfo.height, src.width(), src.height());

            return JNI_FALSE;
        }

        // Copy pixels
        size_t size = bitmapInfo.width * bitmapInfo.height;
        if(src.size_in_bytes() != size) {
            LOGE("buffer sizes do not match, buffer0=%ld, buffer1=%ld", src.size_in_bytes(), size);
            return JNI_FALSE;
        }

        // Copy pixels to bitmap
        void* pixels = nullptr;

        // Lock
        result = AndroidBitmap_lockPixels(env, dst, &pixels);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_lockPixels() failed, error=%d", result);
            return JNI_FALSE;
        }

        std::copy(src.data(), src.data() + size, (uint8_t*) pixels);

        // Unlock
        result = AndroidBitmap_unlockPixels(env, dst);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_unlockPixels() failed, error=%d", result);
            return JNI_FALSE;
        }

        return JNI_TRUE;
    }

    jboolean CopyBitmap(JNIEnv* env, const Halide::Runtime::Buffer<uint8_t>& src, jobject& dst) {
        // Get bitmap info
        AndroidBitmapInfo bitmapInfo;

        int result = AndroidBitmap_getInfo(env, dst, &bitmapInfo);

        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_getInfo() failed, error=%d", result);
            return JNI_FALSE;
        }

        if( bitmapInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888    ||
            bitmapInfo.stride != src.width() * 4                 ||
            bitmapInfo.width  != src.width()                     ||
            bitmapInfo.height != src.height())
        {
            LOGE("Invalid bitmap format format=%d, stride=%d, width=%d, height=%d, output.width=%d, output.height=%d",
                 bitmapInfo.format, bitmapInfo.stride, bitmapInfo.width, bitmapInfo.height, src.width(), src.height());

            return JNI_FALSE;
        }

        // Copy pixels
        size_t size = bitmapInfo.width * bitmapInfo.height * 4;
        if(src.size_in_bytes() != size) {
            LOGE("buffer sizes do not match, buffer0=%ld, buffer1=%ld", src.size_in_bytes(), size);
            return JNI_FALSE;
        }

        // Copy pixels to bitmap
        void* pixels = nullptr;

        // Lock
        result = AndroidBitmap_lockPixels(env, dst, &pixels);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_lockPixels() failed, error=%d", result);
            return JNI_FALSE;
        }

        std::copy(src.data(), src.data() + size, (uint8_t*) pixels);

        // Unlock
        result = AndroidBitmap_unlockPixels(env, dst);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_unlockPixels() failed, error=%d", result);
            return JNI_FALSE;
        }

        return JNI_TRUE;
    }
}
