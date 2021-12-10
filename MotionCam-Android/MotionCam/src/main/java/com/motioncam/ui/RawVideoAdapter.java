package com.motioncam.ui;

import android.content.Context;
import android.net.Uri;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.documentfile.provider.DocumentFile;
import androidx.recyclerview.widget.RecyclerView;

import com.motioncam.R;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.processor.ContainerMetadata;

import java.io.File;
import java.text.Format;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class RawVideoAdapter extends RecyclerView.Adapter<RawVideoAdapter.ViewHolder> implements
        AsyncNativeCameraOps.ContainerListener
{
    static private final Format FORMATTER = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
    private final Context mContext;

    interface OnQueueListener {
        void onQueueClicked(Uri uri);
        void onDeleteClicked(Uri uri);
    }

    static class Item {
        Uri uri;
        int numFrames;
        float frameRate;
        boolean haveMetadata;
        int progress;
        boolean isQueued;

        Item(Uri uri) {
            this.uri = uri;
            this.numFrames = 0;
            this.frameRate = 0;
            this.haveMetadata = false;
            this.progress = -1;
        }
    }

    private final List<Item> mItems;
    private final OnQueueListener mListener;
    private final AsyncNativeCameraOps mNativeOps;

    public static class ViewHolder extends RecyclerView.ViewHolder {
        private final TextView fileNameView;
        private final TextView captureTime;
        private final TextView frameRate;
        private final TextView totalFrames;
        private final Button queueVideoBtn;
        private final ImageView deleteVideoBtn;
        private final ProgressBar progressBar;

        public ViewHolder(View view) {
            super(view);

            fileNameView = view.findViewById(R.id.filename);
            queueVideoBtn = view.findViewById(R.id.queueVideo);
            deleteVideoBtn = view.findViewById(R.id.deleteVideo);
            captureTime = view.findViewById(R.id.captureTime);
            frameRate = view.findViewById(R.id.frameRate);
            totalFrames = view.findViewById(R.id.numFrames);
            progressBar = view.findViewById(R.id.progressBar);
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

        public ImageView getDeleteVideoBtn() {
            return deleteVideoBtn;
        }

        public TextView getFrameRate() {
            return frameRate;
        }

        public TextView getNumFrames() {
            return totalFrames;
        }

        public ProgressBar getProgressBar() {
            return progressBar;
        }
    }

    public RawVideoAdapter(Context context, Uri[] uris, OnQueueListener listener) {
        mItems = new ArrayList<>();

        for(Uri uri : uris) {
            mItems.add(new Item(uri));
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

        String name;
        long modifiedTime;

        if(item.uri.getScheme().equalsIgnoreCase("file")) {
            File file = new File(item.uri.getPath());

            name = file.getName();
            modifiedTime = file.lastModified();
        }
        else {
            DocumentFile documentFile = DocumentFile.fromSingleUri(mContext, item.uri);

            name = documentFile.getName();
            modifiedTime = documentFile.lastModified();
        }

        viewHolder.getFileNameView().setText(name);

        // Buttons
        Date createdDate = new Date(modifiedTime);

        viewHolder.getCaptureTime().setText(FORMATTER.format(createdDate));
        viewHolder.getQueueVideoBtn().setOnClickListener((v) ->
                mListener.onQueueClicked(item.uri));

        viewHolder.getDeleteVideoBtn().setOnClickListener((v) -> mListener.onDeleteClicked(item.uri));

        // Metadata
        viewHolder.getFrameRate().setText(String.valueOf(Math.round(item.frameRate)));
        viewHolder.getNumFrames().setText(String.valueOf(item.numFrames));

        // Set up buttons
        if(item.isQueued) {
            viewHolder.getQueueVideoBtn().setText(R.string.queued);

            viewHolder.getQueueVideoBtn().setEnabled(false);
            viewHolder.getDeleteVideoBtn().setEnabled(false);
        }
        else {
            viewHolder.getQueueVideoBtn().setText(R.string.convert_to_dng);

            viewHolder.getQueueVideoBtn().setEnabled(true);
            viewHolder.getDeleteVideoBtn().setEnabled(true);
        }

        // Update progress
        if(item.progress >= 0) {
            viewHolder.getProgressBar().setVisibility(View.VISIBLE);
            viewHolder.getProgressBar().setProgress(item.progress);
        }
        else {
            viewHolder.getProgressBar().setVisibility(View.INVISIBLE);
        }

        // Check for corrupted video
        if(item.frameRate < 0 || item.numFrames < 0) {
            viewHolder.getFileNameView().setText(R.string.corrupted_video);
            viewHolder.getQueueVideoBtn().setEnabled(false);
        }
        else {
            viewHolder.getQueueVideoBtn().setEnabled(true);
        }

        if(!item.haveMetadata) {
            mNativeOps.getContainerMetadata(mContext, item.uri, this);
        }
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public long getItemId(int position) {
        return mItems.get(position).uri.hashCode();
    }

    public int remove(Uri uri) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if (item.uri.equals(uri)) {
                mItems.remove(i);
                notifyItemRemoved(i);
                break;
            }
        }

        return mItems.size();
    }

    public void update(Uri inputUri, boolean isQueued, int progress) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if(item.uri.equals(inputUri)) {
                item.isQueued = isQueued;
                item.progress = progress;
                notifyItemChanged(i);
                break;
            }
        }
    }

    @Override
    public void onContainerMetadataAvailable(Uri inputUri, ContainerMetadata metadata) {
        for(int i = 0; i < mItems.size(); i++) {
            Item item = mItems.get(i);

            if(item.uri.equals(inputUri)) {
                item.frameRate = metadata.frameRate;
                item.numFrames = metadata.numFrames;
                item.haveMetadata = true;

                notifyItemChanged(i);
                break;
            }
        }
    }
}
