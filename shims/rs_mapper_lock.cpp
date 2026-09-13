/*
 * The GraphicBufferMapper::lock that libnvRSDriver.so was compiled against.
 *
 * Somewhere after this blob was built the method gained two parameters:
 *
 *     status_t lock(buffer_handle_t handle, uint32_t usage, const Rect& bounds,
 *                   void** vaddr,
 *                   int32_t* outBytesPerPixel = nullptr,
 *                   int32_t* outBytesPerStride = nullptr);
 *
 * They are optional to a caller that recompiles, and invisible to one that
 * does not: the mangled name changed, so a 2015 blob asking for the four
 * parameter form finds nothing. That is the whole of the incompatibility --
 * the arguments it does pass mean exactly what they always meant, and the two
 * it cannot pass are outputs it never wanted.
 *
 * So the old name is defined here and forwards, leaving both new parameters
 * at their defaults. Declared extern "C" with the mangled name and an
 * explicit this, since the overload no longer exists to be defined as a
 * member.
 *
 * The linker attaches this file to libnvRSDriver.so alone, through
 * TARGET_LD_SHIM_LIBS in BoardConfig.mk. Nothing else on the board asks for
 * the old signature, and nothing else should be given it.
 */

#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

extern "C" android::status_t
_ZN7android19GraphicBufferMapper4lockEPK13native_handlejRKNS_4RectEPPv(
        android::GraphicBufferMapper *mapper, buffer_handle_t handle,
        uint32_t usage, const android::Rect &bounds, void **vaddr) {
    return mapper->lock(handle, usage, bounds, vaddr);
}
