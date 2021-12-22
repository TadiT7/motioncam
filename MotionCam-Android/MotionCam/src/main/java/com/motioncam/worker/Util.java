package com.motioncam.worker;

import android.content.Context;
import android.net.Uri;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;

public class Util {
    static public boolean DeleteUri(Context context, Uri uri) {
        if(uri == null)
            return false;

        if(uri.getScheme().equalsIgnoreCase("file")) {
            File f = new File(uri.getPath());
            return f.delete();
        }
        else {
            DocumentFile documentFile = DocumentFile.fromSingleUri(context, uri);
            return documentFile.delete();
        }
    }
}
