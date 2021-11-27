package com.motioncam.ui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.LifecycleObserver;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.work.Data;
import androidx.work.ExistingWorkPolicy;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkManager;

import com.motioncam.CameraActivity;
import com.motioncam.R;
import com.motioncam.worker.VideoProcessWorker;

import java.io.File;

public class ConvertVideoFragment  extends Fragment implements LifecycleObserver, RawVideoAdapter.OnQueueListener {
    private File[] mVideoFiles;
    private RecyclerView mFileList;
    private RawVideoAdapter mAdapter;
    private ActivityResultLauncher<Intent> mSelectDocumentLauncher;
    private File mSelectedFile;

    public static ConvertVideoFragment newInstance() {
        return new ConvertVideoFragment();
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {

        return inflater.inflate(R.layout.convert_video_fragment, container, false);
    }

    @Override
    public void onAttach(@NonNull Context context) {
        super.onAttach(context);

        mSelectDocumentLauncher = registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(),
                result -> {
                    if (result.getResultCode() == Activity.RESULT_OK) {
                        Intent data = result.getData();
                        if(data != null && data.getData() != null)
                            startWorker(mSelectedFile, data.getData());

                        mSelectedFile = null;
                    }
                });
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        mFileList = view.findViewById(R.id.fileList);

        File outputDirectory = new File(getContext().getFilesDir(), VideoProcessWorker.VIDEOS_PATH);
        View noFiles = view.findViewById(R.id.noFiles);

        mVideoFiles = outputDirectory.listFiles();
        if(mVideoFiles == null)
            mVideoFiles = new File[0];

        if(mVideoFiles.length == 0) {
            noFiles.setVisibility(View.VISIBLE);
            mFileList.setVisibility(View.GONE);
        }
        else {
            mFileList.setVisibility(View.VISIBLE);
            noFiles.setVisibility(View.GONE);

            mAdapter = new RawVideoAdapter(mVideoFiles, this);

            mFileList.setLayoutManager(new LinearLayoutManager(getActivity()));
            mFileList.setAdapter(mAdapter);
        }
    }

    @Override
    public void onDeleteClicked(View view, File file) {
        new AlertDialog.Builder(requireActivity(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setIcon(android.R.drawable.ic_dialog_info)
                .setTitle("Delete")
                .setMessage("Are you sure you want to delete this video?")
                .setPositiveButton("Yes", (dialog, which) -> {
                    if(file.delete()) {
                        mAdapter.remove(file);
                    }
                })
                .setNegativeButton("No", null)
                .show();
    }

    private void startWorker(File inputFile, Uri outputUri) {
        OneTimeWorkRequest request =
                new OneTimeWorkRequest.Builder(VideoProcessWorker.class)
                        .setInputData(new Data.Builder()
                                .putString(VideoProcessWorker.INPUT_PATH_KEY, inputFile.getPath())
                                .putString(VideoProcessWorker.OUTPUT_URI_KEY, outputUri.toString())
                                .build())
                        .build();

        WorkManager.getInstance(requireActivity())
                .enqueueUniqueWork(
                        CameraActivity.WORKER_VIDEO_PROCESSOR,
                        ExistingWorkPolicy.APPEND_OR_REPLACE,
                        request);
    }

    @Override
    public void onQueueClicked(View queueBtn, View deleteBtn, File file) {
        mSelectedFile = file;

        queueBtn.setEnabled(false);
        deleteBtn.setEnabled(false);

        ((Button) queueBtn).setText("Queued");

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);

        mSelectDocumentLauncher.launch(intent);
    }
}
