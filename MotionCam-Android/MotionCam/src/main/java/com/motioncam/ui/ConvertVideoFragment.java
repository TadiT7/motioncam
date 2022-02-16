package com.motioncam.ui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CheckBox;
import android.widget.ProgressBar;
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

import com.google.common.util.concurrent.ListenableFuture;
import com.motioncam.CameraActivity;
import com.motioncam.R;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.worker.State;
import com.motioncam.worker.Util;
import com.motioncam.worker.VideoProcessWorker;

import java.io.File;
import java.util.Collection;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

public class ConvertVideoFragment  extends Fragment implements LifecycleObserver, RawVideoAdapter.OnQueueListener {
    private ActivityResultLauncher<Intent> mSelectDocumentLauncher;
    private RecyclerView mFileList;
    private View mNoFiles;
    private RawVideoAdapter mAdapter;
    private VideoEntry mSelectedVideo;
    private VideoProcessWorker.WorkerMode mWorkerMode;
    private int mNumFramesToMerge;
    private View mConvertSettings;
    private boolean mDeleteAfterExport;
    private boolean mCorrectVignette;

    static final String AUDIO_TAG = "audio";
    static final String VIDEO_TAG = "video";

    public static ConvertVideoFragment newInstance() {
        return new ConvertVideoFragment();
    }

    static private String normalisedName(String name) {
        if(name == null)
            return null;

        // Check if segmented
        String[] parts = name.split("\\.");
        if(parts.length == 3) {
            return parts[0];
        }

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

    private boolean deleteEntry(VideoEntry entry) {
        boolean deleted = Util.DeleteUri(requireContext(), entry.getAudioUri());

        for(Uri uri : entry.getVideoUris())
            deleted |= Util.DeleteUri(requireContext(), uri);

        return deleted;
    }

    private void loadSettings(SharedPreferences sharedPrefs) {
        ProgressBar mergeFramesSeekBar = requireView().findViewById(R.id.mergeFramesSeekBar);
        CheckBox deleteAfterExportCheckbox = requireView().findViewById(R.id.deleteAfterExport);
        CheckBox correctVignetterCheckbox = requireView().findViewById(R.id.correctVignette);

        mDeleteAfterExport = sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_DELETE_AFTER_EXPORT, false);
        mCorrectVignette = sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_CORRECT_VIGNETTE, true);
        mNumFramesToMerge = sharedPrefs.getInt(SettingsViewModel.PREFS_KEY_RAW_VIDEO_MERGE_FRAMES, 0);

        deleteAfterExportCheckbox.setChecked(mDeleteAfterExport);
        correctVignetterCheckbox.setChecked(mCorrectVignette);
        mergeFramesSeekBar.setProgress(mNumFramesToMerge / 4);
    }

    private void saveSettings(SharedPreferences sharedPrefs) {
        sharedPrefs.edit()
                .putBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_DELETE_AFTER_EXPORT, mDeleteAfterExport)
                .putBoolean(SettingsViewModel.PREFS_KEY_RAW_VIDEO_CORRECT_VIGNETTE, mCorrectVignette)
                .putInt(SettingsViewModel.PREFS_KEY_RAW_VIDEO_MERGE_FRAMES, mNumFramesToMerge)
                .apply();
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
                requireActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String exportUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, null);
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
                                        .putString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, uri.toString())
                                        .apply();

                                if(mSelectedVideo != null) {
                                    startWorker(mSelectedVideo, uri, mNumFramesToMerge, mWorkerMode);
                                    mAdapter.update(mSelectedVideo.getName(), true, null, -1);
                                }
                            }
                        }

                        mSelectedVideo = null;
                        mWorkerMode = null;
                    }
                });
    }

    private void addVideoEntries(Map<String, VideoEntry> entries, DocumentFile root, boolean isInternal) {
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

                entry.setIsInternal(isInternal);
                entry.setAlreadyExported(
                        exportDocumentFile != null && exportDocumentFile.findFile(name) != null);
            }

            if(isVideo) {
                entry.addVideoUri(uri);
                entry.setCreatedAt(documentFile.lastModified());
            }
            else if(isAudio) {
                entry.setAudioUri(uri);
            }
        }
    }

    private Collection<VideoEntry> getVideos() {
        SharedPreferences sharedPrefs =
                requireActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        Map<String, VideoEntry> entries = new HashMap<>();

        // List all videos in the user specified directory, if it exists.
        String tempVideosUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI, null);

        if(tempVideosUriString != null && !tempVideosUriString.isEmpty()) {
            Uri tempVideosUri = Uri.parse(tempVideosUriString);
            DocumentFile root = DocumentFile.fromTreeUri(requireContext(), tempVideosUri);

            addVideoEntries(entries, root, false);
        }

        // Get second storage location
        tempVideosUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_TEMP_OUTPUT_URI_2, null);
        if(tempVideosUriString != null && !tempVideosUriString.isEmpty()) {
            Uri tempVideosUri = Uri.parse(tempVideosUriString);
            DocumentFile root = DocumentFile.fromTreeUri(requireContext(), tempVideosUri);

            addVideoEntries(entries, root, false);
        }

        // Get internal storage videos
        if(getActivity() != null) {
            File filesDir = getActivity().getFilesDir();
            File outputDirectory = new File(filesDir, VideoProcessWorker.VIDEOS_PATH);

            DocumentFile root = DocumentFile.fromFile(outputDirectory);

            addVideoEntries(entries, root, true);
        }

        // Finally add the export folder since we can move videos there from internal storage
        String exportUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, null);
        if(exportUriString != null) {
            Uri exportUri = Uri.parse(exportUriString);
            DocumentFile root = DocumentFile.fromTreeUri(requireContext(), exportUri);

            addVideoEntries(entries, root, false);
        }

        // Sort the videos by creation time
        return entries.values()
                .stream()
                .sorted(Comparator.comparingLong(VideoEntry::getCreatedAt))
                .collect(Collectors.toList());
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
                mNumFramesToMerge = progress * 4;
                mergeFramesText.setText(String.format(Locale.US, "%d", mNumFramesToMerge));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        ((CheckBox) view.findViewById(R.id.deleteAfterExport)).setOnCheckedChangeListener(
                (buttonView, isChecked) -> mDeleteAfterExport = isChecked);

        ((CheckBox) view.findViewById(R.id.correctVignette)).setOnCheckedChangeListener(
                (buttonView, isChecked) -> mCorrectVignette = isChecked);

        ((SimpleItemAnimator) mFileList.getItemAnimator()).setSupportsChangeAnimations(false);

        view.findViewById(R.id.setExportFolderBtn).setOnClickListener(e -> selectExportFolder());

        if(getContext() != null) {
            WorkManager.getInstance(getContext())
                    .getWorkInfosForUniqueWorkLiveData(CameraActivity.WORKER_VIDEO_PROCESSOR)
                    .observe(getViewLifecycleOwner(), this::onProgressChanged);

            SharedPreferences sharedPrefs =
                    getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

            loadSettings(sharedPrefs);
        }
    }

    private void selectExportFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        mSelectDocumentLauncher.launch(intent);
    }

    private void updateQueuedWork() {
        ListenableFuture<List<WorkInfo>> queuedWorkPromise = WorkManager.getInstance(requireContext())
                .getWorkInfosForUniqueWork(CameraActivity.WORKER_VIDEO_PROCESSOR);

        // Update queued work
        try {
            List<WorkInfo> queuedWork = queuedWorkPromise.get(500, TimeUnit.MILLISECONDS);
            if(queuedWork != null)
                onProgressChanged(queuedWork);
        }
        catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void refresh() {
        CompletableFuture
                .supplyAsync(() -> getVideos())
                .thenAccept((videoFiles) -> requireActivity().runOnUiThread(() ->
                {
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
                }));
    }

    @Override
    public void onResume() {
        super.onResume();

        refresh();

        updateQueuedWork();
    }

    @Override
    public void onPause() {
        super.onPause();

        SharedPreferences sharedPrefs =
                requireActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        saveSettings(sharedPrefs);
    }

    @Override
    public void onDeleteClicked(VideoEntry entry) {
        new AlertDialog.Builder(requireActivity(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setIcon(android.R.drawable.ic_dialog_info)
                .setTitle(R.string.delete)
                .setMessage(R.string.confirm_delete)
                .setPositiveButton(R.string.yes, (dialog, which) -> {
                    if(deleteEntry(entry)) {
                        if(mAdapter.remove(entry.getName()) == 0) {
                            mNoFiles.setVisibility(View.VISIBLE);
                            mFileList.setVisibility(View.GONE);
                            mConvertSettings.setVisibility(View.GONE);
                        }
                    }
                })
                .setNegativeButton(R.string.no, null)
                .show();
    }

    private void startWorker(VideoEntry videoEntry, Uri outputUri, int numFramesToMerge, VideoProcessWorker.WorkerMode workerMode) {
        if(videoEntry == null || (videoEntry.getAudioUri() == null && videoEntry.getVideoUris().isEmpty()))
            return; // Nothing to do here

        Data.Builder inputDataBuilder = new Data.Builder()
                .putString(VideoProcessWorker.INPUT_MODE_KEY, workerMode.name())
                .putString(VideoProcessWorker.OUTPUT_URI_KEY, outputUri.toString())
                .putString(VideoProcessWorker.INPUT_NAME_KEY, videoEntry.getName())
                .putBoolean(VideoProcessWorker.INPUT_DELETE_AFTER_EXPORT_KEY, mDeleteAfterExport)
                .putBoolean(VideoProcessWorker.INPUT_CORRECT_VIGNETTE_KEY, mCorrectVignette)
                .putInt(VideoProcessWorker.INPUT_NUM_FRAMES_TO_MERGE, numFramesToMerge);

        OneTimeWorkRequest.Builder requestBuilder = new OneTimeWorkRequest.Builder(VideoProcessWorker.class);

        String[] videoUris =
                videoEntry.getVideoUris()
                        .stream().map(Uri::toString)
                        .toArray(String[]::new);

        inputDataBuilder.putStringArray(VideoProcessWorker.INPUT_VIDEO_URI_KEY, videoUris);
        requestBuilder.addTag(VIDEO_TAG + "=" + videoEntry.getName());

        if(videoEntry.getAudioUri() != null) {
            inputDataBuilder.putString(VideoProcessWorker.INPUT_AUDIO_URI_KEY, videoEntry.getAudioUri().toString());
            requestBuilder.addTag(AUDIO_TAG + "=" + videoEntry.getName());
        }

        requestBuilder.setInputData(inputDataBuilder.build());

        WorkManager.getInstance(requireActivity())
                .enqueueUniqueWork(
                        CameraActivity.WORKER_VIDEO_PROCESSOR,
                        ExistingWorkPolicy.APPEND_OR_REPLACE,
                        requestBuilder.build());
    }

    @Override
    public void onQueueClicked(VideoEntry entry) {
        mSelectedVideo = entry;
        mWorkerMode = VideoProcessWorker.WorkerMode.EXPORT;

        SharedPreferences sharedPrefs =
                requireActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String dstUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, null);
        if(dstUriString == null) {
            selectExportFolder();
        }
        else {
            Uri dstUri = Uri.parse(dstUriString);

            if(!DocumentFile.fromTreeUri(requireContext(), dstUri).exists()) {
                selectExportFolder();
            }
            else {
                startWorker(entry, dstUri, mNumFramesToMerge, VideoProcessWorker.WorkerMode.EXPORT);
            }
        }

        mAdapter.update(entry.getName(), true, null, -1);
    }

    @Override
    public void onMoveClicked(VideoEntry entry) {
        mSelectedVideo = entry;
        mWorkerMode = VideoProcessWorker.WorkerMode.MOVE;

        SharedPreferences sharedPrefs =
                getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        String dstUriString = sharedPrefs.getString(SettingsViewModel.PREFS_KEY_RAW_VIDEO_EXPORT_URI, null);
        if(dstUriString == null) {
            selectExportFolder();
        }
        else {
            Uri dstUri = Uri.parse(dstUriString);
            DocumentFile dst = DocumentFile.fromTreeUri(requireContext(), dstUri);

            if(dst == null || !dst.exists()) {
                selectExportFolder();
            }
            else {
                startWorker(entry, dstUri, -1, VideoProcessWorker.WorkerMode.MOVE);
            }
        }

        mAdapter.update(entry.getName(), true, null, -1);
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

        String videoInProgress = null;

        if(currentWorkInfo != null) {
            Data progress = currentWorkInfo.getProgress();

            int state = progress.getInt(State.PROGRESS_STATE_KEY, -1);
            String name = progress.getString(State.PROGRESS_NAME_KEY);

            if (name != null) {
                videoInProgress = name;

                if (state == State.STATE_PROCESSING) {
                    int progressAmount = progress.getInt(State.PROGRESS_PROGRESS_KEY, 0);
                    mAdapter.update(name, true, null, progressAmount);
                }
            }
        }

        Set<String> queued = new HashSet<>();

        // Build list of queued videos
        for(WorkInfo workInfo : workInfos) {
            boolean enqueued =
                    workInfo.getState() == WorkInfo.State.ENQUEUED |
                    workInfo.getState() == WorkInfo.State.BLOCKED;

            // Add all tags, some are not be URIs but that doesn't really matter
            if(enqueued) {
                Set<String> tags = workInfo.getTags();
                for (String tag : tags) {
                    tag = tag.replace(VIDEO_TAG + "=", "");
                    tag = tag.replace(AUDIO_TAG + "=", "");

                    queued.add(tag);
                }
            }
        }

        // Update the state of all entries
        for(WorkInfo workInfo : workInfos) {
            String audioNameTag = null;
            String videoNameTag = null;

            Set<String> tags = workInfo.getTags();
            for(String tag : tags) {
                if(tag.startsWith(AUDIO_TAG)) {
                    audioNameTag = tag.replace(AUDIO_TAG + "=", "");
                }
                else if (tag.startsWith(VIDEO_TAG)) {
                    videoNameTag = tag.replace(VIDEO_TAG + "=", "");
                }
            }

            // Ignore work in progress
            if(videoNameTag != null && videoNameTag.equals(videoInProgress))
                continue;

            // Ignore old entries we don't know about
            if(!mAdapter.isValid(videoNameTag))
                continue;

            // First check if queued
            if(audioNameTag != null && queued.contains(audioNameTag)) {
                mAdapter.update(audioNameTag, true, null, -1);
            }
            else if(videoNameTag != null && queued.contains(videoNameTag)) {
                mAdapter.update(videoNameTag, true, null, -1);
            }
            else if(workInfo.getState() == WorkInfo.State.SUCCEEDED ||
                    workInfo.getState() == WorkInfo.State.CANCELLED)
            {
                // Has it finished?
                String name = null;

                if(audioNameTag != null)
                    name = audioNameTag;
                else if(videoNameTag != null)
                    name = videoNameTag;

                if(name != null) {
                    Data outputData = workInfo.getOutputData();
                    VideoProcessWorker.WorkerMode workerMode = VideoProcessWorker.WorkerMode.EXPORT;

                    boolean isDeleted = outputData.getBoolean(State.PROGRESS_DELETED, false);
                    String workerModeString = outputData.getString(State.PROGRESS_MODE_KEY);

                    if(workerModeString != null) {
                        workerMode = VideoProcessWorker.WorkerMode.valueOf(workerModeString);
                    }

                    // If we moved a video, refresh the list
                    if(workerMode == VideoProcessWorker.WorkerMode.MOVE) {
                        VideoEntry entry = mAdapter.getItemFromName(name);
                        if(entry != null && entry.isInternal())
                            refresh();
                    }
                    else {
                        if (isDeleted)
                            mAdapter.remove(name);
                        else
                            mAdapter.update(name, false, null, -1);
                    }
                }
            }
        }
    }
}
