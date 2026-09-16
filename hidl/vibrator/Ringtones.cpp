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

#define LOG_TAG "android.hardware.vibrator@1.3-service.mocha"

#include "Ringtones.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

/*
 * What a ringtone is, and why these fifteen are ours to invent.
 *
 * The interface says so itself: the slots "may correspond with the device's
 * ringtone audio, or may just be a pattern that can be played as a ringtone
 * with any audio, depending on the device". There is no canonical set. The
 * compliance test asks only that each effect answer with a length or refuse,
 * and the compatibility document says nothing about how any of them should
 * feel.
 *
 * Where they are implemented at all, they are rhythms. The Pixel HAL carries
 * twelve named patterns from the Immersion library the amplifier family ships
 * -- The_big_adventure, Crackle, Flutterby, Monkey_around -- written as
 * sequences of pauses and library effect numbers, like "20.100, 23.100,
 * 23.80, 23.60, 892". It implements twelve and refuses the last three.
 *
 * We cannot replay those. Their "23.80" is effect 23 of a read-only waveform
 * library at 80% strength, and that library lives in the DRV2605. This board
 * has the DRV2604 -- confirmed by its status register, device id 4 -- whose
 * ROM is instead 2 kB of empty RAM that nothing here loads. What does carry
 * over is the shape of the idea: a rhythm is pauses between pulses, and a
 * figure like "23.100, 23.80, 23.60" is one beat repeated while fading.
 *
 * So these are fifteen rhythms of our own, built to be told apart from each
 * other: how many beats, how they are spaced, whether they speed up, slow
 * down or fade. All fifteen are answered -- three more than the Pixel does.
 */

/*
 * One beat of a rhythm.
 *
 * A pulse is measured as a percentage of the effect's pulse length, so a
 * whole rhythm scales with LIGHT, MEDIUM and STRONG while keeping its shape.
 * A beat with no pulse is a silence, written as the milliseconds the rhythm
 * wants there -- which is a floor away from what it finally gets, because a
 * silence following a long beat is widened to clear that beat's coast. See
 * restAfter() below.
 */
struct Beat {
    uint8_t pulsePct;   /* 0 means this beat is a silence */
    uint8_t silenceMs;  /* read only when pulsePct is 0 */
};

static constexpr Beat pulse(uint8_t pct) { return {pct, 0}; }
static constexpr Beat rest(uint8_t ms) { return {0, ms}; }

/* A single heavy thump with room after it. The sparsest of the set. */
static constexpr Beat RINGTONE_1[] = {pulse(200), rest(255), rest(255)};

/* Two beats, close. */
static constexpr Beat RINGTONE_2[] = {pulse(100), rest(90), pulse(100)};

/* Three even beats. */
static constexpr Beat RINGTONE_3[] = {pulse(100), rest(90), pulse(100), rest(90), pulse(100)};

/* Long then short. */
static constexpr Beat RINGTONE_4[] = {pulse(180), rest(80), pulse(80)};

/* Short then long -- the mirror of the one above, and quite unlike it. */
static constexpr Beat RINGTONE_5[] = {pulse(80), rest(80), pulse(180)};

/* Two short and a long, the rhythm of a question. */
static constexpr Beat RINGTONE_6[] = {pulse(80), rest(70), pulse(80), rest(70), pulse(180)};

/* A long and two short, the rhythm of an answer. */
static constexpr Beat RINGTONE_7[] = {pulse(180), rest(70), pulse(80), rest(70), pulse(80)};

/* Four, evenly. */
static constexpr Beat RINGTONE_8[] = {pulse(100), rest(100), pulse(100), rest(100),
                                      pulse(100), rest(100), pulse(100)};

/* Speeding up. */
static constexpr Beat RINGTONE_9[] = {pulse(100), rest(160), pulse(100), rest(120),
                                      pulse(100), rest(90),  pulse(100), rest(70),
                                      pulse(100)};

/* Slowing down. */
static constexpr Beat RINGTONE_10[] = {pulse(100), rest(70),  pulse(100), rest(90),
                                       pulse(100), rest(120), pulse(100), rest(160),
                                       pulse(100)};

/* A triplet, a wait, and one more. */
static constexpr Beat RINGTONE_11[] = {pulse(100), rest(70), pulse(100), rest(70),
                                       pulse(100), rest(255), rest(45), pulse(140)};

/* Two close together and a long rest, like a heartbeat. */
static constexpr Beat RINGTONE_12[] = {pulse(120), rest(70), pulse(120), rest(255), rest(195)};

/* Heavy first, then two light ones trailing after it. */
static constexpr Beat RINGTONE_13[] = {pulse(200), rest(120), pulse(70), rest(70), pulse(70)};

/* Five in a hurry. */
static constexpr Beat RINGTONE_14[] = {pulse(80), rest(60), pulse(80), rest(60),
                                       pulse(80), rest(60), pulse(80), rest(60),
                                       pulse(80)};

/* Off the beat: one, a long gap, then two together. */
static constexpr Beat RINGTONE_15[] = {pulse(100), rest(200), pulse(100), rest(70), pulse(100)};

/*
 * The silence a beat has to be given before the next one can be heard.
 *
 * An eccentric mass does not stop when the drive does -- it coasts, and the
 * longer it was driven the longer it coasts. A pulse that arrives during that
 * coast is not heard as its own beat; it lands in the tail of the one before
 * and the pair smears into a single blur. Written by hand, the rests in these
 * rhythms ignored that, and the figures with a long beat followed by short
 * ones -- long-then-short, long-and-two-short, heavy-then-two-light -- all
 * ran together at the seam.
 *
 * So the rhythms give the spacing they want and this gives the floor under
 * it: a beat longer than the effect's own pulse is followed by at least twice
 * its length of silence. The rule is in one place because it is a property of
 * the motor rather than of any rhythm, and it scales with strength because it
 * is written against the beat rather than against a number of milliseconds.
 *
 * Beats at or below the base length coast little enough not to need it, which
 * is what keeps the quick figures quick.
 */
static uint8_t restAfter(uint8_t pulsePct, uint8_t pulseLengthMs, uint8_t askedMs) {
    if (pulsePct <= 100) return askedMs;

    const uint32_t floorMs = static_cast<uint32_t>(pulseLengthMs) * 2;

    if (floorMs >= 255) return 255;

    return askedMs > floorMs ? askedMs : static_cast<uint8_t>(floorMs);
}

struct Rhythm {
    const Beat* beats;
    size_t count;
};

#define RHYTHM(name) {name, sizeof(name) / sizeof(name[0])}

/* Indexed by the effect's distance from RINGTONE_1. */
static constexpr Rhythm RHYTHMS[] = {
    RHYTHM(RINGTONE_1),  RHYTHM(RINGTONE_2),  RHYTHM(RINGTONE_3),  RHYTHM(RINGTONE_4),
    RHYTHM(RINGTONE_5),  RHYTHM(RINGTONE_6),  RHYTHM(RINGTONE_7),  RHYTHM(RINGTONE_8),
    RHYTHM(RINGTONE_9),  RHYTHM(RINGTONE_10), RHYTHM(RINGTONE_11), RHYTHM(RINGTONE_12),
    RHYTHM(RINGTONE_13), RHYTHM(RINGTONE_14), RHYTHM(RINGTONE_15),
};

#undef RHYTHM

static constexpr size_t RHYTHM_COUNT = sizeof(RHYTHMS) / sizeof(RHYTHMS[0]);

Effects::Shape Ringtones::of(Effect effect, uint8_t pulseMs) {
    const uint32_t first = static_cast<uint32_t>(Effect::RINGTONE_1);
    const uint32_t asked = static_cast<uint32_t>(effect);

    if (asked < first || asked - first >= RHYTHM_COUNT) return {{}, 0};

    const Rhythm& rhythm = RHYTHMS[asked - first];

    Effects::Shape shape = {{}, 0};
    shape.steps.reserve(rhythm.count);

    /* What the beat before this one was, so a silence can be widened to clear
     * its coast. Zero until the first pulse. */
    uint8_t lastPct = 0;
    uint8_t lastMs = 0;

    for (size_t i = 0; i < rhythm.count; i++) {
        const Beat& beat = rhythm.beats[i];

        if (beat.pulsePct == 0) {
            const uint8_t silence = restAfter(lastPct, lastMs, beat.silenceMs);

            shape.steps.push_back({0, silence});
            shape.lengthMs += silence;
            lastPct = 0;
            continue;
        }

        /* A step's duration is one byte, and the longest pulse any rhythm
         * asks for is twice the longest click -- well inside it. */
        const uint8_t length =
                static_cast<uint8_t>(pulseMs * static_cast<uint32_t>(beat.pulsePct) / 100);

        shape.steps.push_back({Actuator::MAX_STRENGTH, length});
        shape.lengthMs += length;
        lastPct = beat.pulsePct;
        lastMs = length;
    }

    return shape;
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
