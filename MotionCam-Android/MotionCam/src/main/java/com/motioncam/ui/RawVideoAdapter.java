package com.motioncam.ui;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;

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
import java.util.stream.Collectors;

public class RawVideoAdapter extends RecyclerView.Adapter<RawVideoAdapter.ViewHolder> implements AsyncNativeCameraOps.ContainerListener {
    static private final Format FORMATTER = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);

    interface OnQueueListener {
        void onQueueClicked(View queueBtn, View deleteBtn, File file);
        void onDeleteClicked(View view, File file);
    }

    static class Item {
        File file;
        int numFrames;
        float frameRate;
        boolean haveMetadata;

        Item(File file) {
            this.file = file;
            this.numFrames = 0;
            this.frameRate = 0;
            this.haveMetadata = false;
        }
    }

    private List<Item> mItems;
    private final OnQueueListener mListener;
    private final AsyncNativeCameraOps mNativeOps;

    public static class ViewHolder extends RecyclerView.ViewHolder {
        private final TextView fileNameView;
        private final TextView captureTime;
        private final TextView frameRate;
        private final TextView totalFrames;
        private final Button queueVideoBtn;
        private final ImageView deleteVideoBtn;

        public ViewHolder(View view) {
            super(view);

            fileNameView = view.findViewById(R.id.filename);
            queueVideoBtn = view.findViewById(R.id.queueVideo);
            deleteVideoBtn = view.findViewById(R.id.deleteVideo);
            captureTime = view.findViewById(R.id.captureTime);
            frameRate = view.findViewById(R.id.frameRate);
            totalFrames = view.findViewById(R.id.numFrames);
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
    }

    public RawVideoAdapter(File[] files, OnQueueListener listener) {
        mItems = new ArrayList<>();

        for(File file : files) {
            mItems.add(new Item(file));
        }

        mListener = listener;
        mNativeOps = new AsyncNativeCameraOps();

        setHasStableIds(false);
    }

    @Override
    public ViewHolder onCreateViewHolder(ViewGroup viewGroup, int viewType) {
        View view = LayoutInflater.from(viewGroup.getContext())
                .inflate(R.layout.video_file_entry, viewGroup, false);

        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(ViewHolder viewHolder, final int position) {
        Item item = mItems.get(position);

        viewHolder.getFileNameView().setText(item.file.getName());

        Date createdDate = new Date(item.file.lastModified());

        viewHolder.getCaptureTime().setText(FORMATTER.format(createdDate));
        viewHolder.getQueueVideoBtn().setOnClickListener((v) ->
                mListener.onQueueClicked(viewHolder.getQueueVideoBtn(), viewHolder.getDeleteVideoBtn(), item.file));

        viewHolder.getDeleteVideoBtn().setOnClickListener((v) -> mListener.onDeleteClicked(v, item.file));

        viewHolder.getFrameRate().setText(String.valueOf(Math.round(item.frameRate)));
        viewHolder.getNumFrames().setText(String.valueOf(item.numFrames));

        if(!item.haveMetadata) {
            mNativeOps.getContainerMetadata(item.file.getPath(), this);
        }
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    public int remove(File file) {
        mItems =
            mItems
                .stream()
                .filter(f -> !f.file.getName().equals(file.getName()))
                .collect(Collectors.toList());

        notifyDataSetChanged();

        return mItems.size();
    }

    @Override
    public void onContainerMetadataAvailable(String inputPath, ContainerMetadata metadata) {
        for(int i = 0; i < mItems.size(); i++) {
            if(mItems.get(i).file.getPath().equals(inputPath)) {
                mItems.get(i).frameRate = metadata.frameRate;
                mItems.get(i).numFrames = metadata.numFrames;
                mItems.get(i).haveMetadata = true;

                notifyItemChanged(i);
                break;
            }
        }
    }
}
