package org.mocha.audiotest;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.RandomAccessFile;

/*
 * Audio test, the device's audio tester.
 *
 * "SCO loopback": the BT headset's mic recorded as VOICE_COMMUNICATION
 * until Stop, then played back whole into the headset, and saved as a WAV.
 * "SCO" uses the headset; "No SCO" the same with the built-in mic and
 * speaker, for comparison.
 *
 * "Echo test" measures echo cancelling on the speakerphone: a test signal
 * (a sweep, then speech-shaped noise bursts) played as a call on the
 * speaker, recorded twice, as UNPROCESSED (the echo as the mics have it)
 * and as VOICE_COMMUNICATION (what a VoIP app gets). Saved as
 * echo_ref.wav, echo_raw_N.wav and echo_vc_N.wav. Started from adb too:
 *   am start -n org.mocha.audiotest/.Main --ez echo true [--ei vol INDEX]
 *
 * "Raw take": SOURCE (UNPROCESSED by default) for SECONDS as float at
 * 48 kHz, nothing played, saved as raw_N.f32 (little-endian float32,
 * mono), to check what reaches an app:
 *   am start -n org.mocha.audiotest/.Main --ei raw SECONDS [--ei src SOURCE]
 *
 * "Hi-res": a sine through a DIRECT output, as a hi-res player asks for
 * one, at the stream's own rate and depth (the native part):
 *   am start -n org.mocha.audiotest/.Main --ei hifi RATE [--ei bits 16|24]
 *       [--ei freq HZ] [--ei secs SECONDS]
 */
public class Main extends Activity {
    static final String TAG = "AudioTest";
    static final int RATE = 16000;
    static final int MAX_SAMPLES = RATE * 60;  /* a minute at most */

    AudioManager am;
    TextView status;
    Button scoBtn, plainBtn, echoBtn;
    volatile boolean running;
    Thread loop;
    boolean useSco;
    int session = 0;

    final BroadcastReceiver scoReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
            int st = i.getIntExtra(AudioManager.EXTRA_SCO_AUDIO_STATE, -1);
            log("SCO audio state " + st);
            if (st == AudioManager.SCO_AUDIO_STATE_CONNECTED && useSco && loop == null) {
                startLoop();
            }
        }
    };

    void log(final String s) {
        Log.i(TAG, s);
        runOnUiThread(() -> status.setText(s + "\n" + status.getText()));
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        am = getSystemService(AudioManager.class);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        scoBtn = new Button(this);
        scoBtn.setText("Start SCO loopback");
        plainBtn = new Button(this);
        plainBtn.setText("Start loopback without SCO");
        echoBtn = new Button(this);
        echoBtn.setText("Echo test");
        status = new TextView(this);
        l.addView(scoBtn);
        l.addView(plainBtn);
        l.addView(echoBtn);
        l.addView(status);
        setContentView(l);
        scoBtn.setOnClickListener(v -> toggle(true));
        plainBtn.setOnClickListener(v -> toggle(false));
        echoBtn.setOnClickListener(v -> echoTest(-1));
        registerReceiver(scoReceiver,
                new IntentFilter(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED));
        requestPermissions(new String[] {android.Manifest.permission.RECORD_AUDIO}, 1);
        handle(getIntent());
    }

    void handle(Intent i) {
        if (i.getBooleanExtra("probe", false)) {
            log(NativeAudio.probe());
        } else if (i.getIntExtra("hifi", 0) > 0) {
            hifiTake(i.getIntExtra("hifi", 0), i.getIntExtra("bits", 24),
                    i.getIntExtra("freq", 1000), i.getIntExtra("secs", 10));
        } else if (i.getBooleanExtra("echo", false)) {
            echoTest(i.getIntExtra("vol", -1));
        } else if (i.getIntExtra("raw", 0) > 0) {
            rawTake(i.getIntExtra("raw", 0), i.getIntExtra("src",
                    MediaRecorder.AudioSource.UNPROCESSED));
        }
    }

    /* A tone through a DIRECT output, off the UI thread */
    void hifiTake(final int rate, final int bits, final int freq, final int secs) {
        if (running || loop != null) {
            log("busy");
            return;
        }
        running = true;
        loop = new Thread(() -> {
            log("hifi " + rate + " Hz " + bits + " bit, " + freq + " Hz tone, " + secs + " s");
            log(NativeAudio.playDirect(rate, bits, freq, secs));
            runOnUiThread(() -> { loop = null; running = false; });
        });
        loop.start();
    }

    void rawTake(final int secs, final int source) {
        if (running || loop != null) {
            log("busy");
            return;
        }
        running = true;
        final int n = ++session;
        loop = new Thread(() -> {
            final int rate = 48000;
            AudioRecord rec = new AudioRecord.Builder()
                    .setAudioSource(source)
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(rate)
                            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                            .setChannelMask(AudioFormat.CHANNEL_IN_MONO).build())
                    .setBufferSizeInBytes(rate * 4).build();
            float[] all = new float[rate * secs];
            int total = 0;
            try {
                rec.startRecording();
                while (total < all.length) {
                    int got = rec.read(all, total, Math.min(4800, all.length - total),
                            AudioRecord.READ_BLOCKING);
                    if (got <= 0) {
                        log("raw read " + got);
                        break;
                    }
                    total += got;
                }
                rec.stop();
                java.nio.ByteBuffer bb = java.nio.ByteBuffer.allocate(total * 4)
                        .order(java.nio.ByteOrder.LITTLE_ENDIAN);
                for (int k = 0; k < total; k++) bb.putFloat(all[k]);
                File f = new File(getExternalFilesDir(null), "raw_" + n + ".f32");
                try (java.io.FileOutputStream o = new java.io.FileOutputStream(f)) {
                    o.write(bb.array());
                }
                log("saved " + f.getName() + " (" + total + " samples)");
            } catch (Exception e) {
                log("error " + e);
            } finally {
                rec.release();
            }
            runOnUiThread(() -> { loop = null; running = false; });
        });
        loop.start();
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        handle(i);
    }

    /* The test signal: a 2 s log sweep 100 Hz - 7.5 kHz at -12 dBFS, a
     * pause, then 15 s of pink noise bursts (0.4 s on, 0.2 s off): long
     * enough for an echo canceller to converge in its first half */
    static short[] stimulus() {
        int n = RATE * 18;
        short[] x = new short[n];
        double f0 = 100, f1 = 7500, T = 2.0, k = Math.log(f1 / f0);
        for (int i = 0; i < RATE * 2; i++) {
            double t = (double) i / RATE;
            double ph = 2 * Math.PI * f0 * T / k * (Math.exp(t / T * k) - 1);
            x[i] = (short) (Math.sin(ph) * 32768 * 0.25);
        }
        java.util.Random r = new java.util.Random(1);
        double b0 = 0, b1 = 0, b2 = 0;
        for (int i = (int) (RATE * 2.5); i < (int) (RATE * 17.5); i++) {
            double w = r.nextGaussian();
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            double pink = (b0 + b1 + b2 + w * 0.1848) * 0.11;
            double t = (double) (i - (int) (RATE * 2.5)) / RATE;
            if (t % 0.6 < 0.4) {
                x[i] = (short) Math.max(-32767, Math.min(32767, pink * 32768 * 0.18));
            }
        }
        return x;
    }

    void echoTest(final int vol) {
        if (running || loop != null) {
            log("busy");
            return;
        }
        running = true;
        final int n = ++session;
        echoBtn.setEnabled(false);
        loop = new Thread(() -> {
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            am.setSpeakerphoneOn(true);
            if (vol >= 0) {
                am.setStreamVolume(AudioManager.STREAM_VOICE_CALL, vol, 0);
            }
            log("echo test " + n + ": voice call volume "
                    + am.getStreamVolume(AudioManager.STREAM_VOICE_CALL) + "/"
                    + am.getStreamMaxVolume(AudioManager.STREAM_VOICE_CALL));
            short[] x = stimulus();
            try {
                saveWav(new File(getExternalFilesDir(null), "echo_ref.wav"), x, x.length);
                echoTake(MediaRecorder.AudioSource.UNPROCESSED, "echo_raw_" + n, x);
                echoTake(MediaRecorder.AudioSource.VOICE_COMMUNICATION, "echo_vc_" + n, x);
            } catch (Exception e) {
                log("error " + e);
            }
            am.setSpeakerphoneOn(false);
            am.setMode(AudioManager.MODE_NORMAL);
            runOnUiThread(() -> {
                loop = null;
                running = false;
                echoBtn.setEnabled(true);
                log("echo test " + n + " done");
            });
        });
        loop.start();
    }

    /* Records source while x plays, from 0.5 s before it to 1 s after */
    void echoTake(int source, String name, short[] x) throws Exception {
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord rec = new AudioRecord(source, RATE, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT, min * 4);
        AudioTrack trk = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setTransferMode(AudioTrack.MODE_STATIC)
                .setBufferSizeInBytes(x.length * 2).build();
        trk.write(x, 0, x.length);
        int len = x.length + RATE * 3 / 2;
        short[] all = new short[len];
        int total = 0;
        try {
            rec.startRecording();
            int lead = RATE / 2;
            boolean started = false;
            while (total < len) {
                if (!started && total >= lead) {
                    trk.play();
                    started = true;
                    log(name + ": rec routed to " + (rec.getRoutedDevice() != null
                            ? rec.getRoutedDevice().getType() : -1)
                            + ", play routed to " + (trk.getRoutedDevice() != null
                            ? trk.getRoutedDevice().getType() : -1));
                }
                int got = rec.read(all, total, Math.min(min / 2, len - total));
                if (got <= 0) {
                    log(name + ": read " + got);
                    break;
                }
                total += got;
            }
            rec.stop();
            trk.stop();
        } finally {
            rec.release();
            trk.release();
        }
        saveWav(new File(getExternalFilesDir(null), name + ".wav"), all, total);
        log("saved " + name + ".wav (" + total + " samples)");
    }

    static void saveWav(File f, short[] x, int n) throws Exception {
        try (RandomAccessFile out = new RandomAccessFile(f, "rw")) {
            out.setLength(0);
            out.write(new byte[44]);
            byte[] bb = new byte[n * 2];
            for (int i = 0; i < n; i++) {
                bb[2 * i] = (byte) x[i];
                bb[2 * i + 1] = (byte) (x[i] >> 8);
            }
            out.write(bb);
            writeHeader(out, n);
        }
    }

    void toggle(boolean sco) {
        if (running || loop != null) {
            stopAll();
            return;
        }
        useSco = sco;
        running = true;
        am.setMode(AudioManager.MODE_IN_COMMUNICATION);
        if (sco) {
            log("starting SCO, isBluetoothScoAvailableOffCall="
                    + am.isBluetoothScoAvailableOffCall());
            am.startBluetoothSco();
            am.setBluetoothScoOn(true);
        } else {
            startLoop();
        }
        scoBtn.setText("Stop");
        plainBtn.setText("Stop");
    }

    /* Stop records; the loop then plays the take back, and only after
     * that do SCO and the call mode go, off the UI thread */
    void stopAll() {
        running = false;
        scoBtn.setEnabled(false);
        plainBtn.setEnabled(false);
        final Thread t = loop;
        new Thread(() -> {
            if (t != null) {
                try { t.join(); } catch (InterruptedException e) { }
            }
            runOnUiThread(() -> {
                loop = null;
                if (useSco) {
                    am.setBluetoothScoOn(false);
                    am.stopBluetoothSco();
                }
                am.setMode(AudioManager.MODE_NORMAL);
                scoBtn.setText("Start SCO loopback");
                plainBtn.setText("Start loopback without SCO");
                scoBtn.setEnabled(true);
                plainBtn.setEnabled(true);
                log("stopped");
            });
        }).start();
    }

    void startLoop() {
        final int n = ++session;
        loop = new Thread(() -> runLoop(n));
        loop.start();
    }

    void runLoop(int n) {
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord rec = new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, min * 4);
        AudioTrack trk = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(min * 4).build();
        File f = new File(getExternalFilesDir(null),
                (useSco ? "sco_" : "plain_") + n + ".wav");
        short[] buf = new short[min / 2];
        short[] all = new short[MAX_SAMPLES];
        int total = 0;
        try (RandomAccessFile out = new RandomAccessFile(f, "rw")) {
            out.setLength(0);
            out.write(new byte[44]);
            rec.startRecording();
            log("loop " + n + " rec state " + rec.getRecordingState()
                    + " routed to " + (rec.getRoutedDevice() != null
                        ? rec.getRoutedDevice().getType() : -1));
            long lastLog = 0;
            byte[] bb = new byte[buf.length * 2];
            while (running) {
                int got = rec.read(buf, 0, buf.length);
                if (got <= 0) {
                    log("read " + got);
                    break;
                }
                int peak = 0;
                for (int i = 0; i < got; i++) {
                    short s = buf[i];
                    peak = Math.max(peak, Math.abs((int) s));
                    bb[2 * i] = (byte) s;
                    bb[2 * i + 1] = (byte) (s >> 8);
                    if (total + i < MAX_SAMPLES) all[total + i] = s;
                }
                out.write(bb, 0, got * 2);
                total += got;
                long now = System.currentTimeMillis();
                if (now - lastLog > 1000) {
                    lastLog = now;
                    final int p = peak;
                    log(String.format("recording: peak %.1f dBFS, %d s",
                            20 * Math.log10(Math.max(p, 1) / 32768.0), total / RATE));
                }
            }
            rec.stop();
            writeHeader(out, total);
            int n2 = Math.min(total, MAX_SAMPLES);
            log("playing back " + n2 / RATE + " s");
            trk.play();
            log("playback routed to " + (trk.getRoutedDevice() != null
                    ? trk.getRoutedDevice().getType() : -1));
            for (int off = 0; off < n2; ) {
                int w = trk.write(all, off, Math.min(4096, n2 - off));
                if (w <= 0) break;
                off += w;
            }
            Thread.sleep(500);
            trk.stop();
        } catch (Exception e) {
            log("error " + e);
        } finally {
            rec.release();
            trk.release();
        }
        log("saved " + f + " (" + total / RATE + " s)");
    }

    static void writeHeader(RandomAccessFile out, long samples) throws Exception {
        long data = samples * 2;
        out.seek(0);
        out.writeBytes("RIFF");
        out.writeInt(Integer.reverseBytes((int) (36 + data)));
        out.writeBytes("WAVEfmt ");
        out.writeInt(Integer.reverseBytes(16));
        out.writeShort(Short.reverseBytes((short) 1));
        out.writeShort(Short.reverseBytes((short) 1));
        out.writeInt(Integer.reverseBytes(RATE));
        out.writeInt(Integer.reverseBytes(RATE * 2));
        out.writeShort(Short.reverseBytes((short) 2));
        out.writeShort(Short.reverseBytes((short) 16));
        out.writeBytes("data");
        out.writeInt(Integer.reverseBytes((int) data));
    }

    @Override
    protected void onDestroy() {
        running = false;
        unregisterReceiver(scoReceiver);
        super.onDestroy();
    }
}
