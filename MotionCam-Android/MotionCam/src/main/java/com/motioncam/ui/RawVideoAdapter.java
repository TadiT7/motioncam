package com.motioncam.ui;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

import com.motioncam.R;

import java.io.File;
import java.text.Format;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

public class RawVideoAdapter extends RecyclerView.Adapter<RawVideoAdapter.ViewHolder> {
    static private final Format FORMATTER = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);

    interface OnQueueListener {
        void onQueueClicked(View queueBtn, View deleteBtn, File file);
        void onDeleteClicked(View view, File file);
    }

    private File[] mFiles;
    private final OnQueueListener mListener;

    public static class ViewHolder extends RecyclerView.ViewHolder {
        private final TextView fileNameView;
        private final TextView captureTime;
        private final Button queueVideoBtn;
        private final ImageView deleteVideoBtn;

        public ViewHolder(View view) {
            super(view);

            fileNameView = view.findViewById(R.id.filename);
            queueVideoBtn = view.findViewById(R.id.queueVideo);
            deleteVideoBtn = view.findViewById(R.id.deleteVideo);
            captureTime = view.findViewById(R.id.capture_time);
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
    }

    public RawVideoAdapter(File[] files, OnQueueListener listener) {
        mFiles = files;
        mListener = listener;

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
        viewHolder.getFileNameView().setText(mFiles[position].getName());

        Date createdDate = new Date(mFiles[position].lastModified());

        viewHolder.getCaptureTime().setText(FORMATTER.format(createdDate));

        viewHolder.getQueueVideoBtn().setOnClickListener((v) ->
                mListener.onQueueClicked(viewHolder.getQueueVideoBtn(), viewHolder.getDeleteVideoBtn(), mFiles[position]));

        viewHolder.getDeleteVideoBtn().setOnClickListener((v) -> mListener.onDeleteClicked(v, mFiles[position]));
    }

    @Override
    public int getItemCount() {
        return mFiles.length;
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    public int remove(File file) {
        mFiles = Arrays.stream(mFiles)
                .filter(f -> !f.getName().equals(file.getName()))
                .toArray(File[]::new);

        notifyDataSetChanged();

        return mFiles.length;
    }
}
