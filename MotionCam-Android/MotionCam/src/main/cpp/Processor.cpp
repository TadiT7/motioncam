#include <jni.h>
#include <string>

#include <motioncam/MotionCam.h>
#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawContainer.h>
#include <motioncam/Util.h>

#include "ImageProcessorListener.h"
#include "DngConverterListener.h"
#include "camera/Logger.h"

#include <android/bitmap.h>

static std::string gLastError;

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessInMemory(
        JNIEnv *env,
        jobject instance,
        jstring outputPath_,
        jobject progressListener)
{
    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    auto container = motioncam::RawBufferManager::get().popPendingContainer();
    if(!container)
        return JNI_FALSE;

    try {
        ImageProcessListener listener(env, progressListener);

        motioncam::ProcessImage(*container, outputPath, listener);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == nullptr) {
            return JNI_FALSE;
        }

        gLastError = e.what();
        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessFile(
    JNIEnv *env,
    jobject instance,
    jstring inputPath_,
    jstring outputPath_,
    jobject progressListener) {

    const char* javaInputPath = env->GetStringUTFChars(inputPath_, nullptr);
    std::string inputPath(javaInputPath);

    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(inputPath_, javaInputPath);
    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    // Process the image
    try {
        ImageProcessListener listener(env, progressListener);

        motioncam::ProcessImage(inputPath, outputPath, listener);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == nullptr) {
            return JNI_FALSE;
        }

        gLastError = e.what();
        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessVideo(
        JNIEnv *env, jobject thiz, jint fd, jint numFramesToMerge, jobject progressListener) {

    // Process the video
    try {
        DngConverterListener listener(env, progressListener);

        motioncam::ConvertVideoToDNG(fd, listener, 4, numFramesToMerge);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == nullptr) {
            return JNI_FALSE;
        }

        gLastError = e.what();
        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_processor_NativeProcessor_GetLastError(JNIEnv *env, __unused jobject instance) {
    return env->NewStringUTF(gLastError.c_str());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_processor_NativeProcessor_GetMetadata(JNIEnv *env, jobject thiz, jint fd) {
    jclass nativeClass = env->FindClass("com/motioncam/processor/ContainerMetadata");

    float frameRate = -1;
    int numFrames = -1;

    try {
        motioncam::GetMetadata(fd, frameRate, numFrames);
    }
    catch(std::runtime_error& error) {
        gLastError = error.what();
    }

    return env->NewObject(nativeClass, env->GetMethodID(nativeClass, "<init>", "(FI)V"), frameRate, numFrames);
}


extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_GenerateVideoPreview(
        JNIEnv *env, jobject thiz, jint fd, jint numPreviews, jobject listener) {

    if(numPreviews == 0)
        return JNI_FALSE;

    jobject listenerClass = env->GetObjectClass(listener);
    if(!listenerClass)
        return JNI_FALSE;

    jmethodID callbackMethod = env->GetMethodID(
            reinterpret_cast<jclass>(listenerClass), "createBitmap", "(II)Landroid/graphics/Bitmap;");

    if(!callbackMethod)
        return JNI_FALSE;

    try {
        motioncam::RawContainer container(fd);
        motioncam::PostProcessSettings settings;

        auto& cameraMetadata = container.getCameraMetadata();
        auto frames = container.getFrames();

        int step = std::max(1, (int) frames.size() / numPreviews);

        for(int i = 0; i < frames.size(); i+=step) {
            auto frame = container.loadFrame(frames[i]);
            if(!frame)
                continue;

            auto output = motioncam::ImageProcessor::createPreview(*frame, 8, cameraMetadata, settings);

            jobject dst = env->CallObjectMethod(listener, callbackMethod, output.width(), output.height());

            // Get bitmap info
            AndroidBitmapInfo bitmapInfo;

            int result = AndroidBitmap_getInfo(env, dst, &bitmapInfo);

            if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
                LOGE("AndroidBitmap_getInfo() failed, error=%d", result);
                return JNI_FALSE;
            }

            if( bitmapInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888    ||
                bitmapInfo.stride != output.width() * 4                 ||
                bitmapInfo.width  != output.width()                     ||
                bitmapInfo.height != output.height())
            {
                LOGE("Invalid bitmap format format=%d, stride=%d, width=%d, height=%d, output.width=%d, output.height=%d",
                     bitmapInfo.format, bitmapInfo.stride, bitmapInfo.width, bitmapInfo.height, output.width(), output.height());

                return JNI_FALSE;
            }

            // Copy pixels
            size_t size = bitmapInfo.width * bitmapInfo.height * 4;
            if(output.size_in_bytes() != size) {
                LOGE("buffer sizes do not match, buffer0=%ld, buffer1=%ld", output.size_in_bytes(), size);
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

            std::copy(output.data(), output.data() + size, (uint8_t*) pixels);

            // Unlock
            result = AndroidBitmap_unlockPixels(env, dst);
            if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
                LOGE("AndroidBitmap_unlockPixels() failed, error=%d", result);
                return JNI_FALSE;
            }
        }
    }
    catch(std::runtime_error& error) {
        gLastError = error.what();
    }

    return JNI_TRUE;
}