#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include <android/bitmap.h>

#include <motioncam/Settings.h>
#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawImageBuffer.h>
#include <json11/json11.hpp>

#include "NativeCameraBridgeListener.h"
#include "NativeRawPreviewListener.h"
#include "AudioRecorder.h"

#include "camera/CameraSession.h"
#include "camera/Exceptions.h"
#include "camera/Logger.h"
#include "camera/CameraSessionListener.h"
#include "camera/RawPreviewListener.h"
#include "camera/RawImageConsumer.h"
#include "camera/CaptureSessionManager.h"
#include "camera/CameraSession.h"
#include "JavaUtils.h"

using namespace motioncam;

static const int TYPE_RAW_PREVIEW = 0;
static const int TYPE_WHITE_LEVEL = 1;
static const int TYPE_BLACK_LEVEL = 2;

namespace {
    std::shared_ptr<CaptureSessionManager> gCaptureSessionManager = nullptr;
    std::shared_ptr<CameraDescription> gActiveCameraDescription = nullptr;
    std::shared_ptr<CameraSession> gCameraSession = nullptr;
    std::shared_ptr<motioncam::AudioInterface> gAudioRecorder = nullptr;

    std::string gLastError;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_Create(
        JNIEnv* env, jobject instance) {

    if(gCameraSession != nullptr) {
        gLastError = "Capture session already exists";
        return JNI_FALSE;
    }

    // Try to create the session
    try {
        gCameraSession = std::make_shared<CameraSession>();
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return JNI_FALSE;
    }

    return reinterpret_cast<long> (gCaptureSessionManager.get());
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCamera_Acquire(JNIEnv *env, jobject thiz)
{
    return JNI_FALSE;
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_camera_NativeCamera_Destroy(JNIEnv *env, jobject instance) {

    if(gCameraSession) {
        gCameraSession = nullptr;
    }
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_camera_NativeCamera_GetLastError(JNIEnv *env, jobject instance) {
    return env->NewStringUTF(gLastError.c_str());
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_StartCapture(
        JNIEnv *env,
        jobject instance,
        jstring jcameraId,
        jobject previewSurface,
        jboolean setupForRawPreview,
        jboolean preferRaw12,
        jboolean preferRaw16,
        jstring jcameraStartupSettingsJson,
        jobject listener,
        jlong maxMemoryUsageBytes)
{
    if(!gCaptureSessionManager || !gCameraSession) {
        return JNI_FALSE;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        return JNI_FALSE;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    // Get startup settings
    const char* jstartupSettingsChar = env->GetStringUTFChars(jcameraStartupSettingsJson, nullptr);
    if(jstartupSettingsChar == nullptr) {
        LOGE("Failed to get startup settings");
        return JNI_FALSE;
    }

    std::string startupSettingsStr(jstartupSettingsChar);
    env->ReleaseStringUTFChars(jcameraStartupSettingsJson, jstartupSettingsChar);

    // If there's an error parsing these settings, it doesn't matter we'll ignore them
    // later on
    std::string err;
    json11::Json cameraStartupSettings = json11::Json::parse(startupSettingsStr, err);

    try {
        LOGD("Clearing buffer manager");
        RawBufferManager::get().reset();

        OutputConfiguration outputConfig, previewOutputConfig;
        auto cameraDesc = gCaptureSessionManager->getCameraDescription(cameraId);

        if(!cameraDesc)
            throw CameraSessionException("Invalid camera");

        if(!gCaptureSessionManager->getRawConfiguration(*cameraDesc, preferRaw12, preferRaw16, outputConfig)) {
            throw CameraSessionException("Failed to get output configuration");
        }

        LOGD("RAW output %dx%d format: %d",
             outputConfig.outputSize.width(), outputConfig.outputSize.height(), outputConfig.format);

        if(!gCaptureSessionManager->getPreviewConfiguration(*cameraDesc, outputConfig.outputSize, outputConfig.outputSize, previewOutputConfig)) {
            throw CameraSessionException("Failed to get preview configuration");
        }

        LOGD("Preview output %dx%d format: %d",
             previewOutputConfig.outputSize.width(), previewOutputConfig.outputSize.height(), outputConfig.format);

        // Create image consumer if we have not done so
        auto nativeListener = std::make_shared<NativeCameraBridgeListener>(env, listener);

        std::shared_ptr<ANativeWindow> window(ANativeWindow_fromSurface(env, previewSurface), ANativeWindow_release);

        LOGD("Starting camera %s", cameraId.c_str());

        // Keep camera description
        gActiveCameraDescription = cameraDesc;

        gCameraSession->openCamera(
                nativeListener,
                cameraDesc,
                outputConfig,
                previewOutputConfig,
                gCaptureSessionManager->cameraManager(),
                window,
                setupForRawPreview,
                cameraStartupSettings,
                maxMemoryUsageBytes);
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCamera_StopCapture(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    try {
        LOGD("Stopping camera");
        gCameraSession->closeCamera();
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return JNI_FALSE;
    }

    LOGD("Clearing camera session");
    gCameraSession = nullptr;

    LOGD("Stop capture completed");

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_PauseCapture(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->pauseCapture();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_ResumeCapture(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->resumeCapture();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_SetManualExposure(
        JNIEnv* env,
        jobject thiz,
        jint iso,
        jlong exposure_time) {

    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setManualExposure(iso, exposure_time);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_SetAutoExposure(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setAutoExposure();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_EnableRawPreview(
        JNIEnv *env, jobject thiz, jobject listener, jint previewQuality, jboolean overrideWb)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    auto rawListener = std::make_shared<NativeRawPreviewListener>(env, listener);
    gCameraSession->enableRawPreview(rawListener, previewQuality);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_DisableRawPreview(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->disableRawPreview();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_UpdateOrientation(JNIEnv *env, jobject thiz, jint orientation) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->updateOrientation(static_cast<ScreenOrientation>(orientation));

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetFocusPoint(
        JNIEnv *env, jobject thiz, jfloat focusX, jfloat focusY, jfloat exposureX, jfloat exposureY)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setFocusPoint(focusX, focusY, exposureX, exposureY);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetAutoFocus(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setAutoFocus();

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jobjectArray JNICALL Java_com_motioncam_camera_NativeCamera_GetAvailableImages(JNIEnv *env, jobject thiz)
{
    // Construct result
    jclass nativeCameraBufferClass = env->FindClass("com/motioncam/camera/NativeCameraBuffer");
    jobjectArray result;

    // Return available buffers
    auto lockedBuffers = RawBufferManager::get().consumeAllBuffers();
    if(!lockedBuffers)
        return nullptr;

    auto buffers = lockedBuffers->getBuffers();

    result = env->NewObjectArray(static_cast<jsize>(buffers.size()), nativeCameraBufferClass, nullptr);
    if(result == nullptr)
        return nullptr;

    for (size_t i = 0; i < buffers.size(); i++) {
        jobject obj =
                env->NewObject(
                        nativeCameraBufferClass,
                        env->GetMethodID(nativeCameraBufferClass, "<init>", "(JIJIII)V"),
                        buffers[i]->metadata.timestampNs,
                        buffers[i]->metadata.iso,
                        buffers[i]->metadata.exposureTime,
                        static_cast<int>(buffers[i]->metadata.screenOrientation),
                        buffers[i]->width,
                        buffers[i]->height);

        env->SetObjectArrayElement(result, i, obj);
    }

    return result;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_NativeCamera_GetPreviewSize(JNIEnv *env, jobject thiz, jint downscaleFactor)
{
    // Get first available buffer and return size divided by downscale factor
    jobject captureSize;

    auto lockedBuffer = RawBufferManager::get().consumeLatestBuffer();
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return nullptr;

    auto imageBuffer = lockedBuffer->getBuffers().front();

    // Return as Size instance
    jclass cls = env->FindClass("android/util/Size");
    captureSize =
            env->NewObject(
                    cls,
                    env->GetMethodID(cls, "<init>", "(II)V"),
                    imageBuffer->width / downscaleFactor / 2,
                    imageBuffer->height / downscaleFactor / 2
            );

    return captureSize;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_CreateImagePreview(
        JNIEnv *env,
        jobject thiz,
        jlong bufferHandle,
        jstring postProcessSettings_,
        jint downscaleFactor,
        jobject dst)
{
    if(!gActiveCameraDescription) {
        return JNI_FALSE;
    }

    // Parse post process settings
    const char* cJsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cJsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string jsonString(cJsonString);

    env->ReleaseStringUTFChars(postProcessSettings_, cJsonString);

    std::string err;
    json11::Json json = json11::Json::parse(jsonString, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    auto lockedBuffer = RawBufferManager::get().consumeBuffer(bufferHandle);
    if(!lockedBuffer || lockedBuffer->getBuffers().empty()) {
        LOGE("Failed to get image buffer");
        return JNI_FALSE;
    }

    auto imageBuffer = lockedBuffer->getBuffers().front();

    // Create preview from image buffer
    auto output = ImageProcessor::createPreview(*imageBuffer, downscaleFactor, gActiveCameraDescription->metadata, settings);

    motioncam::CopyBitmap(env, output, dst);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jdouble JNICALL Java_com_motioncam_camera_NativeCamera_MeasureSharpness(
        JNIEnv *env,
        jobject thiz,
        jlong bufferHandle)
{
    if(!gActiveCameraDescription)
        return 0;

    auto lockedBuffer = RawBufferManager::get().consumeBuffer(bufferHandle);
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return -1e10;

    auto imageBuffer = lockedBuffer->getBuffers().front();

    return ImageProcessor::measureSharpness(gActiveCameraDescription->metadata, *imageBuffer);
}

extern "C"
JNIEXPORT jstring JNICALL Java_com_motioncam_camera_NativeCamera_EstimatePostProcessSettings(
        JNIEnv *env, jobject thiz, jfloat shadowsBias)
{
    if(!gActiveCameraDescription)
        return nullptr;

    motioncam::PostProcessSettings settings;

    auto lockedBuffer = RawBufferManager::get().consumeLatestBuffer();
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return nullptr;

    auto imageBuffer = lockedBuffer->getBuffers().front();
    float shadingMapCorrection;

    ImageProcessor::estimateSettings(*imageBuffer, gActiveCameraDescription->metadata, settings, shadingMapCorrection);

    json11::Json::object settingsJson;

    settings.toJson(settingsJson);

    return env->NewStringUTF(json11::Json(settingsJson).dump().c_str());
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_SetExposureCompensation(JNIEnv *env, jobject thiz, jfloat value)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setExposureCompensation(value);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jfloat JNICALL Java_com_motioncam_camera_NativeCamera_EstimateShadows(JNIEnv *env, jobject thiz, jfloat bias) {
    if(!gActiveCameraDescription) {
        return -1.0f;
    }

    auto lockedBuffer = RawBufferManager::get().consumeLatestBuffer();
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return -1;

    auto imageBuffer = lockedBuffer->getBuffers().front();
    float shiftAmount;

    cv::Mat histogram = motioncam::ImageProcessor::calcHistogram(gActiveCameraDescription->metadata, *imageBuffer, false, 4, shiftAmount);

    float ev = motioncam::ImageProcessor::calcEv(gActiveCameraDescription->metadata, imageBuffer->metadata);
    float keyValue = 1.03f - bias / (bias + std::log10(std::pow(10.0f, ev) + 1));

    return motioncam::ImageProcessor::estimateShadows(histogram, keyValue);
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_SetRawPreviewSettings(
        JNIEnv *env,
        jobject thiz,
        jfloat shadows,
        jfloat contrast,
        jfloat saturation,
        jfloat blacks,
        jfloat whitePoint,
        jfloat tempOffset,
        jfloat tintOffset,
        jboolean useVideoPreview)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->updateRawPreviewSettings(
            shadows,
            contrast,
            saturation,
            blacks,
            whitePoint,
            tempOffset,
            tintOffset,
            useVideoPreview);

    return JNI_TRUE;
}


extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_CaptureImage(
        JNIEnv *env,
        jobject instance,
        jlong bufferHandle,
        jint saveNumImages,
        jstring postProcessSettings_,
        jstring outputPath_)
{
    if(!gActiveCameraDescription) {
        return JNI_FALSE;
    }

    const char *coutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    if(coutputPath == nullptr) {
        LOGE("Failed to get output path");
        return JNI_FALSE;
    }

    std::string outputPath(coutputPath);

    env->ReleaseStringUTFChars(outputPath_, coutputPath);

    const char* cjsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cjsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string settingsJson(cjsonString);

    env->ReleaseStringUTFChars(outputPath_, cjsonString);

    // Parse post process settings
    std::string err;
    json11::Json json = json11::Json::parse(settingsJson, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    RawBufferManager::get().save(
            gActiveCameraDescription->metadata, bufferHandle, saveNumImages, settings, std::string(outputPath));

    return JNI_TRUE;
}


extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_CaptureZslHdrImage(
        JNIEnv *env,
        jobject instance,
        jint numImages,
        jstring postProcessSettings_,
        jstring outputPath_)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    const char *coutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    if(coutputPath == nullptr) {
        LOGE("Failed to get output path");
        return JNI_FALSE;
    }

    std::string outputPath(coutputPath);

    env->ReleaseStringUTFChars(outputPath_, coutputPath);

    const char* cjsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cjsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string settingsJson(cjsonString);

    env->ReleaseStringUTFChars(outputPath_, cjsonString);

    // Parse post process settings
    std::string err;
    json11::Json json = json11::Json::parse(settingsJson, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    gCameraSession->captureHdr(numImages, settings, outputPath);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCamera_CaptureHdrImage(
    JNIEnv *env,
    jobject thiz,
    jint numImages,
    jint baseIso,
    jlong baseExposure,
    jint hdrIso,
    jlong hdrExposure,
    jstring postProcessSettings_,
    jstring outputPath_)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    const char *coutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    if(coutputPath == nullptr) {
        LOGE("Failed to get output path");
        return JNI_FALSE;
    }

    std::string outputPath(coutputPath);

    env->ReleaseStringUTFChars(outputPath_, coutputPath);

    const char* cjsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cjsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string settingsJson(cjsonString);

    env->ReleaseStringUTFChars(outputPath_, cjsonString);

    // Parse post process settings
    std::string err;
    json11::Json json = json11::Json::parse(settingsJson, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    gCameraSession->captureHdr(numImages, baseIso, baseExposure, hdrIso, hdrExposure, settings, outputPath);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_camera_NativeCamera_GetRawPreviewEstimatedSettings(JNIEnv* env, jobject thiz)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    PostProcessSettings settings;

    gCameraSession->getEstimatedPostProcessSettings(settings);

    json11::Json::object settingsJson;
    settings.toJson(settingsJson);

    return env->NewStringUTF(json11::Json(settingsJson).dump().c_str());
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_camera_NativeCamera_PrepareHdrCapture(
        JNIEnv *env,
        jobject thiz,
        jint iso,
        jlong exposure)
{
    if(!gCameraSession) {
        return;
    }

    gCameraSession->prepareHdr(iso, exposure);
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCamera_StartStreamToFile(
        JNIEnv *env, jobject thiz,
        jintArray jfds, jint audioFd, jint audioDeviceId, jboolean enableCompression, jint numThreads)
{
    if(!gActiveCameraDescription) {
        return JNI_FALSE;
    }

    jsize len = env->GetArrayLength(jfds);
    jint* fdsArray = env->GetIntArrayElements(jfds, 0);

    std::vector<int> fds;

    for(int i = 0; i < len; i++) {
        if(fdsArray[i] >= 0)
            fds.push_back(fdsArray[i]);
    }

    if(fds.empty())
        return JNI_FALSE;

    if(gAudioRecorder)
        return JNI_FALSE;

    gAudioRecorder = std::make_shared<AudioRecorder>(audioDeviceId);

    RawBufferManager::get().enableStreaming(
            fds, audioFd, gAudioRecorder, enableCompression, numThreads, gActiveCameraDescription->metadata);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_EndStream(JNIEnv *env, jobject thiz) {
    RawBufferManager::get().endStreaming();

    gAudioRecorder = nullptr;
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_SetFrameRate(
        JNIEnv *env, jobject thiz, jint frameRate)
{
    if(!gCameraSession) {
        return;
    }

    gCameraSession->setFrameRate(frameRate);
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_SetVideoCropPercentage(JNIEnv *env, jobject thiz, jint horizontal, jint vertical)
{
    RawBufferManager::get().setCropAmount(horizontal, vertical);
}

extern "C"
JNIEXPORT jobject JNICALL Java_com_motioncam_camera_NativeCamera_GetVideoRecordingStats(JNIEnv *env, jobject thiz) {

    size_t memoryUseBytes, writtenBytes;
    float fps;

    RawBufferManager::get().recordingStats(memoryUseBytes, fps, writtenBytes);
    float bufferUse = RawBufferManager::get().bufferSpaceUse();

    jclass nativeClass = env->FindClass("com/motioncam/camera/VideoRecordingStats");
    return env->NewObject(
            nativeClass,
            env->GetMethodID(nativeClass, "<init>", "(FFJ)V"),
            bufferUse,
            fps,
            static_cast<jlong>(writtenBytes));
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetAWBLock(JNIEnv *env, jobject thiz, jboolean lock)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setAWBLock(lock);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCamera_SetAELock(JNIEnv *env, jobject thiz, jboolean lock)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setAELock(lock);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetOIS(JNIEnv *env, jobject thiz, jboolean on)
{
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setOIS(on);

    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetManualFocus(JNIEnv *env, jobject thiz, jfloat focusDistance) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setFocusDistance(focusDistance);

    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetFocusForVideo(JNIEnv *env, jobject thiz, jboolean focusForVideo) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setFocusForVideo(focusForVideo);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_AdjustMemoryUse(
        JNIEnv *env, jobject thiz, jlong maxUseBytes) {

    if(!gCameraSession) {
        return;
    }

    gCameraSession->growMemory(maxUseBytes);
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_SetVideoBin(JNIEnv *env, jobject thiz, jboolean bin) {
    RawBufferManager::get().setVideoBin(bin);
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCamera_GenerateStats(
        JNIEnv *env, jobject thiz, jobject listener) {

    jobject listenerClass = env->GetObjectClass(listener);
    if(!listenerClass)
        return;

    jmethodID callbackMethod = env->GetMethodID(
            reinterpret_cast<jclass>(listenerClass), "createBitmap", "(III)Landroid/graphics/Bitmap;");

    if(!callbackMethod)
        return;

    if(!gActiveCameraDescription) {
        return;
    }

    auto lockedBuffer = RawBufferManager::get().consumeLatestBuffer();
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return;

    auto imageBuffer = lockedBuffer->getBuffers().front();

    Halide::Runtime::Buffer<uint8_t> whiteLevelBuffer;
    Halide::Runtime::Buffer<uint8_t> blackLevelBuffer;

    ImageProcessor::generateStats(*imageBuffer, 8, 8, gActiveCameraDescription->metadata, whiteLevelBuffer, blackLevelBuffer);

    jobject whiteLevelDst = env->CallObjectMethod(
            listener, callbackMethod, whiteLevelBuffer.width(), whiteLevelBuffer.height(), TYPE_WHITE_LEVEL);

    if(!whiteLevelDst)
        return;

    jobject blackLevelDst = env->CallObjectMethod(
            listener, callbackMethod, blackLevelBuffer.width(), blackLevelBuffer.height(), TYPE_BLACK_LEVEL);

    if(!blackLevelDst)
        return;

    motioncam::CopyAlphaBitmap(env, whiteLevelBuffer, whiteLevelDst);
    motioncam::CopyAlphaBitmap(env, blackLevelBuffer, blackLevelDst);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCamera_SetLensAperture(JNIEnv *env, jobject thiz, jfloat lensAperture) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->setLensAperture(lensAperture);

    return JNI_TRUE;

}
extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCamera_ActivateCameraSettings(JNIEnv *env, jobject thiz) {
    if(!gCameraSession) {
        return JNI_FALSE;
    }

    gCameraSession->activateCameraSettings();

    return JNI_TRUE;
}

//
// Camera manager
//

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCameraManager_CreateCameraManager(JNIEnv *env, jobject thiz) {
    gCaptureSessionManager = std::make_shared<CaptureSessionManager>();
}

extern "C"
JNIEXPORT void JNICALL Java_com_motioncam_camera_NativeCameraManager_DestroyCameraManager(JNIEnv *env, jobject thiz) {
    if(gCaptureSessionManager)
        gCaptureSessionManager = nullptr;
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_motioncam_camera_NativeCameraManager_GetSupportedCameras(JNIEnv *env, jobject thiz) {
    if(!gCaptureSessionManager) {
        return nullptr;
    }

    auto supportedCameras = gCaptureSessionManager->getSupportedCameras();

    jclass nativeCameraInfoClass = env->FindClass("com/motioncam/camera/NativeCameraInfo");

    jobjectArray result = env->NewObjectArray(static_cast<jsize>(supportedCameras.size()), nativeCameraInfoClass, nullptr);
    if(result == nullptr)
        return nullptr;

    for(size_t i = 0; i < supportedCameras.size(); i++) {
        auto desc = gCaptureSessionManager->getCameraDescription(supportedCameras[i]);

        jstring jcameraId = env->NewStringUTF(supportedCameras[i].c_str());

        // Find supported fixed frame rates
        auto it = desc->availableFpsRange.begin();
        std::vector<int> fixedFpsRange;

        while(it != desc->availableFpsRange.end()) {
            if(it->first == it->second) {
                fixedFpsRange.push_back(it->first);
            }
            ++it;
        }

        // Make sure there's at least one entry
        if(fixedFpsRange.empty())
            fixedFpsRange.push_back(30);

        jintArray fpsRanges = env->NewIntArray(fixedFpsRange.size());
        if(fpsRanges != nullptr) {
            env->SetIntArrayRegion(fpsRanges, 0, fixedFpsRange.size(), fixedFpsRange.data());
        }

        jobject obj =
                env->NewObject(
                        nativeCameraInfoClass,
                        env->GetMethodID(nativeCameraInfoClass, "<init>", "(Ljava/lang/String;ZIIII[I)V"),
                        jcameraId,
                        desc->lensFacing == ACAMERA_LENS_FACING_FRONT,
                        desc->exposureCompensationRange[0],
                        desc->exposureCompensationRange[1],
                        desc->exposureCompensationStepFraction[0],
                        desc->exposureCompensationStepFraction[1],
                        fpsRanges);

        env->DeleteLocalRef(jcameraId);

        env->SetObjectArrayElement(result, i, obj);
    }

    return result;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_NativeCameraManager_GetMetadata(JNIEnv *env, jobject thiz, jstring jcameraId) {
    if(!gCaptureSessionManager) {
        LOGW("Session manager null. Failed to get metadata");
        return nullptr;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    auto cameraDesc = gCaptureSessionManager->getCameraDescription(cameraId);

    // Construct result
    jclass nativeCameraMetadataClass = env->FindClass("com/motioncam/camera/NativeCameraMetadata");

    jfloatArray apertures = env->NewFloatArray(cameraDesc->metadata.apertures.size());
    env->SetFloatArrayRegion(
            apertures,
            0,
            cameraDesc->metadata.apertures.size(),
            &cameraDesc->metadata.apertures[0]);

    jfloatArray focalLengths = env->NewFloatArray(cameraDesc->metadata.focalLengths.size());
    env->SetFloatArrayRegion(
            focalLengths,
            0,
            cameraDesc->metadata.focalLengths.size(),
            &cameraDesc->metadata.focalLengths[0]);

    bool oisSupport = true;
    for(auto& mode : cameraDesc->oisModes) {
        if(mode == ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_ON)
            oisSupport = true;
    }

    jobject obj =
            env->NewObject(
                    nativeCameraMetadataClass,
                    env->GetMethodID(nativeCameraMetadataClass, "<init>", "(IIIJJII[F[FFFZ)V"),
                    cameraDesc->sensorOrientation,
                    cameraDesc->isoRange[0],
                    cameraDesc->isoRange[1],
                    cameraDesc->exposureRange[0],
                    cameraDesc->exposureRange[1],
                    cameraDesc->maxAfRegions,
                    cameraDesc->maxAeRegions,
                    apertures,
                    focalLengths,
                    cameraDesc->minimumFocusDistance,
                    cameraDesc->hyperFocalDistance,
                    !cameraDesc->oisModes.empty());

    return obj;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_NativeCameraManager_GetRawOutputSize(JNIEnv *env, jobject thiz, jstring jcameraId) {
    if(!gCaptureSessionManager) {
        return nullptr;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    try {
        motioncam::OutputConfiguration rawOutputConfig;
        auto cameraDesc = gCaptureSessionManager->getCameraDescription(cameraId);

        if(gCaptureSessionManager->getRawConfiguration(*cameraDesc, false, false, rawOutputConfig)) {
            jclass cls = env->FindClass("android/util/Size");

            jobject captureSize =
                    env->NewObject(
                            cls,
                            env->GetMethodID(cls, "<init>", "(II)V"),
                            rawOutputConfig.outputSize.width(),
                            rawOutputConfig.outputSize.height());

            return captureSize;
        }

    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
    }

    return nullptr;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_NativeCameraManager_GetPreviewOutputSize(JNIEnv *env,
                                                                   jobject thiz,
                                                                   jstring jcameraId,
                                                                   jobject captureSize,
                                                                   jobject displaySize) {
    if(!gCaptureSessionManager) {
        return nullptr;
    }

    jclass sizeClass = env->GetObjectClass(captureSize);

    jmethodID getWidthMethod = env->GetMethodID(sizeClass, "getWidth", "()I");
    jmethodID getHeightMethod = env->GetMethodID(sizeClass, "getHeight", "()I");

    int captureWidth = env->CallIntMethod(captureSize, getWidthMethod);
    int captureHeight = env->CallIntMethod(captureSize, getHeightMethod);

    int displayWidth = env->CallIntMethod(displaySize, getWidthMethod);
    int displayHeight = env->CallIntMethod(displaySize, getHeightMethod);

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    try {
        DisplayDimension captureDimens = DisplayDimension(captureWidth, captureHeight);
        DisplayDimension displayDimens = DisplayDimension(displayWidth, displayHeight);
        auto cameraDesc = gCaptureSessionManager->getCameraDescription(cameraId);

        motioncam::OutputConfiguration previewSize;

        if(gCaptureSessionManager->getPreviewConfiguration(*cameraDesc, captureDimens, displayDimens, previewSize)) {
            jobject previewSizeInstance =
                    env->NewObject(
                            sizeClass,
                            env->GetMethodID(sizeClass, "<init>", "(II)V"),
                            previewSize.outputSize.width(),
                            previewSize.outputSize.height());

            return previewSizeInstance;
        }
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
    }

    return nullptr;
}
