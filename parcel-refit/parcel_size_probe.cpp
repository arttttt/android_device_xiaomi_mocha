/*
 * The size of android::Parcel, asked of the compiler that builds this release.
 *
 * Several NVIDIA blobs construct a Parcel in a stack slot whose size was
 * decided when the blob was compiled.  Parcel has grown since -- 48 bytes in
 * KitKat, 52 in Lollipop, 60 in Q -- and libbinder writes the whole of it, so
 * on a newer platform those constructors write past the slot and into the
 * caller's saved registers and stack guard.
 *
 * refit-parcels.py widens the slots, and needs to know what to widen them to.
 * A number written down here would be a number to get wrong on the next
 * release, so nothing is written down: this array is sized by the platform's
 * own header through the platform's own compiler, and the tool reads its size
 * back out of the object file.  Port to a new Android and the number follows
 * on its own.
 *
 * Nothing links this and nothing calls it.  build/tasks/parcel-refit.mk
 * depends on the built archive so that it exists to be measured.
 */

#include <binder/Parcel.h>

extern "C" char __parcel_size_probe[sizeof(android::Parcel)];
char __parcel_size_probe[sizeof(android::Parcel)];
