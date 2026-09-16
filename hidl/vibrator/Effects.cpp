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

#include "Effects.h"

#include "Ringtones.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

/*
 * Why every effect here is three durations at one amplitude.
 *
 * The motor is an eccentric rotating mass, not a linear actuator, and that
 * decides the whole shape of this table. Both dials were measured by hand on
 * the device, which is the only instrument there is for this:
 *
 *   - Duration is a strong dial. It rises evenly and audibly to the hand over
 *     the whole useful range.
 *   - Amplitude is a weak one. At 55 ms the steps 127/110/90/70/50 can be
 *     told apart, but the top two read as nearly the same; at 35 ms only the
 *     top of the scale has any body left, and the bottom is limp. 110 at
 *     55 ms is plainly stronger than 127 at 35 -- length beats level.
 *
 * Other boards do the opposite: LG, nubia and Pixel fix the duration per
 * effect and vary the amplitude for LIGHT/MEDIUM/STRONG. Their actuators are
 * linear, where amplitude is the honest dial. Copying that convention here
 * was tried and abandoned -- the three levels stopped being three.
 *
 * So: duration carries strength, amplitude stays at the maximum, and the
 * effects are told apart by how long they run. Their ranges overlap -- a
 * STRONG pop outlasts a LIGHT click -- and that is fine. The framework never
 * asks which effect a buzz was; it asks for a named effect at a named
 * strength, and gets it.
 *
 * Around 20 ms a pulse stops being definite and starts being faint. It does
 * not vanish -- twelve is still there, just weak -- but the mass needs about
 * that long to reach speed, so anything shorter is a fraction of a pulse and
 * feels like one. Where an effect has to be noticed, 20 is where to stop
 * shortening it; where it only has to be present, less will do.
 *
 * That behaviour belongs to the motor and not to how the amplifier is driven
 * -- closing the chip's feedback loop was tried, in both orders to rule out
 * the hand tiring, and made the short pulses weaker rather than sharper.
 * Driving a braking pulse of reversed polarity after the main one was tried
 * too, at 3, 5 and 10 ms: the long tails blunted the ending, and the short
 * ones could not be told from no tail at all. Neither is used.
 */

/* The tick: the shortest pulse that still arrives as a definite event rather
 * than a hint. Its three strengths are one number because shortening it makes
 * it vague rather than lighter, and quieting it does the same. */
static constexpr Effects::Lengths TICK_MS = {20, 20, 20};

/*
 * The texture tick: "a soft tick effect meant to be played as a texture",
 * expected to arrive "multiple times in quick succession" so a finger reads a
 * surface under it.
 *
 * Two lengths, because the same request means two different things depending
 * on how fast it arrives.
 *
 * Dragged quickly they arrive about every 23 ms at the fastest and 30 on
 * average -- measured from the HAL rather than guessed, after a guess of
 * sixty a second proved to be twice the truth.
 *
 * Eight rather than ten, and the difference is not the pulse but what the
 * driver wraps around it. Playing a pattern takes the amplifier out of
 * standby and puts it back, and each of those costs four to five
 * milliseconds, so a ten-millisecond pulse occupies the motor for nearly
 * twenty and leaves almost nothing of the gap. Eight fits with room to
 * spare, and that room is what the finger reads as a surface: the mass sags
 * and is caught again. Six was tried first and is a little thin.
 *
 * The honest fix is in the driver, which need not visit standby between
 * patterns that follow each other closely. Until it does, this is sized for
 * the driver we have.
 *
 * Dragged slowly, each arrives alone, and ten on its own is barely there. So
 * a lone one is given the tick's length, which is what a lone soft tick
 * should be. The two cases do not compete for one number, because the caller
 * tells them apart by when it asks.
 */
static constexpr Effects::Lengths TEXTURE_TICK_REPEATED_MS = {8, 8, 8};
static constexpr Effects::Lengths TEXTURE_TICK_SINGLE_MS = {20, 20, 20};

/* The pop: "a short, quick burst". A little more body than a tick and still
 * well under a click. */
static constexpr Effects::Lengths POP_MS = {22, 26, 30};

/* The heavy click: "a sharp striking sensation, like a click but stronger".
 * Above the click's range at every strength. */
static constexpr Effects::Lengths HEAVY_CLICK_MS = {55, 65, 75};

/*
 * The thud: "a solid feeling bump, like the depression of a heavy mechanical
 * button". The longest of the family, and the heaviest for it.
 *
 * A thud should also be duller than a click, and that part this board cannot
 * do: dullness would come from a shaped decay, and the one tool for shaping
 * -- driving the mass the other way -- was measured and does nothing the hand
 * can hear. So it differs from the heavy click only in weight. If someone has
 * ears and time, a two-step decay is the obvious next thing to try.
 */
static constexpr Effects::Lengths THUD_MS = {70, 85, 100};

/* The click, kept as it was found by hand and unchanged since. Also what a
 * ringtone's beats are measured against. */
const Effects::Lengths Effects::CLICK_MS = {27, 35, 48};

/*
 * The silence inside a double click.
 *
 * What makes two pulses read as one gesture rather than two taps. 80 and 100
 * were tried against it at the longest pulse: both still read as a double,
 * and both are softer for it. 60 is the one that stays sharp, which is what
 * a double click is for.
 */
static constexpr uint8_t DOUBLE_CLICK_GAP_MS = 60;

uint8_t Effects::pick(const Lengths& lengths, EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:  return lengths.light;
        case EffectStrength::MEDIUM: return lengths.medium;
        case EffectStrength::STRONG: return lengths.strong;
    }
    return lengths.medium;
}

static Effects::Shape single(uint8_t lengthMs) {
    return {{{Actuator::MAX_STRENGTH, lengthMs}}, lengthMs};
}

Effects::Shape Effects::of(Effect effect, EffectStrength strength, Pace pace) {
    const uint8_t full = Actuator::MAX_STRENGTH;

    switch (effect) {
        case Effect::CLICK:
            return single(pick(CLICK_MS, strength));

        case Effect::DOUBLE_CLICK: {
            const uint8_t length = pick(CLICK_MS, strength);

            return {{{full, length},
                     {0, DOUBLE_CLICK_GAP_MS},
                     {full, length}},
                    static_cast<uint32_t>(length) + DOUBLE_CLICK_GAP_MS + length};
        }

        case Effect::TICK:
            return single(pick(TICK_MS, strength));

        case Effect::TEXTURE_TICK:
            return single(pick(pace == Pace::REPEATED ? TEXTURE_TICK_REPEATED_MS
                                                     : TEXTURE_TICK_SINGLE_MS,
                               strength));

        case Effect::POP:
            return single(pick(POP_MS, strength));

        case Effect::HEAVY_CLICK:
            return single(pick(HEAVY_CLICK_MS, strength));

        case Effect::THUD:
            return single(pick(THUD_MS, strength));

        default:
            /* The fifteen ringtone slots, which are rhythms rather than
             * single pulses and live in their own unit. Anything that is not
             * one comes back empty and is answered as unsupported. */
            return Ringtones::of(effect, pick(CLICK_MS, strength));
    }
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
