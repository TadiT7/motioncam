#include <jni.h>
#include <string>

#include <motioncam/MotionCam.h>
#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawContainer.h>
#include <motioncam/Util.h>

#include "ImageProcessorListener.h"
#include "DngConverterListener.h"
#include "JavaUtils.h"
#include "camera/Logger.h"

#include <android/bitmap.h>
#include <unistd.h>

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

        motioncam::MotionCam::ProcessImage(*container, outputPath, listener);
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

        motioncam::MotionCam::ProcessImage(inputPath, outputPath, listener);
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
JNIEXPORT jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessRawVideo(
        JNIEnv *env,
        jobject thiz,
        jintArray jfds,
        jint numFramesToMerge,
        jboolean correctVignette,
        jobject progressListener)
{
    // Process the video
    try {
        DngConverterListener listener(env, progressListener);

        jsize len = env->GetArrayLength(jfds);
        jint* fdsArray = env->GetIntArrayElements(jfds, 0);

        std::vector<int> fds;
        for(int i = 0; i < len; i++)
            fds.push_back(fdsArray[i]);

        motioncam::MotionCam m;
        const std::vector<float> weights = { 0, 0, 0, 0 };

        m.convertVideoToDNG(fds, listener, weights, 2, numFramesToMerge, true, correctVignette);
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

extern "C" JNIEXPORT jobject JNICALL Java_com_motioncam_processor_NativeProcessor_GetRawVideoMetadata(
        JNIEnv *env,
        jobject thiz,
        jintArray jfds)
{
    jclass nativeClass = env->FindClass("com/motioncam/processor/ContainerMetadata");

    float frameRate = 0;
    float duration = 0;
    int numFrames = 0;
    int numSegments = 0;

    try {
        jsize len = env->GetArrayLength(jfds);
        jint* fdsArray = env->GetIntArrayElements(jfds, 0);

        std::vector<int> fds;
        for(int i = 0; i < len; i++)
            fds.push_back(fdsArray[i]);

        motioncam::MotionCam::GetMetadata(fds, duration, frameRate, numFrames, numSegments);
    }
    catch(std::runtime_error& error) {
        gLastError = error.what();
    }

    return env->NewObject(nativeClass, env->GetMethodID(nativeClass, "<init>", "(FII)V"), frameRate, numFrames, numSegments);
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_GenerateRawVideoPreview(
        JNIEnv *env,
        jobject thiz,
        jint fd,
        jint numPreviews,
        jobject listener)
{
    if(numPreviews == 0)
        return JNI_FALSE;

    jobject listenerClass = env->GetObjectClass(listener);
    if(!listenerClass)
        return JNI_FALSE;

    jmethodID callbackMethod = env->GetMethodID(
            reinterpret_cast<jclass>(listenerClass), "createBitmap", "(III)Landroid/graphics/Bitmap;");

    if(!callbackMethod)
        return JNI_FALSE;

    try {
        auto container = motioncam::RawContainer::Open(fd);
        if(!container)
            return JNI_FALSE;

        motioncam::PostProcessSettings settings;

        auto& cameraMetadata = container->getCameraMetadata();
        auto frames = container->getFrames();

        int step = std::max(1, (int) frames.size() / numPreviews);

        for(int i = 0; i < frames.size(); i+=step) {
            auto frame = container->loadFrame(frames[i]);
            if(!frame)
                continue;

            auto output = motioncam::ImageProcessor::createFastPreview(*frame, 8, 8, cameraMetadata);
            jobject dst = env->CallObjectMethod(listener, callbackMethod, output.width(), output.height(), 0);

            if(!dst)
                return JNI_FALSE;

            if(!motioncam::CopyBitmap(env, output, dst))
                return JNI_FALSE;
        }
    }
    catch(std::runtime_error& error) {
        gLastError = error.what();
    }

    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_processor_NativeProcessor_CloseFd(JNIEnv *env, jclass thiz, jint fd) {
    close(fd);
}