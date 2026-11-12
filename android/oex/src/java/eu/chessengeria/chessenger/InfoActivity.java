/*
 * Chessenger OEX engine package: information screen. GPL-3.0-or-later.
 */
package eu.chessengeria.chessenger;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public final class InfoActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView t = new TextView(this);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        t.setPadding(pad, pad, pad, pad);
        t.setTextSize(18);
        t.setText("Chessenger 4.2 engine is installed. Open your chess app and pick "
                + "Chessenger from its engine list.\n\n"
                + "License: GPL-3.0. Source: https://github.com/chessengeriadoteu/chessenger");
        setContentView(t);
    }
}
