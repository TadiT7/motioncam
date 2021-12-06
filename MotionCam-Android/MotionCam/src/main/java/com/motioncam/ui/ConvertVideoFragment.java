package com.motioncam.ui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.documentfile.provider.DocumentFile;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.LifecycleObserver;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.recyclerview.widget.SimpleItemAnimator;
import androidx.work.Data;
import androidx.work.ExistingWorkPolicy;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkInfo;
import androidx.work.WorkManager;

import com.motioncam.CameraActivity;
import com.motioncam.R;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.worker.State;
import com.motioncam.worker.VideoProcessWorker;

import java.io.File;
import java.util.List;
import java.util.Locale;

public class ConvertVideoFragment  extends Fragment implements LifecycleObserver, RawVideoAdapter.OnQueueListener {
    private RecyclerView mFileList;
    private View mNoFiles;
    private RawVideoAdapter mAdapter;
    private ActivityResultLauncher<Intent> mSelectDocumentLauncher;
    private File mSelectedFile;
    private int mNumFramesToMerge;
    private View mConvertSettings;

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

                        if(data.getData() != null) {
                            final int takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
                            Uri uri = data.getData();

                            if(getActivity() != null) {
                                getActivity()
                                        .getContentResolver()
                                        .takePersistableUriPermission(uri, takeFlags);

                                SharedPreferences sharedPrefs =
                                        getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

                                sharedPrefs
                                        .edit()
                                        .putString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_OUTPUT_URI, uri.toString())
                                        .apply();

                                startWorker(mSelectedFile, uri, mNumFramesToMerge);
                            }
                        }

                        mSelectedFile = null;
                    }
                });
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        mFileList = view.findViewById(R.id.fileList);
        mNoFiles = view.findViewById(R.id.noFiles);
        mConvertSettings = view.findViewById(R.id.convertSettings);

        mNumFramesToMerge = 0;

        final TextView mergeFramesText = view.findViewById(R.id.mergeFrames);

        ((SeekBar) view.findViewById(R.id.mergeFramesSeekBar)).setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                mNumFramesToMerge = progress* 4;
                mergeFramesText.setText(String.format(Locale.US, "%d", mNumFramesToMerge));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        File[] videoFiles = null;

        if(getActivity() != null) {
            File filesDir = getActivity().getFilesDir();
            File outputDirectory = new File(filesDir, VideoProcessWorker.VIDEOS_PATH);

            videoFiles = outputDirectory.listFiles();
        }

        if(videoFiles == null)
            videoFiles = new File[0];

        if(videoFiles.length == 0) {
            mNoFiles.setVisibility(View.VISIBLE);
            mFileList.setVisibility(View.GONE);
            mConvertSettings.setVisibility(View.GONE);
        }
        else {
            mFileList.setVisibility(View.VISIBLE);
            mConvertSettings.setVisibility(View.VISIBLE);
            mNoFiles.setVisibility(View.GONE);

            mAdapter = new RawVideoAdapter(videoFiles, this);

            mFileList.setLayoutManager(new LinearLayoutManager(getActivity()));
            mFileList.setAdapter(mAdapter);
        }

        ((SimpleItemAnimator) mFileList.getItemAnimator()).setSupportsChangeAnimations(false);

        if(getContext() != null) {
            WorkManager.getInstance(getContext())
                    .getWorkInfosForUniqueWorkLiveData(CameraActivity.WORKER_VIDEO_PROCESSOR)
                    .observe(getViewLifecycleOwner(), this::onProgressChanged);
        }
    }

    @Override
    public void onDeleteClicked(File file) {
        new AlertDialog.Builder(requireActivity(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setIcon(android.R.drawable.ic_dialog_info)
                .setTitle("Delete")
                .setMessage("Are you sure you want to delete this video?")
                .setPositiveButton("Yes", (dialog, which) -> {
                    if(file.delete()) {
                        if(mAdapter.remove(file) == 0) {
                            mNoFiles.setVisibility(View.VISIBLE);
                            mFileList.setVisibility(View.GONE);
                            mConvertSettings.setVisibility(View.GONE);
                        }
                    }
                })
                .setNegativeButton("No", null)
                .show();
    }

    private void startWorker(File inputFile, Uri outputUri, int numFramesToMerge) {
        OneTimeWorkRequest request =
                new OneTimeWorkRequest.Builder(VideoProcessWorker.class)
                        .setInputData(new Data.Builder()
                                .putString(VideoProcessWorker.INPUT_PATH_KEY, inputFile.getPath())
                                .putString(VideoProcessWorker.OUTPUT_URI_KEY, outputUri.toString())
                                .putInt(VideoProcessWorker.INPUT_NUM_FRAMES_TO_MERGE, numFramesToMerge)
                                .build())
                        .build();

        WorkManager.getInstance(requireActivity())
                .enqueueUniqueWork(
                        CameraActivity.WORKER_VIDEO_PROCESSOR,
                        ExistingWorkPolicy.APPEND_OR_REPLACE,
                        request);
    }

    @Override
    public void onQueueClicked(File file) {
        mSelectedFile = file;

        SharedPreferences sharedPrefs =
                getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String dstUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_OUTPUT_URI, null);
        if(dstUriString == null) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            mSelectDocumentLauncher.launch(intent);
        }
        else {
            Uri dstUri = Uri.parse(dstUriString);

            if(!DocumentFile.fromTreeUri(getContext(), dstUri).exists()) {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                mSelectDocumentLauncher.launch(intent);
            }
            else {
                startWorker(file, dstUri, mNumFramesToMerge);
            }
        }

        mAdapter.update(file.getPath(), true, 0);
    }

    private void onProgressChanged(List<WorkInfo> workInfos) {
        if(mAdapter == null)
            return;

        WorkInfo currentWorkInfo = null;

        for(WorkInfo workInfo : workInfos) {
            if(workInfo.getState() == WorkInfo.State.RUNNING) {
                currentWorkInfo = workInfo;
                break;
            }
        }

        if(currentWorkInfo != null) {
            Data progress = currentWorkInfo.getProgress();

            int state = progress.getInt(State.PROGRESS_STATE_KEY, -1);
            String inputPath = progress.getString(State.PROGRESS_INPUT_PATH_KEY);

            if (state == State.STATE_PROCESSING) {
                int progressAmount = progress.getInt(State.PROGRESS_PROGRESS_KEY, 0);

                mAdapter.update(inputPath, true, progressAmount);
            }
        }

        for(WorkInfo workInfo : workInfos) {
            if(workInfo.getState() == WorkInfo.State.ENQUEUED) {
                continue;
            }
            else if(workInfo.getState() == WorkInfo.State.SUCCEEDED) {
                Data output = workInfo.getOutputData();
                int state = output.getInt(State.PROGRESS_STATE_KEY, -1);

                if (state == State.STATE_COMPLETED) {
                    String inputPath = output.getString(State.PROGRESS_INPUT_PATH_KEY);
                    if(inputPath != null) {
                        if (mAdapter.remove(new File(inputPath)) == 0) {
                            mNoFiles.setVisibility(View.VISIBLE);
                            mFileList.setVisibility(View.GONE);
                            mConvertSettings.setVisibility(View.GONE);
                        }
                    }
                }
            }
        }
    }
}
