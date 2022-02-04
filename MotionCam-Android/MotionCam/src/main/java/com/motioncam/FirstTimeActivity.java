package com.motioncam;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.core.app.ActivityCompat;

import com.motioncam.model.SettingsViewModel;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class FirstTimeActivity extends Activity {
    private static final int PERMISSION_REQUEST_CODE = 0x50001000;

    private static final String[] MINIMUM_PERMISSIONS = {
            Manifest.permission.CAMERA,
    };

    private static final String[] ADDITIONAL_PERMISSIONS = {
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.WRITE_EXTERNAL_STORAGE
    };

    private static String[] SUPPORTED_PHONE_LIST = {
            "OnePlus 7/8/9 Pro",
            "Samsung S20 FE (Snapdragon)",
            "Xiaomi Mi 10 Ultra",
            "Xiaomi Mi 11 Ultra",
            "Motorola Edge Plus",
            "Asus ZenFone 7 Pro",
            "Samsung Galaxy Note 9",
            "LG V40",
            "ZTE Axon 30 Ultra",
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.first_time_activity);

        findViewById(R.id.startBtn).setOnClickListener((v) -> requestPermissions());

        ListView supportedPhones = findViewById(R.id.supportedPhoneList);
        supportedPhones.setAdapter(new ArrayAdapter<>(this, R.layout.simple_list_item, SUPPORTED_PHONE_LIST));
    }

    private void requestPermissions() {
        ArrayList<String> needPermissions = new ArrayList<>();

        for(String permission : MINIMUM_PERMISSIONS) {
            if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                needPermissions.add(permission);
            }
        }

        if(!needPermissions.isEmpty()) {
            for(String permission : ADDITIONAL_PERMISSIONS) {
                if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                    needPermissions.add(permission);
                }
            }
        }

        if(!needPermissions.isEmpty()) {
            String[] permissions = needPermissions.toArray(new String[0]);
            ActivityCompat.requestPermissions(this, permissions, PERMISSION_REQUEST_CODE);
        }
        else {
            onPermissionsGranted();
        }
    }

    private void onPermissionsGranted() {
        // Disable first run key
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        sharedPrefs.edit()
                .putBoolean(SettingsViewModel.PREFS_KEY_FIRST_RUN, false)
                .apply();

        // Start camera
        Intent intent = new Intent(this, CameraActivity.class);
        startActivity(intent);

        finish();
    }

    private void onPermissionsDenied() {
        AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                .setCancelable(false)
                .setTitle(R.string.error)
                .setMessage(R.string.permissions_error)
                .setPositiveButton(R.string.ok, (dialog, which) -> {
                    // no op
                });

        dialogBuilder.show();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (PERMISSION_REQUEST_CODE != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        // Check if camera permission has been denied
        List<String> minimumPermissions = Arrays.asList(MINIMUM_PERMISSIONS);

        for(int i = 0; i < permissions.length; i++) {
            if(grantResults[i] == PackageManager.PERMISSION_DENIED) {

                if(minimumPermissions.contains(permissions[i])) {
                    onPermissionsDenied();
                    return;
                }

            }
        }

        onPermissionsGranted();
    }
}
