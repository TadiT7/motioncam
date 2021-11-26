package com.motioncam.ui;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

import com.motioncam.R;

import java.io.File;

public class RawVideoAdapter extends RecyclerView.Adapter<RawVideoAdapter.ViewHolder> {
    interface OnQueueListener {
        void onQueueClicked(View view, File file);
    }

    private final File[] mFiles;
    private final OnQueueListener mListener;

    public static class ViewHolder extends RecyclerView.ViewHolder {
        private final TextView fileNameView;
        private final Button queueVideoBtn;

        public ViewHolder(View view) {
            super(view);

            fileNameView = view.findViewById(R.id.filename);
            queueVideoBtn = view.findViewById(R.id.queueVideo);
        }

        public TextView getFileNameView() {
            return fileNameView;
        }

        public Button getQueueVideoBtn() {
            return queueVideoBtn;
        }
    }

    public RawVideoAdapter(File[] files, OnQueueListener listener) {
        mFiles = files;
        mListener = listener;

        setHasStableIds(true);
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
        viewHolder.getQueueVideoBtn().setOnClickListener((v) -> {
            mListener.onQueueClicked(v, mFiles[position]);
        });
    }

    @Override
    public int getItemCount() {
        return mFiles.length;
    }

    @Override
    public long getItemId(int position) {
        return position;
    }
}
