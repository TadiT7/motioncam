package com.motioncam.camera;

import android.content.ContentResolver;
import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.util.Pair;
import android.util.Size;

import com.motioncam.CameraActivity;
import com.motioncam.processor.ContainerMetadata;
import com.motioncam.processor.NativeProcessor;
import com.motioncam.ui.VideoEntry;

import java.io.Closeable;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

public class AsyncNativeCameraOps implements Closeable {
    public enum PreviewSize {
        SMALL(8),
        MEDIUM(4),
        LARGE(2);

        private final int scale;

        PreviewSize(int scale) {
            this.scale = scale;
        }
    }

    private final ThreadPoolExecutor mBackgroundProcessor = new ThreadPoolExecutor( 1, 1, 0L, TimeUnit.MILLISECONDS, new LinkedBlockingQueue<>() );
    private final NativeCameraSessionBridge mCameraSessionBridge;
    private final Handler mMainHandler;
    private Size mUnscaledSize;

    public interface PreviewListener {
        void onPreviewAvailable(NativeCameraBuffer buffer, Bitmap image);
    }

    public interface ContainerListener {
        void onContainerMetadataAvailable(String name, ContainerMetadata metadata);
        void onContainerGeneratedPreviews(String name, List<Bitmap> previewImages);
    }

    public interface PostProcessSettingsListener {
        void onSettingsEstimated(PostProcessSettings settings);
    }

    public interface CaptureImageListener {
        void onCaptured(long handle);
    }

    public interface SharpnessMeasuredListener {
        void onSharpnessMeasured(List<Pair<NativeCameraBuffer, Double>> sharpnessList);
    }

    public interface StatsListener {
        void onExposureMap(Bitmap bitmap);
    }

    public AsyncNativeCameraOps(NativeCameraSessionBridge cameraSessionBridge) {
        mCameraSessionBridge = cameraSessionBridge;
        mMainHandler = new Handler(Looper.getMainLooper());
    }

    public AsyncNativeCameraOps() {
        mMainHandler = new Handler(Looper.getMainLooper());
        mCameraSessionBridge = null;
    }

    @Override
    public void close() {
        mBackgroundProcessor.shutdown();

        try {
            if(!mBackgroundProcessor.awaitTermination(500, TimeUnit.MILLISECONDS)) {
                mBackgroundProcessor.shutdownNow();
            }
        }
        catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public void captureImage(long bufferHandle, int numSaveImages, PostProcessSettings settings, String outputPath, CaptureImageListener listener) {
        if(mCameraSessionBridge == null)
            throw new RuntimeException("No camera bridge");

        mBackgroundProcessor.submit(() -> {
            mCameraSessionBridge.captureImage(bufferHandle, numSaveImages, settings, outputPath);
            mMainHandler.post(() -> listener.onCaptured(bufferHandle));
        });
    }

    public void estimateSettings(float shadowsBias, PostProcessSettingsListener listener) {
        if(mCameraSessionBridge == null)
            throw new RuntimeException("No camera bridge");

        mBackgroundProcessor.submit(() -> {
            try {
                PostProcessSettings result = mCameraSessionBridge.estimatePostProcessSettings(shadowsBias);
                mMainHandler.post(() -> listener.onSettingsEstimated(result));

            }
            catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    public void measureSharpness(List<NativeCameraBuffer> buffers, SharpnessMeasuredListener listener) {
        if(buffers.isEmpty())
            return;

        if(mCameraSessionBridge == null)
            throw new RuntimeException("No camera bridge");

        mBackgroundProcessor.submit(() -> {
            List<Pair<NativeCameraBuffer, Double>> result = new ArrayList<>();

            for(NativeCameraBuffer buffer : buffers) {
                double sharpness = mCameraSessionBridge.measureSharpness(buffer.timestamp);
                result.add(new Pair<>(buffer, sharpness));
            }

            result.sort(Comparator.comparing(l -> l.second));

            mMainHandler.post(() -> listener.onSharpnessMeasured(result));
        });
    }

    public Size getPreviewSize(PreviewSize generateSize, NativeCameraBuffer buffer) {
        if (mUnscaledSize == null)
            mUnscaledSize = mCameraSessionBridge.getPreviewSize(1);

        if(mUnscaledSize == null)
            return new Size(0, 0);

        int width = mUnscaledSize.getWidth() / generateSize.scale;
        int height = mUnscaledSize.getHeight() / generateSize.scale;

        if( buffer.screenOrientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT ||
            buffer.screenOrientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {

            int temp = width;

            width = height;
            height = temp;

        }

        return new Size(width, height);
    }

    public void generatePreview(NativeCameraBuffer buffer,
                                PostProcessSettings settings,
                                PreviewSize generateSize,
                                Bitmap useBitmap,
                                PreviewListener listener,
                                boolean canSkip)
    {
        if(canSkip && mBackgroundProcessor.getQueue().size() != 0)
            return;

        if(mCameraSessionBridge == null)
            throw new RuntimeException("No camera bridge");

        PostProcessSettings postProcessSettings = settings.clone();

        mBackgroundProcessor.submit(() -> {
            Bitmap preview = useBitmap;
            Size size = getPreviewSize(generateSize, buffer);

            // Create bitmap if the provided one was incompatible (or null)
            if( preview == null ||
                preview.getWidth() != size.getWidth() ||
                preview.getHeight() != size.getHeight() )
            {
                preview = Bitmap.createBitmap(size.getWidth(), size.getHeight(), Bitmap.Config.ARGB_8888);
            }

            mCameraSessionBridge.createPreviewImage(
                    buffer.timestamp,
                    postProcessSettings,
                    generateSize.scale,
                    preview);

            final Bitmap resultBitmap = preview;

            // On the main thread, let listeners know that an image is ready
            mMainHandler.post(() -> listener.onPreviewAvailable(buffer, resultBitmap));
        });
    }

    public void getContainerMetadata(Context context, VideoEntry entry, ContainerListener listener) {
        Objects.requireNonNull(context);
        Objects.requireNonNull(entry);
        Objects.requireNonNull(listener);

        final ContentResolver resolver = context.getContentResolver();

        VideoEntry entryClone = entry.clone();

        mBackgroundProcessor.submit(() -> {
            NativeProcessor processor = new NativeProcessor();

            List<Integer> fds = new ArrayList<>();

            for(Uri uri : entryClone.getVideoUris()) {
                int fd = -1;

                try(ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "r", null)) {
                    if (pfd != null)
                        fd = pfd.detachFd();
                }
                catch (IOException e) {
                    e.printStackTrace();
                }

                if(fd < 0) {
                    Log.e(CameraActivity.TAG, "Failed to open " + uri);
                }
                else {
                    fds.add(fd);
                }
            }

            final int[] finalFds = fds.stream().mapToInt(i -> i).toArray();

            mMainHandler.post(() -> listener.onContainerMetadataAvailable(entry.getName(), processor.getRawVideoMetadata(finalFds)));
        });
    }

    public void generateVideoPreview(Context context, VideoEntry entry, int numPreviews, ContainerListener listener) {
        Objects.requireNonNull(context);
        Objects.requireNonNull(entry);
        Objects.requireNonNull(listener);

        final ContentResolver resolver = context.getContentResolver();
        VideoEntry entryClone = entry.clone();

        mBackgroundProcessor.submit(() -> {
            if(entryClone.getVideoUris().isEmpty()) {
                listener.onContainerGeneratedPreviews(entryClone.getName(), new ArrayList<>());
                return;
            }

            NativeProcessor processor = new NativeProcessor();

            int fd = -1;

            // Take first video
            final Uri uri = entryClone.getVideoUris().iterator().next();

            try(ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "r", null)) {
                if (pfd != null)
                    fd = pfd.detachFd();
            }
            catch (IOException e) {
                e.printStackTrace();
            }

            if(fd < 0)
                return;

            final int finalFd = fd;

            List<Bitmap> bitmaps = new ArrayList<>();

            processor.generateRawVideoPreview(finalFd, numPreviews, (width, height, type) -> {
                Bitmap preview = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
                bitmaps.add(preview);

                return preview;
            });

            mMainHandler.post(() -> listener.onContainerGeneratedPreviews(entry.getName(), bitmaps));
        });
    }
}
