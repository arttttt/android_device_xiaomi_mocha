package org.mocha.audiotest;

/* The native part: AudioTrack and AudioRecord as libaudioclient has them */
final class NativeAudio {
    static {
        System.loadLibrary("mochaaudiotest_jni");
    }

    private NativeAudio() {
    }

    /* A line on libaudioclient as this process reaches it */
    static native String probe();

    /* A sine through a DIRECT output: rate Hz, 16 or 24 bit, freq Hz for
     * secs seconds; returns what the track got */
    static native String playDirect(int rate, int bits, int freq, int secs);
}
