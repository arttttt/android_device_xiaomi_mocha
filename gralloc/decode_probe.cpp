/*
 * Copyright (C) 2026 Artem Bambalov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Decodes one frame of a file and says what happened.
 *
 * The gallery is a poor instrument for this: it caches, it decides on its own
 * when a tile is worth drawing, and it swallows the reason when nothing comes
 * back. This asks the question directly -- a named component, no surface, the
 * same byte-buffer mode a thumbnail is taken in -- and prints the answer.
 *
 *     decode-probe <file> [component]
 *
 * Without a component the framework picks one. With a component the choice is
 * ours, which is what makes it possible to ask "does the software decoder work
 * on this file" rather than "did something somewhere produce a picture".
 */

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const int64_t kTimeoutUs = 50000;
const int kMaxRounds = 400;

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file> [component]\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    const char *want = (argc > 2) ? argv[2] : NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("%-28s  cannot open the file\n", path);
        return 1;
    }

    struct stat st;
    fstat(fd, &st);

    AMediaExtractor *ex = AMediaExtractor_new();
    media_status_t rc = AMediaExtractor_setDataSourceFd(ex, fd, 0, st.st_size);
    close(fd);

    if (rc != AMEDIA_OK) {
        printf("%-28s  the extractor refused it (%d)\n", path, rc);
        AMediaExtractor_delete(ex);
        return 1;
    }

    size_t tracks = AMediaExtractor_getTrackCount(ex);
    AMediaFormat *fmt = NULL;
    const char *mime = NULL;
    size_t track = 0;

    for (size_t i = 0; i < tracks; i++) {
        AMediaFormat *f = AMediaExtractor_getTrackFormat(ex, i);
        const char *m = NULL;
        if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) &&
            strncmp(m, "video/", 6) == 0) {
            fmt = f;
            mime = m;
            track = i;
            break;
        }
        AMediaFormat_delete(f);
    }

    if (fmt == NULL) {
        printf("%-28s  no video track\n", path);
        AMediaExtractor_delete(ex);
        return 1;
    }

    int32_t w = 0, h = 0;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);

    AMediaExtractor_selectTrack(ex, track);

    AMediaCodec *codec = want ? AMediaCodec_createCodecByName(want)
                              : AMediaCodec_createDecoderByType(mime);
    if (codec == NULL) {
        printf("%-28s %4dx%-4d  %-24s  no such component\n", path, w, h,
               want ? want : mime);
        AMediaFormat_delete(fmt);
        AMediaExtractor_delete(ex);
        return 1;
    }

    /* No surface: byte buffers, which is how a thumbnail is taken. */
    rc = AMediaCodec_configure(codec, fmt, NULL, NULL, 0);
    if (rc != AMEDIA_OK) {
        printf("%-28s %4dx%-4d  %-24s  configure failed (%d)\n", path, w, h,
               want ? want : mime, rc);
        AMediaCodec_delete(codec);
        AMediaFormat_delete(fmt);
        AMediaExtractor_delete(ex);
        return 1;
    }

    rc = AMediaCodec_start(codec);
    if (rc != AMEDIA_OK) {
        printf("%-28s %4dx%-4d  %-24s  start failed (%d)\n", path, w, h,
               want ? want : mime, rc);
        AMediaCodec_delete(codec);
        AMediaFormat_delete(fmt);
        AMediaExtractor_delete(ex);
        return 1;
    }

    bool sawInputEnd = false;
    int rounds = 0;

    for (; rounds < kMaxRounds; rounds++) {
        if (!sawInputEnd) {
            ssize_t in = AMediaCodec_dequeueInputBuffer(codec, kTimeoutUs);
            if (in >= 0) {
                size_t cap = 0;
                uint8_t *buf = AMediaCodec_getInputBuffer(codec,
                                                          (size_t)in, &cap);
                ssize_t got = AMediaExtractor_readSampleData(ex, buf, cap);
                if (got < 0) {
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, 0, 0,
                            AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    sawInputEnd = true;
                } else {
                    int64_t pts = AMediaExtractor_getSampleTime(ex);
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0,
                                                 (size_t)got, pts, 0);
                    AMediaExtractor_advance(ex);
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t out = AMediaCodec_dequeueOutputBuffer(codec, &info, kTimeoutUs);

        if (out >= 0) {
            printf("%-28s %4dx%-4d  %-24s  DECODED, %d bytes after %d rounds\n",
                   path, w, h, want ? want : mime, info.size, rounds);
            AMediaCodec_releaseOutputBuffer(codec, (size_t)out, false);
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            AMediaFormat_delete(fmt);
            AMediaExtractor_delete(ex);
            return 0;
        }

        if (out == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
            out == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED ||
            out == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            continue;
        }

        printf("%-28s %4dx%-4d  %-24s  FAILED, dequeue said %zd\n", path, w, h,
               want ? want : mime, out);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        AMediaFormat_delete(fmt);
        AMediaExtractor_delete(ex);
        return 1;
    }

    printf("%-28s %4dx%-4d  %-24s  NO FRAME in %d rounds\n", path, w, h,
           want ? want : mime, kMaxRounds);

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(fmt);
    AMediaExtractor_delete(ex);
    return 1;
}
