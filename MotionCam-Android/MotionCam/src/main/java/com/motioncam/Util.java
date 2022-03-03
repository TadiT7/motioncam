package com.motioncam;

import static com.motioncam.CameraActivity.TAG;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayList;

public class Util {
    public static class DocumentFileEntry {
        public final Uri uri;
        public final String displayName;
        public final long lastModified;
        public final String mimeType;

        private DocumentFileEntry(Uri uri, String displayName, long lastModified, String mimeType) {
            this.uri = uri;
            this.displayName = displayName;
            this.lastModified = lastModified;
            this.mimeType = mimeType;
        }
    }

    static public boolean DeleteUri(Context context, Uri uri) {
        if(uri == null)
            return false;

        if(uri.getScheme() != null && uri.getPath() != null && uri.getScheme().equalsIgnoreCase("file")) {
            return new File(uri.getPath()).delete();
        }
        else {
            DocumentFile documentFile = DocumentFile.fromSingleUri(context, uri);
            if(documentFile != null)
                return documentFile.delete();
        }

        return false;
    }

    static public int CreateNewFile(Context context, Uri rootFolder, String filename, String mimeType) {
        try {
            DocumentFile root = DocumentFile.fromTreeUri(context, rootFolder);
            if(root == null)
                return -1;

            if (root.exists() && root.isDirectory() && root.canWrite()) {
                DocumentFile output = root.createFile(mimeType, filename);
                if(output == null)
                    return -1;

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

    private static ArrayList<DocumentFileEntry> internalListFiles(File root) {
        final ArrayList<DocumentFileEntry> results = new ArrayList<>();
        if(root == null || !root.exists())
            return results;

        File[] files = root.listFiles();
        if(files == null)
            return results;

        for(File file : files) {
            if(file.isDirectory())
                continue;

            results.add(new DocumentFileEntry(Uri.fromFile(file), file.getName(), file.lastModified(), "application/octet-stream"));
        }

        return results;
    }

    static private ArrayList<DocumentFileEntry> internalListFiles(Context context, Uri uri) {
        final ContentResolver resolver = context.getContentResolver();
        final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, DocumentsContract.getDocumentId(uri));
        final ArrayList<DocumentFileEntry> results = new ArrayList<>();

        final String[] projection = new String[] {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED,
                DocumentsContract.Document.COLUMN_MIME_TYPE };

        try (Cursor c = resolver.query(childrenUri, projection, null, null, null)) {
            if(c == null)
                return results;

            while (c.moveToNext()) {

                // TODO
                //c.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID)

                final String documentId = c.getString(0);
                final String documentName = c.getString(1);
                final long documentLastModified = c.getLong(2);
                final String documentMimeType = c.getString(3);

                final Uri documentUri = DocumentsContract.buildDocumentUriUsingTree(uri, documentId);

                results.add(
                        new DocumentFileEntry(documentUri, documentName, documentLastModified, documentMimeType));
            }
        }
        catch (Exception e) {
            Log.w(TAG, "Failed query: " + e);
        }

        return results;
    }

    static public ArrayList<DocumentFileEntry> listFiles(Context context, Uri uri) {
        if(uri.getScheme() != null && uri.getPath() != null && uri.getScheme().equalsIgnoreCase("file")) {
            return internalListFiles(new File(uri.getPath()));
        }
        else
            return internalListFiles(context, uri);
    }
}
