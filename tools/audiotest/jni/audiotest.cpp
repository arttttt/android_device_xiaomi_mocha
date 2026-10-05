/*
 * Audio test, native part: what the SDK cannot ask for, through
 * libaudioclient.
 */
#define LOG_TAG "AudioTest"

#include <jni.h>

#include <media/AudioTrack.h>
#include <utils/Log.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using android::AudioTrack;
using android::OK;
using android::sp;
using android::status_t;

static std::string hex(unsigned v)
{
    char b[16];

    snprintf(b, sizeof(b), "0x%x", v);
    return b;
}

static jstring probe(JNIEnv *env, jclass)
{
    size_t frames = 0;
    const status_t ret = AudioTrack::getMinFrameCount(&frames,
                                                      AUDIO_STREAM_MUSIC,
                                                      48000);
    const std::string line = "libaudioclient: min frame count at 48 kHz " +
            std::to_string(frames) + ", status " + std::to_string(ret);

    ALOGI("%s", line.c_str());
    return env->NewStringUTF(line.c_str());
}

/*
 * A sine played through a DIRECT output, as a hi-res player asks for one:
 * AUDIO_OUTPUT_FLAG_DIRECT, the media usage, the stream's own rate and
 * format, so that AudioFlinger neither mixes nor resamples it. Stereo,
 * freq Hz at -12 dBFS for secs seconds; bits 16 for PCM_16_BIT, 24 for
 * PCM_24_BIT_PACKED. Returns what the track got: the flags (DIRECT, or
 * not), the device, the rate and the frames written; the output it
 * opened is in the HAL's dump.
 */
static jstring playDirect(JNIEnv *env, jclass, jint rate, jint bits,
                          jint freq, jint secs)
{
    const audio_format_t format = bits == 24 ? AUDIO_FORMAT_PCM_24_BIT_PACKED
                                             : AUDIO_FORMAT_PCM_16_BIT;
    const size_t bytes_per_sample = bits == 24 ? 3 : 2;
    const size_t frame_bytes = 2 * bytes_per_sample;
    audio_attributes_t attr = AUDIO_ATTRIBUTES_INITIALIZER;
    std::string line;

    attr.usage = AUDIO_USAGE_MEDIA;
    attr.content_type = AUDIO_CONTENT_TYPE_MUSIC;

    sp<AudioTrack> track = new AudioTrack();
    status_t ret = track->set(AUDIO_STREAM_DEFAULT, rate, format,
                              AUDIO_CHANNEL_OUT_STEREO, 0,
                              AUDIO_OUTPUT_FLAG_DIRECT, nullptr, nullptr, 0,
                              nullptr, false, AUDIO_SESSION_ALLOCATE,
                              AudioTrack::TRANSFER_SYNC, nullptr,
                              AUDIO_UID_INVALID, -1, &attr);
    if (ret == OK)
        ret = track->initCheck();
    if (ret != OK) {
        line = "direct " + std::to_string(rate) + " Hz " +
               std::to_string(bits) + " bit: set failed, status " +
               std::to_string(ret);
        ALOGE("%s", line.c_str());
        return env->NewStringUTF(line.c_str());
    }

    /* A second of the sine at a time; the phase runs on across them */
    const size_t chunk_frames = rate;
    std::vector<uint8_t> buf(chunk_frames * frame_bytes);
    const double step = 2.0 * M_PI * freq / rate;
    const double amp = 0.25;
    double phase = 0;
    size_t written = 0;

    track->start();
    for (int s = 0; s < secs; s++) {
        uint8_t *p = buf.data();
        for (size_t i = 0; i < chunk_frames; i++, phase += step) {
            const double v = amp * sin(phase);
            if (bits == 24) {
                const int32_t x = lrint(v * 8388607.0);
                for (int ch = 0; ch < 2; ch++) {
                    *p++ = x & 0xff;
                    *p++ = (x >> 8) & 0xff;
                    *p++ = (x >> 16) & 0xff;
                }
            } else {
                const int16_t x = lrint(v * 32767.0);
                for (int ch = 0; ch < 2; ch++) {
                    *p++ = x & 0xff;
                    *p++ = (x >> 8) & 0xff;
                }
            }
        }
        const ssize_t n = track->write(buf.data(), buf.size());
        if (n < 0) {
            line = "write failed " + std::to_string(n) + "; ";
            break;
        }
        written += n / frame_bytes;
        if (s == 0) {
            const audio_output_flags_t flags = track->getFlags();
            line += "direct " + std::to_string(rate) + " Hz " +
                    std::to_string(bits) + " bit: flags " + hex(flags) +
                    ((flags & AUDIO_OUTPUT_FLAG_DIRECT) ? " (DIRECT)" : " (not direct)") +
                    ", device " + std::to_string(track->getRoutedDeviceId()) +
                    ", track rate " + std::to_string(track->getSampleRate()) + "; ";
        }
    }
    track->stop();
    line += "frames written " + std::to_string(written);
    ALOGI("%s", line.c_str());
    return env->NewStringUTF(line.c_str());
}

static const JNINativeMethod methods[] = {
    { "probe", "()Ljava/lang/String;", reinterpret_cast<void *>(probe) },
    { "playDirect", "(IIII)Ljava/lang/String;",
      reinterpret_cast<void *>(playDirect) },
};

jint JNI_OnLoad(JavaVM *vm, void *)
{
    JNIEnv *env;

    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    jclass cls = env->FindClass("org/mocha/audiotest/NativeAudio");
    if (cls == nullptr ||
        env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
