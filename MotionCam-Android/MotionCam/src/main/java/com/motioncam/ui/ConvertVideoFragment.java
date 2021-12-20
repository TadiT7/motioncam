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
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.stream.Collectors;

public class ConvertVideoFragment  extends Fragment implements LifecycleObserver, RawVideoAdapter.OnQueueListener {
    private ActivityResultLauncher<Intent> mSelectDocumentLauncher;
    private RecyclerView mFileList;
    private View mNoFiles;
    private RawVideoAdapter mAdapter;
    private VideoEntry mSelectedVideo;
    private int mNumFramesToMerge;
    private View mConvertSettings;

    public static ConvertVideoFragment newInstance() {
        return new ConvertVideoFragment();
    }

    static private String normalisedName(String name) {
        if(name == null)
            return null;

        if(name.contains("."))
            name = name.substring(0, name.lastIndexOf('.'));

        return name.toUpperCase(Locale.ROOT);
    }

    static private boolean isVideo(DocumentFile file) {
        if(!file.isFile())
            return false;

        String name = file.getName();
        if(name == null)
            return false;

        if(name.toLowerCase(Locale.ROOT).endsWith("zip"))
            return true;

        return name.toLowerCase(Locale.ROOT).endsWith("container");
    }

    static private boolean isAudio(DocumentFile file) {
        if(!file.isFile())
            return false;

        String name = file.getName();
        if(name == null)
            return false;

        return name.toLowerCase(Locale.ROOT).endsWith("wav");
    }

    private boolean deleteUri(Uri uri) {
        if(uri == null)
            return false;

        if(uri.getScheme().equalsIgnoreCase("file")) {
            File f = new File(uri.getPath());
            return f.delete();
        }
        else {
            DocumentFile documentFile = DocumentFile.fromSingleUri(requireContext(), uri);
            return documentFile.delete();
        }
    }

    private boolean deleteEntry(VideoEntry entry) {
        boolean deleted = deleteUri(entry.getAudioUri());
        deleted |= deleteUri(entry.getVideoUri());

        return deleted;
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {

        return inflater.inflate(R.layout.convert_video_fragment, container, false);
    }

    private Uri getExportUri() {
        SharedPreferences sharedPrefs =
                getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String exportUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_OUTPUT_URI, null);
        if(exportUriString == null)
            return null;

        return Uri.parse(exportUriString);
    }

    @Override
    public void onAttach(@NonNull Context context) {
        super.onAttach(context);

        mSelectDocumentLauncher = registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(),
                result -> {
                    if (result.getResultCode() == Activity.RESULT_OK) {
                        Intent data = result.getData();

                        if(data != null && data.getData() != null) {
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

                                startWorker(mSelectedVideo, uri, mNumFramesToMerge);
                            }
                        }

                        mSelectedVideo = null;
                    }
                });
    }

    private void addVideoEntries(Map<String, VideoEntry> entries, DocumentFile root) {
        if(root == null || !root.exists()) {
            return;
        }

        DocumentFile[] documentFiles = root.listFiles();

        Uri exportUri = getExportUri();
        DocumentFile exportDocumentFile = null;

        if(exportUri != null)
            exportDocumentFile = DocumentFile.fromTreeUri(requireContext(), exportUri);

        for(DocumentFile documentFile : documentFiles) {
            String name = normalisedName(documentFile.getName());
            if(name == null)
                continue;

            Uri uri = documentFile.getUri();
            VideoEntry entry;

            entry = entries.get(name);

            boolean isAudio = false;
            boolean isVideo = false;

            if(isVideo(documentFile))
                isVideo = true;
            else if(isAudio(documentFile))
                isAudio = true;

            if(entry == null && (isAudio || isVideo)) {
                entry = new VideoEntry(name);
                entries.put(name, entry);

                if(exportDocumentFile != null && exportDocumentFile.findFile(name) != null)
                    entry.setAlreadyExported(true);
                else
                    entry.setAlreadyExported(false);
            }

            if(isVideo) {
                entry.setVideoUri(uri);
                entry.setCreatedAt(documentFile.lastModified());
            }
            else if(isAudio) {
                entry.setAudioUri(uri);
            }
        }
    }

    private Collection<VideoEntry> getVideos() {
        SharedPreferences sharedPrefs =
                getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        Map<String, VideoEntry> entries = new HashMap<>();

        // List all videos in the user specified directory, if it exists.
        String tempVideosUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, null);

        if(tempVideosUriString != null && !tempVideosUriString.isEmpty()) {
            Uri tempVideosUri = Uri.parse(tempVideosUriString);
            DocumentFile root = DocumentFile.fromTreeUri(requireContext(), tempVideosUri);

            addVideoEntries(entries, root);
        }

        // Get internal storage videos
        if(getActivity() != null) {
            File filesDir = getActivity().getFilesDir();
            File outputDirectory = new File(filesDir, VideoProcessWorker.VIDEOS_PATH);

            DocumentFile root = DocumentFile.fromFile(outputDirectory);

            addVideoEntries(entries, root);
        }

        // Sort the videos by creation time
        List<VideoEntry> result = entries.values()
                .stream()
                .sorted((o1, o2) -> ((Long) o1.getCreatedAt()).compareTo(o2.getCreatedAt()))
                .collect(Collectors.toList());

        return result;
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        mFileList = view.findViewById(R.id.fileList);
        mNoFiles = view.findViewById(R.id.noFiles);
        mConvertSettings = view.findViewById(R.id.convertSettings);

        mNumFramesToMerge = 0;

        final TextView mergeFramesText = view.findViewById(R.id.mergeFrames);

        ((SeekBar) view.findViewById(R.id.mergeFramesSeekBar)).setOnSeekBarChangeListener(
                new SeekBar.OnSeekBarChangeListener() {
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

        Collection<VideoEntry> videoFiles = getVideos();

        if(videoFiles.size() == 0) {
            mNoFiles.setVisibility(View.VISIBLE);
            mFileList.setVisibility(View.GONE);
            mConvertSettings.setVisibility(View.GONE);
        }
        else {
            mFileList.setVisibility(View.VISIBLE);
            mConvertSettings.setVisibility(View.VISIBLE);
            mNoFiles.setVisibility(View.GONE);

            mAdapter = new RawVideoAdapter(requireContext(), videoFiles, this);

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
    public void onDeleteClicked(VideoEntry entry) {
        new AlertDialog.Builder(requireActivity(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setIcon(android.R.drawable.ic_dialog_info)
                .setTitle(R.string.delete)
                .setMessage(R.string.confirm_delete)
                .setPositiveButton(R.string.yes, (dialog, which) -> {
                    if(deleteEntry(entry)) {
                        if(mAdapter.remove(entry) == 0) {
                            mNoFiles.setVisibility(View.VISIBLE);
                            mFileList.setVisibility(View.GONE);
                            mConvertSettings.setVisibility(View.GONE);
                        }
                    }
                })
                .setNegativeButton(R.string.no, null)
                .show();
    }

    private void startWorker(VideoEntry videoEntry, Uri outputUri, int numFramesToMerge) {
        Data.Builder inputDataBuilder = new Data.Builder()
                .putString(VideoProcessWorker.OUTPUT_URI_KEY, outputUri.toString())
                .putInt(VideoProcessWorker.INPUT_NUM_FRAMES_TO_MERGE, numFramesToMerge);

        if(videoEntry.getVideoUri() != null)
            inputDataBuilder.putString(VideoProcessWorker.INPUT_URI_KEY, videoEntry.getVideoUri().toString());

        if(videoEntry.getAudioUri() != null)
            inputDataBuilder.putString(VideoProcessWorker.INPUT_AUDIO_URI_KEY, videoEntry.getAudioUri().toString());

        OneTimeWorkRequest request =
                new OneTimeWorkRequest.Builder(VideoProcessWorker.class)
                        .setInputData(inputDataBuilder.build())
                        .build();

        WorkManager.getInstance(requireActivity())
                .enqueueUniqueWork(
                        CameraActivity.WORKER_VIDEO_PROCESSOR,
                        ExistingWorkPolicy.APPEND_OR_REPLACE,
                        request);
    }

    @Override
    public void onQueueClicked(VideoEntry entry) {
        mSelectedVideo = entry;

        SharedPreferences sharedPrefs =
                getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String dstUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_OUTPUT_URI, null);
        if(dstUriString == null) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            mSelectDocumentLauncher.launch(intent);
        }
        else {
            Uri dstUri = Uri.parse(dstUriString);

            if(!DocumentFile.fromTreeUri(requireContext(), dstUri).exists()) {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                mSelectDocumentLauncher.launch(intent);
            }
            else {
                startWorker(entry, dstUri, mNumFramesToMerge);
            }
        }

        mAdapter.update(entry.getVideoUri(), true, false, 0);
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

        Uri currentUri = null;

        if(currentWorkInfo != null) {
            Data progress = currentWorkInfo.getProgress();

            int state = progress.getInt(State.PROGRESS_STATE_KEY, -1);
            String inputUriString = progress.getString(State.PROGRESS_INPUT_URI_KEY);

            if (state == State.STATE_PROCESSING) {
                int progressAmount = progress.getInt(State.PROGRESS_PROGRESS_KEY, 0);
                currentUri = Uri.parse(inputUriString);

                mAdapter.update(currentUri, true, false, progressAmount);
            }
        }

        for(WorkInfo workInfo : workInfos) {
            if(workInfo.getState() == WorkInfo.State.SUCCEEDED) {
                Data output = workInfo.getOutputData();
                int state = output.getInt(State.PROGRESS_STATE_KEY, -1);

                if (state != State.STATE_COMPLETED) {
                    continue;
                }

                String inputUriString = output.getString(State.PROGRESS_INPUT_URI_KEY);
                if(inputUriString == null) {
                    continue;
                }

                Uri inputUri = Uri.parse(inputUriString);

                // Don't update if there's an existing one in progress with the same input URI
                if(currentUri != null && currentUri.equals(inputUri))
                    continue;

                mAdapter.update(inputUri, false, true, -1);
            }
        }
    }
}
