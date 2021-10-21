package com.nomone.cc.samples.hello_cc;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

import com.nomone.std_lib.StdLibNatives;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);
        TextView textView = findViewById(R.id.hello_textView);
        textView.setText("Please check you logcat!");

        try { Thread.sleep(2000); } catch (InterruptedException e) { e.printStackTrace(); }

        StdLibNatives.start();
    }
}
