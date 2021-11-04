#include <jni.h>
#include <string>

#include <motioncam/MotionCam.h>
#include <motioncam/RawBufferManager.h>

#include "ImageProcessorListener.h"
#include "DngConverterListener.h"

using namespace motioncam;

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

    auto container = RawBufferManager::get().popPendingContainer();
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
        JNIEnv *env, jobject thiz, jstring inputPath_, jobject progressListener) {

    const char* javaInputPath = env->GetStringUTFChars(inputPath_, nullptr);
    std::string inputPath(javaInputPath);

    // Process the video
    try {
        DngConverterListener listener(env, progressListener);

        motioncam::ConvertVideoToDNG(inputPath, listener, 4);
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
