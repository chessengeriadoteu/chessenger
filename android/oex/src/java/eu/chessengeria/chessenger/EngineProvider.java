/*
 * Chessenger OEX engine provider. GPL-3.0-or-later.
 * Read-only: hands out the engine executable that the package installer
 * extracted to ApplicationInfo.nativeLibraryDir. Nothing else is served.
 */
package eu.chessengeria.chessenger;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

public final class EngineProvider extends ContentProvider {
    static final String ENGINE_FILE = "libchessenger.so";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (mode == null || !mode.equals("r")) {
            throw new FileNotFoundException("read-only");
        }
        String name = uri.getLastPathSegment();
        if (name == null || !name.equals(ENGINE_FILE)) {
            throw new FileNotFoundException("unknown file");
        }
        File f = new File(getContext().getApplicationInfo().nativeLibraryDir, ENGINE_FILE);
        return ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return "application/octet-stream";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}
