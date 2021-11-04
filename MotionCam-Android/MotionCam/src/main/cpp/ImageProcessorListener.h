#ifndef MOTIONCAM_ANDROID_IMAGEPROCESSORLISTENER_H
#define MOTIONCAM_ANDROID_IMAGEPROCESSORLISTENER_H

#include <motioncam/ImageProcessorProgress.h>
#include <jni.h>
#include <string>

class ImageProcessListener : public motioncam::ImageProcessorProgress {
public:
    ImageProcessListener(_JNIEnv * env, _jobject *progressListener);
    ~ImageProcessListener();

    std::string onPreviewSaved(const std::string& outputPath) const override;
    bool onProgressUpdate(int progress) const override;
    void onCompleted() const override;
    void onError(const std::string& error) const override;

private:
    _JNIEnv * mEnv;
    _jobject *mProgressListenerRef;
};

#endif //MOTIONCAM_ANDROID_IMAGEPROCESSORLISTENER_H
