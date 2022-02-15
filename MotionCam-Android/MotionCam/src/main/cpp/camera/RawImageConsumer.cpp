#include "RawImageConsumer.h"
#include "RawPreviewListener.h"
#include "CameraSessionListener.h"
#include "CameraDescription.h"
#include "Logger.h"
#include "ClHelper.h"
#include "NativeClBuffer.h"
#include "Exceptions.h"

#ifdef GPU_CAMERA_PREVIEW
    #include <HalideRuntimeOpenCL.h>
    #include <motioncam/CameraPreview.h>
#endif

#include <chrono>
#include <memory>
#include <utility>

#include <motioncam/Util.h>
#include <motioncam/Measure.h>
#include <motioncam/Settings.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawContainer.h>
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include <motioncam/ImageProcessor.h>
#include <motioncam/RawImageBuffer.h>

#include <camera/NdkCameraMetadata.h>
#include <motioncam/Logger.h>

namespace motioncam {
    static const int MINIMUM_BUFFERS = 16;
    static const int ESTIMATE_SHADOWS_FRAME_INTERVAL = 6;

#ifdef GPU_CAMERA_PREVIEW
    void VERIFY_RESULT(int32_t errCode, const std::string& errString)
    {
        if(errCode != 0)
            throw RawPreviewException(errString);
    }
#endif

    //

    RawImageConsumer::RawImageConsumer(
            std::shared_ptr<CameraDescription> cameraDescription,
            std::shared_ptr<CameraSessionListener> listener,
            const size_t maxMemoryUsageBytes) :
            mListener(std::move(listener)),
            mMaximumMemoryUsageBytes(maxMemoryUsageBytes),
            mRunning(false),
            mEnableRawPreview(false),
            mRawPreviewQuality(4),
            mCopyCaptureColorTransform(true),
            mShadowBoost(0.0f),
            mTempOffset(0.0f),
            mTintOffset(0.0f),
            mUseVideoPreview(false),
            mPreviewShadows(4.0f),
            mPreviewShadowStep(0.0f),
            mShadingMapCorrection(1.0f),
            mBufferSize(0),
            mFramesSinceEstimatedSettings(0),
            mCameraDesc(std::move(cameraDescription))
    {
    }

    RawImageConsumer::~RawImageConsumer() {
        stop();
    }

    void RawImageConsumer::start() {
        LOGD("Starting image consumer");

        if(mRunning) {
            LOGD("Attempting to start already running image consumer");
            return;
        }

        mRunning = true;
        mBufferSize = 0;

        // Start threads
        mConsumerThread = std::make_unique<std::thread>(&RawImageConsumer::doCopyImage, this);
        mSetupBuffersThread = std::make_unique<std::thread>(&RawImageConsumer::doSetupBuffers, this);
    }

    void RawImageConsumer::stop() {
        if(!mRunning) {
            return;
        }

        disableRawPreview();

        // Stop all threads
        mRunning = false;

        mBufferCondition.notify_one();

        LOGD("Stopping buffers thread");

        if(mSetupBuffersThread && mSetupBuffersThread->joinable())
            mSetupBuffersThread->join();
        mSetupBuffersThread = nullptr;

        LOGD("Stopping consumer threads thread");

        if(mConsumerThread && mConsumerThread->joinable())
            mConsumerThread->join();
        mConsumerThread = nullptr;

        LOGD("Raw image consumer has stopped");
    }

    void RawImageConsumer::grow(size_t memoryLimitBytes) {
        const size_t currentLimitBytes = mMaximumMemoryUsageBytes;
        mMaximumMemoryUsageBytes = memoryLimitBytes;

        RawBufferManager::get().setTargetMemory(memoryLimitBytes);

        if(memoryLimitBytes > currentLimitBytes) {
            mBufferCondition.notify_one();
        }
    }

    void RawImageConsumer::queueImage(AImage* image) {
        mImageQueue.enqueue(std::shared_ptr<AImage>(image, AImage_delete));
    }

    void RawImageConsumer::queueMetadata(const ACameraMetadata* cameraMetadata, ScreenOrientation screenOrientation, RawType rawType) {
        using namespace std::chrono;

        RawImageMetadata metadata;

        if(!copyMetadata(metadata, cameraMetadata)) {
            LOGW("Failed to copy frame metadata. Buffer will be unusable!");
            return;
        }

        // Keep screen orientation and RAW type at time of capture
        metadata.screenOrientation = screenOrientation;
        metadata.rawType = rawType;
        metadata.recvdTimestampMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        // Add pending metadata
        mPendingMetadata.enqueue(std::move(metadata));
    }

    cv::Mat getColorMatrix(ACameraMetadata_const_entry& entry) {
        cv::Mat m(3, 3, CV_32F);

        for(int y = 0; y < 3; y++) {
            for(int x = 0; x < 3; x++) {
                int i = y * 3 + x;

                m.at<float>(y, x) = (float) entry.data.r[i].numerator / (float) entry.data.r[i].denominator;
            }
        }

        return m;
    }

    bool RawImageConsumer::copyMetadata(RawImageMetadata& dst, const ACameraMetadata* src) {
        ACameraMetadata_const_entry metadataEntry;

        // Without the timestamp this metadata is useless
        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_TIMESTAMP, &metadataEntry) == ACAMERA_OK) {
            dst.timestampNs = metadataEntry.data.i64[0];
        }
        else {
            LOGE("ACAMERA_SENSOR_TIMESTAMP error");
            return false;
        }

        // Color balance
        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_NEUTRAL_COLOR_POINT, &metadataEntry) == ACAMERA_OK) {
            dst.asShot[0] = (float) metadataEntry.data.r[0].numerator / (float) metadataEntry.data.r[0].denominator;
            dst.asShot[1] = (float) metadataEntry.data.r[1].numerator / (float) metadataEntry.data.r[1].denominator;
            dst.asShot[2] = (float) metadataEntry.data.r[2].numerator / (float) metadataEntry.data.r[2].denominator;
        }
        else {
            LOGE("ACAMERA_SENSOR_NEUTRAL_COLOR_POINT error");
            return false;
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_SENSITIVITY, &metadataEntry) == ACAMERA_OK) {
            dst.iso = metadataEntry.data.i32[0];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_DYNAMIC_BLACK_LEVEL, &metadataEntry) == ACAMERA_OK) {
            dst.dynamicBlackLevel.resize(4);

            dst.dynamicBlackLevel[0] = metadataEntry.data.f[0];
            dst.dynamicBlackLevel[1] = metadataEntry.data.f[1];
            dst.dynamicBlackLevel[2] = metadataEntry.data.f[2];
            dst.dynamicBlackLevel[3] = metadataEntry.data.f[3];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_DYNAMIC_WHITE_LEVEL, &metadataEntry) == ACAMERA_OK) {
            dst.dynamicWhiteLevel = metadataEntry.data.i32[0];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_EXPOSURE_TIME, &metadataEntry) == ACAMERA_OK) {
            dst.exposureTime = metadataEntry.data.i64[0];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, &metadataEntry) == ACAMERA_OK) {
            dst.exposureCompensation = metadataEntry.data.i32[0];
        }

        // Get lens shading map
        int lensShadingMapWidth;
        int lensShadingMapHeight;

        std::vector<cv::Mat> shadingMap;

        if(ACameraMetadata_getConstEntry(src, ACAMERA_LENS_INFO_SHADING_MAP_SIZE, &metadataEntry) == ACAMERA_OK) {
            lensShadingMapWidth  = metadataEntry.data.i32[0];
            lensShadingMapHeight = metadataEntry.data.i32[1];

            for (int i = 0; i < 4; i++) {
                cv::Mat m(lensShadingMapHeight, lensShadingMapWidth, CV_32F, cv::Scalar(1.0f));
                shadingMap.push_back(m);
            }
        }
        else {
            LOGE("ACAMERA_LENS_INFO_SHADING_MAP_SIZE error");
            return false;
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_STATISTICS_LENS_SHADING_MAP, &metadataEntry) == ACAMERA_OK) {
            for (int y = 0; y < lensShadingMapHeight; y++) {
                int i = y * lensShadingMapWidth * 4;

                for (int x = 0; x < lensShadingMapWidth * 4; x += 4) {
                    shadingMap[0].at<float>(y, x / 4) = metadataEntry.data.f[i + x];
                    shadingMap[1].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 1];
                    shadingMap[2].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 2];
                    shadingMap[3].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 3];
                }
            }
        }
        else {
            LOGE("ACAMERA_STATISTICS_LENS_SHADING_MAP error");
            return false;
        }

        dst.updateShadingMap(std::move(shadingMap));

        // Keep Noise profile
        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_NOISE_PROFILE, &metadataEntry) == ACAMERA_OK) {
            dst.noiseProfile.resize(metadataEntry.count);
            for(int n = 0; n < metadataEntry.count; n++) {
                dst.noiseProfile[n] = metadataEntry.data.d[n];
            }
        }

        // If the color transform is not part of the request we won't attempt tp copy it
        if(mCopyCaptureColorTransform) {
            // ACAMERA_SENSOR_CALIBRATION_TRANSFORM1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_CALIBRATION_TRANSFORM1, &metadataEntry) == ACAMERA_OK) {
                dst.calibrationMatrix1 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_CALIBRATION_TRANSFORM2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_CALIBRATION_TRANSFORM2, &metadataEntry) == ACAMERA_OK) {
                dst.calibrationMatrix2 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_FORWARD_MATRIX1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_FORWARD_MATRIX1, &metadataEntry) == ACAMERA_OK) {
                dst.forwardMatrix1 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_FORWARD_MATRIX2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_FORWARD_MATRIX2, &metadataEntry) == ACAMERA_OK) {
                dst.forwardMatrix2 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_COLOR_TRANSFORM1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_COLOR_TRANSFORM1, &metadataEntry) == ACAMERA_OK) {
                dst.colorMatrix1 = getColorMatrix(metadataEntry);
            }
            else {
                mCopyCaptureColorTransform = false;
            }

            // ACAMERA_SENSOR_COLOR_TRANSFORM2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_COLOR_TRANSFORM2, &metadataEntry) == ACAMERA_OK) {
                dst.colorMatrix2 = getColorMatrix(metadataEntry);
            }
            else {
                mCopyCaptureColorTransform = false;
            }
        }

        return true;
    }

    void RawImageConsumer::onBufferReady(const std::shared_ptr<RawImageBuffer>& buffer) {
        // Skip estimation when in video preview mode
        if(mUseVideoPreview) {
            mPreviewShadows = 1.0f;
            RawBufferManager::get().enqueueReadyBuffer(buffer);
            return;
        }

        // Estimate settings every few frames
        if(mFramesSinceEstimatedSettings >= ESTIMATE_SHADOWS_FRAME_INTERVAL)
        {
            // Keep previous value
            mPreviewShadows = mEstimatedSettings.shadows;

            float shiftAmount;

            motioncam::ImageProcessor::estimateSettings(*buffer, mCameraDesc->metadata, mEstimatedSettings, shiftAmount);

            // Update shadows to include user selected boost
            float shadowBoost = 0.0f;
            if(mEnableRawPreview)
                shadowBoost = mShadowBoost;

            float userShadows = std::pow(2.0f, std::log(mEstimatedSettings.shadows) / std::log(2.0f) + shadowBoost);
            mEstimatedSettings.shadows = std::max(1.0f, std::min(32.0f, userShadows));

            // Store noise profile
            if(!buffer->metadata.noiseProfile.empty()) {
                mEstimatedSettings.noiseSigma = 1024 * sqrt(0.18f * buffer->metadata.noiseProfile[0] + buffer->metadata.noiseProfile[1]);
            }

            mPreviewShadowStep = (1.0f / ESTIMATE_SHADOWS_FRAME_INTERVAL) * (mEstimatedSettings.shadows - mPreviewShadows);
            mFramesSinceEstimatedSettings = 0;
            mShadingMapCorrection = shiftAmount;
        }
        else {
            ++mFramesSinceEstimatedSettings;

            // Interpolate shadows to make transition smoother
            mPreviewShadows = mPreviewShadows + mPreviewShadowStep;
        }

        RawBufferManager::get().enqueueReadyBuffer(buffer);
    }

    void RawImageConsumer::doMatchMetadata() {
        using namespace std::chrono;

        auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        RawImageMetadata metadata;

        std::vector<RawImageMetadata> unmatched;

        while(mPendingMetadata.try_dequeue(metadata)) {
            auto pendingBufferIt = mPendingBuffers.find(metadata.timestampNs);

            // Found a match, set it to the image and remove from pending list
            if(pendingBufferIt != mPendingBuffers.end()) {
                // Update the metadata of the image
                pendingBufferIt->second->metadata = metadata;

                // Return buffer to either preprocess queue or normal queue if raw preview is not enabled
                if( mEnableRawPreview &&
                    mPreprocessQueue.size_approx() < 2 &&
                    pendingBufferIt->second->metadata.rawType == RawType::ZSL)
                {
                    mPreprocessQueue.enqueue(pendingBufferIt->second);
                }
                else {
                    onBufferReady(pendingBufferIt->second);
                }

                // Erase from pending buffer and metadata
                mPendingBuffers.erase(pendingBufferIt);
            }
            else {
                // If the metadata has not been matched within a reasonable amount of time we can
                // return it to the queue
                if(now - metadata.recvdTimestampMs < 5000) {
                    unmatched.push_back(std::move(metadata));
                }
                else {
                    LOGD("Discarding %ld metadata, too old.", metadata.recvdTimestampMs);
                }
            }
        }

        mPendingMetadata.enqueue_bulk(unmatched.begin(), unmatched.size());
    }

    void RawImageConsumer::doSetupBuffers() {
#ifdef GPU_CAMERA_PREVIEW
        {
            // Make sure the OpenCL library is loaded/symbols looked up in Halide
            Halide::Runtime::Buffer<int32_t> buf(32);
            buf.device_malloc(halide_opencl_device_interface());

            // Use relaxed math
            halide_opencl_set_build_options("-cl-fast-relaxed-math -cl-mad-enable");
        }
#endif
        while(mRunning) {
            std::unique_lock<std::mutex> lock(mBufferMutex);

            mBufferCondition.wait(lock);

            // Do we need to allocate more buffers?
            size_t memoryUseBytes = RawBufferManager::get().memoryUseBytes();

            const size_t bufferSize = mBufferSize;
            if(bufferSize <= 0 || memoryUseBytes >= mMaximumMemoryUsageBytes) {
                continue;
            }

            mListener->onMemoryAdjusting();

            while(  mRunning
                    &&  (  memoryUseBytes + bufferSize < mMaximumMemoryUsageBytes
                        || RawBufferManager::get().numBuffers() < MINIMUM_BUFFERS) ) {

                std::shared_ptr<RawImageBuffer> buffer;

    #ifdef GPU_CAMERA_PREVIEW
                buffer = std::make_shared<RawImageBuffer>(std::make_unique<NativeClBuffer>(bufferSize));
    #else
                buffer = std::make_shared<RawImageBuffer>(std::make_unique<NativeHostBuffer>(bufferSize));
    #endif

                RawBufferManager::get().addBuffer(buffer);

                memoryUseBytes = RawBufferManager::get().memoryUseBytes();

                LOGI("Memory use: %zu, max: %zu", memoryUseBytes, mMaximumMemoryUsageBytes);
            }

            mListener->onMemoryStable();
        }

        LOGD("Exiting buffer thread");
    }

#ifdef GPU_CAMERA_PREVIEW
    Halide::Runtime::Buffer<uint8_t> RawImageConsumer::createCameraPreviewOutputBuffer(const RawImageBuffer& buffer, const int downscaleFactor) {
        const int width = buffer.width / 2 / downscaleFactor;
        const int height = buffer.height / 2 / downscaleFactor;
        const int bufSize = width * height * 4;

        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

        cl_mem clOutputBuffer = CL_createBuffer(clContext, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, bufSize, nullptr, &errCode);
        VERIFY_RESULT(errCode, "Failed to create camera preview buffer");

        halide_buffer_t buf = {0};
        halide_dimension_t dim[3];

        buf.dim = &dim[0];
        buf.dimensions = 3;
        buf.dim[0].extent = width;
        buf.dim[0].stride = 4;
        buf.dim[1].extent = height;
        buf.dim[1].stride = width*4;
        buf.dim[2].extent = 4;
        buf.dim[2].stride = 1;
        buf.type = halide_type_of<uint8_t>();

        VERIFY_RESULT(halide_opencl_wrap_cl_mem(nullptr, &buf, (uint64_t) clOutputBuffer), "Failed to wrap camera preview");

        VERIFY_RESULT(CL_release(), "Failed to release CL context");

        return Halide::Runtime::Buffer<uint8_t>(buf);
    }

    void RawImageConsumer::releaseCameraPreviewOutputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer) {
        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

        auto clOutputBuffer = (cl_mem) halide_opencl_get_cl_mem(nullptr, buffer.raw_buffer());
        if(clOutputBuffer == nullptr) {
            throw RawPreviewException("Failed to get CL memory");
        }

        VERIFY_RESULT(halide_opencl_detach_cl_mem(nullptr, buffer.raw_buffer()), "Failed to detach CL memory from buffer");

        errCode = CL_releaseMemObject(clOutputBuffer);
        VERIFY_RESULT(errCode, "Failed to release camera preview buffer");

        CL_release();
    }

    Halide::Runtime::Buffer<uint8_t> RawImageConsumer::wrapCameraPreviewInputBuffer(const RawImageBuffer& buffer) {
        halide_buffer_t buf = {0};
        halide_dimension_t dim;

        buf.dim = &dim;
        buf.dimensions = 1;
        buf.dim[0].extent = buffer.data->len();
        buf.dim[0].stride = 1;
        buf.type = halide_type_of<uint8_t>();

        VERIFY_RESULT(
                halide_opencl_wrap_cl_mem(nullptr, &buf, (uint64_t) buffer.data->nativeHandle()),
                "Failed to wrap camera preview buffer");

        return Halide::Runtime::Buffer<uint8_t>(buf);
    }

    void RawImageConsumer::unwrapCameraPreviewInputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer) {
        VERIFY_RESULT(
                halide_opencl_detach_cl_mem(nullptr, buffer.raw_buffer()),
                "Failed to unwrap camera preview buffer");
    }
#endif

    void RawImageConsumer::doPreprocess() {
#ifdef GPU_CAMERA_PREVIEW
        Halide::Runtime::Buffer<uint8_t> outputBuffer;
        std::shared_ptr<RawImageBuffer> buffer;

        std::chrono::steady_clock::time_point fpsTimestamp = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point previewTimestamp;

        cl_int errCode = -1;

        bool outputCreated = false;
        int downscaleFactor = mRawPreviewQuality;
        int processedFrames = 0;
        double totalPreviewTimeMs = 0;

        while(mEnableRawPreview) {
            if(!mPreprocessQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                continue;
            }

            if(!outputCreated) {
                outputBuffer = createCameraPreviewOutputBuffer(*buffer, downscaleFactor);
                outputCreated = true;
            }

            Halide::Runtime::Buffer<uint8_t> inputBuffer = wrapCameraPreviewInputBuffer(*buffer);

            previewTimestamp = std::chrono::steady_clock::now();

            if(mUseVideoPreview) {
                motioncam::CameraPreview::generate(*buffer, mCameraDesc->metadata, downscaleFactor, mShadingMapCorrection, inputBuffer, outputBuffer);
            }
            else {
                motioncam::CameraPreview::generate(
                        *buffer,
                        mCameraDesc->metadata,
                        downscaleFactor,
                        mShadingMapCorrection,
                        mCameraDesc->lensFacing == ACAMERA_LENS_FACING_FRONT,
                        mPreviewShadows,
                        mEstimatedSettings.contrast,
                        mEstimatedSettings.saturation,
                        mEstimatedSettings.blacks,
                        mEstimatedSettings.whitePoint,
                        mTempOffset,
                        mTintOffset,
                        0.25f,
                        inputBuffer,
                        outputBuffer);
            }

            totalPreviewTimeMs +=
                    std::chrono::duration <double, std::milli>(std::chrono::steady_clock::now() - previewTimestamp).count();

            unwrapCameraPreviewInputBuffer(inputBuffer);

            cl_context clContext = nullptr;
            cl_command_queue clQueue = nullptr;

            VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

            auto clOutputBuffer = (cl_mem) halide_opencl_get_cl_mem(nullptr, outputBuffer.raw_buffer());
            auto data = CL_enqueueMapBuffer(
                    clQueue, clOutputBuffer, CL_TRUE, CL_MAP_READ, 0, outputBuffer.size_in_bytes(), 0, nullptr, nullptr, &errCode);

            VERIFY_RESULT(errCode, "Failed to map output buffer");

            mPreviewListener->onPreviewGenerated(data, outputBuffer.size_in_bytes(), outputBuffer.width(), outputBuffer.height());

            errCode = CL_enqueueUnmapMemObject(clQueue, clOutputBuffer, data, 0, nullptr, nullptr);
            VERIFY_RESULT(errCode, "Failed to unmap output buffer");

            VERIFY_RESULT(CL_release(), "Failed to release CL context");

            // Return buffer
            onBufferReady(buffer);

            processedFrames += 1;

            auto now = std::chrono::steady_clock::now();
            double durationMs = std::chrono::duration <double, std::milli>(now - fpsTimestamp).count();

            // Print camera FPS + stats
            if(durationMs > 3000.0f) {
                double avgProcessTimeMs = totalPreviewTimeMs / processedFrames;

                LOGI("Camera FPS: %d, cameraQuality=%d processTimeMs=%.2f", processedFrames / 3, downscaleFactor, avgProcessTimeMs);

                processedFrames = 0;
                totalPreviewTimeMs = 0;

                fpsTimestamp = now;
            }
        }

        if(outputCreated)
            releaseCameraPreviewOutputBuffer(outputBuffer);

        while(mPreprocessQueue.try_dequeue(buffer)) {
            RawBufferManager::get().discardBuffer(buffer);
        }
#endif
        LOGD("Exiting preprocess thread");
    }

    void RawImageConsumer::enableRawPreview(std::shared_ptr<RawPreviewListener> listener, const int previewQuality) {
        if(mEnableRawPreview)
            return;

        LOGI("Enabling RAW preview mode");

        mPreviewListener  = std::move(listener);
        mEnableRawPreview = true;
        mRawPreviewQuality = previewQuality;
        mPreprocessThread = std::make_unique<std::thread>(&RawImageConsumer::doPreprocess, this);
        mEstimatedSettings = PostProcessSettings();
    }

    void RawImageConsumer::updateRawPreviewSettings(
            float shadowBoost,
            float contrast,
            float saturation,
            float blacks,
            float whitePoint,
            float tempOffset,
            float tintOffset,
            bool useVideoPreview)
    {
        mShadowBoost = shadowBoost;
        mEstimatedSettings.contrast = contrast;
        mEstimatedSettings.saturation = saturation;
        mEstimatedSettings.blacks = blacks;
        mEstimatedSettings.whitePoint = whitePoint;
        mTempOffset = tempOffset;
        mTintOffset = tintOffset;
        mUseVideoPreview = useVideoPreview;
    }

    void RawImageConsumer::getEstimatedSettings(PostProcessSettings& outSettings) {
        outSettings = mEstimatedSettings;
    }

    void RawImageConsumer::disableRawPreview() {
        if(!mEnableRawPreview)
            return;

        LOGI("Disabling RAW preview mode");

        mEnableRawPreview = false;
        if(mPreprocessThread && mPreprocessThread->joinable())
            mPreprocessThread->join();

        mPreprocessThread = nullptr;
        mPreviewListener = nullptr;
    }

    void RawImageConsumer::setWhiteBalanceOverride(bool override) {
    }

    void RawImageConsumer::setUseVideoPreview(bool useVideoPreview) {
        mUseVideoPreview = useVideoPreview;
    }

    void RawImageConsumer::doCopyImage() {
        while(mRunning) {
            std::shared_ptr<AImage> pendingImage = nullptr;

            // Wait for image
            if(!mImageQueue.wait_dequeue_timed(pendingImage, std::chrono::milliseconds(100))) {
                // Try to match buffers even if no image has been added
                doMatchMetadata();
                continue;
            }

            if(!pendingImage)
                continue;

            if(!mBufferSize) {
                int length = 0;
                uint8_t* data = nullptr;

                // Get size of buffer
                if(AImage_getPlaneData(pendingImage.get(), 0, &data, &length) != AMEDIA_OK) {
                    LOGE("Failed to get size of camera buffer!");
                }

                {
                    std::lock_guard<std::mutex> lock(mBufferMutex);
                    mBufferSize = length;
                }

                mBufferCondition.notify_one();
            }

            std::shared_ptr<RawImageBuffer> dst = RawBufferManager::get().dequeueUnusedBuffer();

            // If there are no buffers available, we can't do anything useful here
            if(!dst) {
                continue;
            }

            // Reset buffer
            dst->width     = 0;
            dst->height    = 0;
            dst->rowStride = 0;
            dst->metadata.timestampNs = 0;
            dst->isCompressed = false;

            //
            // Copy buffer
            //

            int32_t format      = 0;
            int32_t width       = 0;
            int32_t height      = 0;
            int32_t rowStride   = 0;
            int64_t timestamp   = 0;
            uint8_t* data       = nullptr;
            int length          = 0;
            bool result         = true;

            // Copy frame metadata
            const AImage* image = pendingImage.get();

            result &= (AImage_getFormat(image, &format)                 == AMEDIA_OK);
            result &= (AImage_getWidth(image, &width)                   == AMEDIA_OK);
            result &= (AImage_getHeight(image, &height)                 == AMEDIA_OK);
            result &= (AImage_getPlaneRowStride(image, 0, &rowStride)   == AMEDIA_OK);
            result &= (AImage_getTimestamp(image, &timestamp)           == AMEDIA_OK);
            result &= (AImage_getPlaneData(image, 0, &data, &length)    == AMEDIA_OK);

            // Copy raw data if were able to acquire it successfully
            if(result) {
                switch(format) {
                    default:
                    case AIMAGE_FORMAT_RAW10:
                        dst->pixelFormat = PixelFormat::RAW10;
                        break;

                    case AIMAGE_FORMAT_RAW12:
                        dst->pixelFormat = PixelFormat::RAW12;
                        break;

                    case AIMAGE_FORMAT_RAW16:
                        dst->pixelFormat = PixelFormat::RAW16;
                        break;

                    case AIMAGE_FORMAT_YUV_420_888:
                        dst->pixelFormat = PixelFormat::YUV_420_888;
                        break;

                    case AIMAGE_FORMAT_RAW_PRIVATE:
                        dst->pixelFormat = PixelFormat::RAW10;
                        break;
                }

                dst->width                  = width;
                dst->height                 = height;
                dst->originalWidth          = width;
                dst->originalHeight         = height;
                dst->isBinned               = false;
                dst->rowStride              = rowStride;
                dst->metadata.timestampNs   = timestamp;

                if(dst->data->len() != length) {
                    LOGE("Unexpected buffer size!!");
                }
                else {
                    auto dstBuffer = dst->data->lock(true);

                    if(dstBuffer)
                        std::copy(data, data + length, dstBuffer);

                    dst->data->unlock();
                    dst->data->setValidRange(0, length);
                }
            }
            else {
                LOGW("Failed to copy image!");
            }

            // Insert back
            if(!result) {
                LOGW("Got error, discarding buffer");
                RawBufferManager::get().discardBuffer(dst);
            }
            else {
                auto imageIt = mPendingBuffers.find(timestamp);

                if (imageIt != mPendingBuffers.end()) {
                    LOGW("Pending timestamp already exists!");

                    RawBufferManager::get().discardBuffer(imageIt->second);
                    mPendingBuffers.erase(imageIt);
                }

                mPendingBuffers.insert(std::make_pair(timestamp, dst));
            }

            // Match buffers
            doMatchMetadata();
        }

        // Clear pending buffers
        std::shared_ptr<AImage> pendingImage = nullptr;

        while(mImageQueue.try_dequeue(pendingImage)) {
        }

        // Stop setup buffers thread and return all pending buffers
        std::shared_ptr<std::thread> bufferThread;

        // Return all pending buffers
        auto it = mPendingBuffers.begin();
        while(it != mPendingBuffers.end()) {
            RawBufferManager::get().discardBuffer(it->second);
            ++it;
        }

        mPendingBuffers.clear();

        LOGD("Exiting copy thread");
    }
}
