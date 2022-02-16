package com.motioncam.ui;

import android.content.Context;
import android.graphics.Bitmap;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.motioncam.R;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.processor.ContainerMetadata;

import java.text.Format;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class RawVideoAdapter extends RecyclerView.Adapter<RawVideoAdapter.ViewHolder> implements
        AsyncNativeCameraOps.ContainerListener
{
    static private final Format FORMATTER = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
    private final Context mContext;

    interface OnQueueListener {
        void onQueueClicked(VideoEntry entry);
        void onMoveClicked(VideoEntry entry);
        void onDeleteClicked(VideoEntry entry);
    }

    static class Item {
        VideoEntry entry;
        int progress;
        boolean isQueued;
        boolean queriedMetadata;
        List<Bitmap> previewImages;

        Item(VideoEntry entry) {
            this.entry = entry;
            this.progress = -1;
            this.queriedMetadata = false;
        }
    }

    private final List<Item> mItems;
    private final OnQueueListener mListener;
    private final AsyncNativeCameraOps mNativeOps;

    public static class ViewHolder extends RecyclerView.ViewHolder {
        private final View background;
        private final TextView fileNameView;
        private final TextView captureTime;
        private final TextView frameRate;
        private final TextView totalFrames;
        private final TextView numParts;
        private final Button queueVideoBtn;
        private final Button moveVideoBtn;
        private final ImageView deleteVideoBtn;
        private final ProgressBar progressBar;
        private final ViewGroup previewList;
        private final TextView statusText;

        public ViewHolder(View view) {
            super(view);

            background = view;
            fileNameView = view.findViewById(R.id.filename);
            queueVideoBtn = view.findViewById(R.id.queueVideo);
            moveVideoBtn = view.findViewById(R.id.moveVideo);
            deleteVideoBtn = view.findViewById(R.id.deleteVideo);
            captureTime = view.findViewById(R.id.captureTime);
            frameRate = view.findViewById(R.id.frameRate);
            totalFrames = view.findViewById(R.id.numFrames);
            numParts = view.findViewById(R.id.numParts);
            progressBar = view.findViewById(R.id.progressBar);
            previewList = view.findViewById(R.id.previewList);
            statusText = view.findViewById(R.id.videoStatusText);
        }

        public View getBackground() {
            return background;
        }

        public TextView getFileNameView() {
            return fileNameView;
        }

        public TextView getCaptureTime() {
            return captureTime;
        }

        public Button getQueueVideoBtn() {
            return queueVideoBtn;
        }

        public Button getMoveVideoBtn() {
            return moveVideoBtn;
        }

        public ImageView getDeleteVideoBtn() {
            return deleteVideoBtn;
        }

        public TextView getFrameRate() {
            return frameRate;
        }

        public TextView getNumFrames() {
            return totalFrames;
        }

        public TextView getNumParts() {
            return numParts;
        }

        public ProgressBar getProgressBar() {
            return progressBar;
        }

        public ViewGroup getPreviewList() {
            return previewList;
        }

        public TextView getStatusText() {
            return statusText;
        }
    }

    public RawVideoAdapter(Context context, Collection<VideoEntry> entries, OnQueueListener listener) {
        mItems = new ArrayList<>();

        for(VideoEntry entry : entries) {
            mItems.add(new Item(entry));
        }

        mListener = listener;
        mNativeOps = new AsyncNativeCameraOps();
        mContext = context;

        setHasStableIds(true);
    }

    @Override
    @NonNull
    public ViewHolder onCreateViewHolder(ViewGroup viewGroup, int viewType) {
        View view = LayoutInflater.from(viewGroup.getContext())
                .inflate(R.layout.video_file_entry, viewGroup, false);

        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(ViewHolder viewHolder, final int position) {
        Item item = mItems.get(position);

        viewHolder.getFileNameView().setText(item.entry.getName());

        // Buttons
        Date createdDate = new Date(item.entry.getCreatedAt());

        viewHolder.getCaptureTime().setText(FORMATTER.format(createdDate));
        viewHolder.getQueueVideoBtn().setOnClickListener((v) ->
                mListener.onQueueClicked(item.entry));

        viewHolder.getMoveVideoBtn().setOnClickListener((v) ->
                mListener.onMoveClicked(item.entry));

        viewHolder.getDeleteVideoBtn().setOnClickListener((v) -> mListener.onDeleteClicked(item.entry));

        // Metadata
        String numPartsText = "-";
        String frameRateText = "-";
        String numFramesText = "-";

        // Part
        if(item.entry.getNumParts() > 0) {
            numPartsText = item.entry.getVideoUris().size() + "/" + item.entry.getNumParts();
        }

        // FPS
        if(item.entry.getFrameRate() > 0)
            frameRateText = String.valueOf(Math.round(item.entry.getFrameRate()));

        // Num frames
        if(item.entry.getNumFrames() > 0) {
            numFramesText = String.valueOf(item.entry.getNumFrames());
        }

        viewHolder.getFrameRate().setText(frameRateText);
        viewHolder.getNumParts().setText(numPartsText);
        viewHolder.getNumFrames().setText(numFramesText);

        String statusText = "";

        // Exported text first
        if(item.entry.isAlreadyExported()) {
            statusText = mContext.getString(R.string.this_video_has_been_exported);
        }

        // In the process of being exported?
        if(item.isQueued) {
            viewHolder.getQueueVideoBtn().setText(R.string.queued);

            viewHolder.getQueueVideoBtn().setEnabled(false);
            viewHolder.getDeleteVideoBtn().setEnabled(false);
            viewHolder.getMoveVideoBtn().setVisibility(View.GONE);
        }
        // Parts missing?
        else if(item.entry.getNumParts() > 0 && item.entry.getVideoUris().size() != item.entry.getNumParts()) {
            viewHolder.getQueueVideoBtn().setVisibility(View.INVISIBLE);
            viewHolder.getDeleteVideoBtn().setEnabled(true);

            statusText = mContext.getString(R.string.video_parts_missing);
        }
        // Waiting for metadata?
        else if(item.entry.getMetadata() == null) {
            viewHolder.getQueueVideoBtn().setVisibility(View.INVISIBLE);
            viewHolder.getDeleteVideoBtn().setEnabled(false);
        }
        // Good to go
        else {
            viewHolder.getQueueVideoBtn().setVisibility(View.VISIBLE);
            viewHolder.getQueueVideoBtn().setText(R.string.convert_to_dng);
            viewHolder.getDeleteVideoBtn().setEnabled(true);

            // Corrupted? Show warning.
            if(item.entry.getFrameRate() <= 0 || item.entry.getNumFrames() <= 0)
                statusText = mContext.getString(R.string.corrupted_video_warning);

            if(item.entry.isInternal()) {
                viewHolder.getMoveVideoBtn().setVisibility(View.VISIBLE);
            }
            else {
                viewHolder.getMoveVideoBtn().setVisibility(View.GONE);
            }
        }

        // Update progress
        if(item.progress >= 0) {
            viewHolder.getProgressBar().setVisibility(View.VISIBLE);
            viewHolder.getProgressBar().setProgress(item.progress);
        }
        else {
            viewHolder.getProgressBar().setVisibility(View.GONE);
        }

        // Get all metadata
        if(!item.queriedMetadata) {
            mNativeOps.getContainerMetadata(mContext, item.entry, this);
            mNativeOps.generateVideoPreview(mContext, item.entry, 6, this);

            item.queriedMetadata = true;
        }

        ViewGroup previewList = viewHolder.getPreviewList();
        previewList.removeAllViews();

        if(item.previewImages != null) {
            int pixels = (int) TypedValue.applyDimension(
                    TypedValue.COMPLEX_UNIT_DIP, 60, mContext.getResources().getDisplayMetrics() );

            for(Bitmap bitmap : item.previewImages) {
                ImageView imageView = new ImageView(mContext);

                imageView.setImageBitmap(bitmap);
                imageView.setScaleType(ImageView.ScaleType.FIT_CENTER);
                imageView.setLayoutParams(new ViewGroup.LayoutParams(pixels, pixels));

                previewList.addView(imageView);
            }

        }

        viewHolder.getStatusText().setText(statusText);
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public long getItemId(int position) {
        return mItems.get(position).entry.getName().hashCode();
    }

    public int remove(String name) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if (item.entry.getName().equals(name)) {
                mItems.remove(i);
                notifyItemRemoved(i);
                break;
            }
        }

        return mItems.size();
    }

    public boolean isValid(String name) {
        for(Item item : mItems) {
            if(item.entry.getName().equals(name))
                return true;
        }

        return false;
    }

    public VideoEntry getItemFromName(String name) {
        for(Item item : mItems) {
            if(item.entry.getName().equals(name))
                return item.entry;
        }

        return null;
    }

    public void update(String name, Boolean optionalIsQueued, Boolean optionalIsExported, int progress) {
        if(name == null)
            return;

        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            boolean match = item.entry.getName().equals(name);

            if(match) {
                if(optionalIsQueued != null)
                    item.isQueued = optionalIsQueued;

                if(optionalIsExported != null)
                    item.entry.setAlreadyExported(optionalIsExported);

                item.progress = progress;

                notifyItemChanged(i);
                break;
            }
        }
    }

    @Override
    public void onContainerMetadataAvailable(String name, ContainerMetadata metadata) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if(item.entry.getName().equals(name)) {
                item.entry.setMetadata(metadata);
                notifyItemChanged(i);
                break;
            }
        }
    }

    @Override
    public void onContainerGeneratedPreviews(String name, List<Bitmap> previewImages) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if(item.entry.getName().equals(name)) {
                item.previewImages = previewImages;
                notifyItemChanged(i);
                break;
            }
        }
    }
}
