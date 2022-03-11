#ifndef RawImageConsumer_hpp
#define RawImageConsumer_hpp

#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <chrono>

#include <motioncam/RawImageMetadata.h>

#ifdef GPU_CAMERA_PREVIEW
    #include <HalideBuffer.h>
#endif

#include <queue/blockingconcurrentqueue.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>

namespace motioncam {
    // Forward declarations
    class RawPreviewListener;
    class CameraSessionListener;

    struct CameraDescription;
    struct RawImageMetadata;
    struct RawImageBuffer;
    struct PostProcessSettings;

    class RawImageConsumer {
    public:
        RawImageConsumer(std::shared_ptr<CameraDescription> cameraDescription,
                         std::shared_ptr<CameraSessionListener> listener,
                         const size_t maxMemoryUsageBytes);
        ~RawImageConsumer();

        void start();
        void stop();

        void grow(size_t memoryLimitBytes);

        void queueImage(AImage* image);
        void queueMetadata(const ACameraMetadata* metadata, ScreenOrientation screenOrientation, RawType rawType);

        void enableRawPreview(std::shared_ptr<RawPreviewListener> listener, const int previewQuality);
        void disableRawPreview();

        void getEstimatedSettings(PostProcessSettings& outSettings);

    private:
        bool copyMetadata(RawImageMetadata& dst, const ACameraMetadata* src);
        void onBufferReady(const std::shared_ptr<RawImageBuffer>& buffer);

        void doSetupBuffers(const size_t bufferSize);
        void doCopyImage();
        void doMatchMetadata();
        void doPreprocess();

#ifdef GPU_CAMERA_PREVIEW
        static Halide::Runtime::Buffer<uint8_t> createCameraPreviewOutputBuffer(const RawImageBuffer& buffer, const int downscaleFactor);
        static void releaseCameraPreviewOutputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer);

        static Halide::Runtime::Buffer<uint8_t> wrapCameraPreviewInputBuffer(const RawImageBuffer& buffer);
        static void unwrapCameraPreviewInputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer);
#endif

    private:
        std::shared_ptr<CameraSessionListener> mListener;
        int mMaximumMemoryUsageBytes;
        std::unique_ptr<std::thread> mConsumerThread;
        std::unique_ptr<std::thread> mPreprocessThread;
        std::atomic<bool> mRunning;
        std::atomic<bool> mEnableRawPreview;

        std::atomic<bool> mRequestSetupBuffers;

        PostProcessSettings mEstimatedSettings;

        std::shared_ptr<CameraDescription> mCameraDesc;
        int mRawPreviewQuality;
        bool mCopyCaptureColorTransform;
        int mFramesSinceEstimatedSettings;

        moodycamel::BlockingConcurrentQueue<std::shared_ptr<AImage>> mImageQueue;
        moodycamel::ConcurrentQueue<RawImageMetadata> mPendingMetadata;
        moodycamel::BlockingConcurrentQueue<std::shared_ptr<RawImageBuffer>> mPreprocessQueue;

        std::map<int64_t, std::shared_ptr<RawImageBuffer>> mPendingBuffers;

        std::shared_ptr<RawPreviewListener> mPreviewListener;
    };
}

#endif /* RawImageConsumer_hpp */
