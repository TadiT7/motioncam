package com.motioncam;

import android.os.Bundle;

import androidx.appcompat.app.AppCompatActivity;

import com.motioncam.ui.ConvertVideoFragment;

public class ConvertVideoActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.convert_video_activity);

        if (savedInstanceState == null) {
            ConvertVideoFragment fragment = ConvertVideoFragment.newInstance();

            getSupportFragmentManager()
                    .beginTransaction()
                    .replace(R.id.container, fragment)
                    .commitNow();
        }
    }
}
