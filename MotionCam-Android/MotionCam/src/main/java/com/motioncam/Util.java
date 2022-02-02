package com.motioncam;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import androidx.documentfile.provider.DocumentFile;

import java.io.FileNotFoundException;

public class Util {
    static public int CreateNewFile(Context context, Uri rootFolder, String filename, String mimeType) {
        try {
            DocumentFile root = DocumentFile.fromTreeUri(context, rootFolder);

            if (root.exists() && root.isDirectory() && root.canWrite()) {
                DocumentFile output = root.createFile(mimeType, filename);
                ContentResolver resolver = context.getContentResolver();

                try {
                    ParcelFileDescriptor pfd = resolver.openFileDescriptor(output.getUri(), "w", null);

                    if (pfd != null) {
                        return pfd.detachFd();
                    }
                } catch (FileNotFoundException e) {
                    e.printStackTrace();
                    return -1;
                }
            }
        }
        catch(Exception e) {
            e.printStackTrace();
        }

        return -1;
    }
}
