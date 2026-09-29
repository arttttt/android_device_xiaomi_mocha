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

#include <malloc.h>
#include <stdlib.h>

/*
 * libglcore grows a buffer while it creates a context one fixed step at a
 * time:
 *
 *   p = realloc(p, 0x1020 * ++n);
 *
 * and takes it to about 2.7 MB. jemalloc, bionic's allocator through Q,
 * extends a large block where it lies, so that cost nothing. R's scudo
 * maps every block past 256 KB on its own and moves it whenever it has to
 * grow: a fresh mapping, a copy of everything so far, an unmap, some 660
 * times over. Every app's first eglCreateContext took 10 to 17 seconds,
 * with the window left white until it returned.
 *
 * The blob's import of realloc is renamed to glrealc in its .dynstr (see
 * shield-renamed in the vendor tree), and it keeps the version it asked
 * for, glrealc@LIBC. Growing by half of what a block already holds turns
 * those steps into a few dozen moves; a request that still fits keeps the
 * block where it is.
 */
extern "C" void* glrealc(void* ptr, size_t size) {
    if (ptr != nullptr && size != 0) {
        size_t usable = malloc_usable_size(ptr);
        // Fits, and shrinking would give back less than half: keep it.
        if (size <= usable && size > usable / 2)
            return ptr;
        size_t grown = usable + usable / 2;
        if (size > usable && grown > size) {
            void* p = realloc(ptr, grown);
            if (p != nullptr)
                return p;
        }
    }
    return realloc(ptr, size);
}
