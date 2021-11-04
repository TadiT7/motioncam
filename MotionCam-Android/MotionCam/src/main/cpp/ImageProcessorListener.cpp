#include "ImageProcessorListener.h"

ImageProcessListener::ImageProcessListener(_JNIEnv * env, _jobject *progressListener) :
    mEnv(env), mProgressListenerRef(env->NewGlobalRef(progressListener)) {
}

ImageProcessListener::~ImageProcessListener() {
    mEnv->DeleteGlobalRef(mProgressListenerRef);
}

std::string ImageProcessListener::onPreviewSaved(const std::string& outputPath) const {
    struct _jmethodID *onCompletedMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onPreviewSaved",
            "(Ljava/lang/String;)Ljava/lang/String;");

    auto result = (_jstring *)
            mEnv->CallObjectMethod(mProgressListenerRef, onCompletedMethod, mEnv->NewStringUTF(outputPath.c_str()));

    const char *resultStr = mEnv->GetStringUTFChars(result, nullptr);
    std::string metadata(resultStr);

    mEnv->ReleaseStringUTFChars(result, resultStr);

    return metadata;
}

bool ImageProcessListener::onProgressUpdate(int progress) const {
    struct _jmethodID *onProgressMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onProgressUpdate",
            "(I)Z");

    uint8_t result = mEnv->CallBooleanMethod(mProgressListenerRef, onProgressMethod, progress);

    return result == 1;
}

void ImageProcessListener::onCompleted() const {
    struct _jmethodID *onCompletedMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onCompleted",
            "()V");

    mEnv->CallVoidMethod(mProgressListenerRef, onCompletedMethod);
}

void ImageProcessListener::onError(const std::string & error) const {
    struct _jmethodID *onErrorMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onError",
            "(Ljava/lang/String;)V");

    mEnv->CallObjectMethod(mProgressListenerRef, onErrorMethod, mEnv->NewStringUTF(error.c_str()));
}
