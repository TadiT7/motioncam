package com.motioncam.ui;

import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.LifecycleObserver;
import androidx.lifecycle.OnLifecycleEvent;
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

        requireActivity().getLifecycle().addObserver(this);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        File outputDirectory = new File(getContext().getFilesDir(), VideoProcessWorker.VIDEOS_PATH);

        RecyclerView fileList = view.findViewById(R.id.fileList);

        fileList.setLayoutManager(new LinearLayoutManager(getActivity()));
        fileList.setAdapter(new RawVideoAdapter(outputDirectory.listFiles(), this));
    }

    @OnLifecycleEvent(Lifecycle.Event.ON_CREATE)
    void onCreated() {
        getActivity().getLifecycle().removeObserver(this);
    }

    @Override
    public void onQueueClicked(View view, File file) {
        OneTimeWorkRequest request =
                new OneTimeWorkRequest.Builder(VideoProcessWorker.class)
                        .setInputData(new Data.Builder()
                                .putString(VideoProcessWorker.INPUT_PATH_KEY, file.getPath())
                                .build())
                        .build();

        WorkManager.getInstance(getActivity())
                .enqueueUniqueWork(
                        CameraActivity.WORKER_VIDEO_PROCESSOR,
                        ExistingWorkPolicy.APPEND_OR_REPLACE,
                        request);

        view.setEnabled(false);
        ((Button) view).setText("Queued");

        Toast.makeText(getActivity(), "The recording will be processed to the Download folder", Toast.LENGTH_LONG).show();
    }
}
