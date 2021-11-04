#include "DngConverterListener.h"

DngConverterListener::DngConverterListener(_JNIEnv * env, _jobject *progressListener) :
        mEnv(env), mProgressListenerRef(env->NewGlobalRef(progressListener)) {
}

DngConverterListener::~DngConverterListener() {
    mEnv->DeleteGlobalRef(mProgressListenerRef);
}

int DngConverterListener::onNeedFd(int frameNumber) const {
    struct _jmethodID *onNeedFd = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onNeedFd",
            "(I)I");

    return mEnv->CallIntMethod(mProgressListenerRef, onNeedFd, frameNumber);
}

void DngConverterListener::onCompleted(int fd) const {
    struct _jmethodID *onSaved = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onCompleted",
            "(I)V");

    mEnv->CallVoidMethod(mProgressListenerRef, onSaved, fd);
}

bool DngConverterListener::onProgressUpdate(int progress) const {
    struct _jmethodID *onProgressUpdate = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onProgressUpdate",
            "(I)Z");

    uint8_t result = mEnv->CallBooleanMethod(mProgressListenerRef, onProgressUpdate, progress);

    return result == 1;
}

void DngConverterListener::onCompleted() const {
    struct _jmethodID *onCompletedMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onCompleted",
            "()V");

    mEnv->CallVoidMethod(mProgressListenerRef, onCompletedMethod);
}

void DngConverterListener::onError(const std::string& error) const {
    struct _jmethodID *onErrorMethod = mEnv->GetMethodID(
            mEnv->GetObjectClass(mProgressListenerRef),
            "onError",
            "(Ljava/lang/String;)V");

    mEnv->CallObjectMethod(mProgressListenerRef, onErrorMethod, mEnv->NewStringUTF(error.c_str()));
}
