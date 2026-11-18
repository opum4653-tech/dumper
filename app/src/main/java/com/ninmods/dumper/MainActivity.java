package com.ninmods.dumper;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.view.Gravity;

public class MainActivity extends Activity {
    static {
        try {
            System.loadLibrary("NINMODS");
        } catch (UnsatisfiedLinkError e) {
            e.printStackTrace();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView tv = new TextView(this);
        tv.setTextSize(18.0f);
        tv.setGravity(Gravity.CENTER);
        tv.setText("NINMODS In-Memory Il2Cpp Dumper\n\nNative Library: libNINMODS.so (arm64-v8a & armeabi-v7a)\nUniversal Engine Injection Loaded.");
        setContentView(tv);
    }
}
