/*
 * Copyright (C) 2026 The LineageOS Project
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
#ifndef MOCHA_SYSFS_NODE_H
#define MOCHA_SYSFS_NODE_H

/*
 * Writing to sysfs, for the services on this board that steer the hardware
 * through it -- which is most of them.
 *
 * There were three ways of doing this before, one per service, and each was
 * wrong differently. The power HAL wrote through an ofstream and looked at
 * nothing afterwards, so a write that did not happen was indistinguishable
 * from one that did. The lights HAL keeps seven streams open for the life of
 * the process: a stream that fails once sets failbit and keeps it, and every
 * write after that quietly does nothing until the service is restarted. The
 * vibrator had the same, and that is how it came to be written down here.
 *
 * So: open, write, close, say so if it did not work. A sysfs file is a few
 * bytes in a driver, not a socket -- opening one per write costs less than
 * any of the failure modes above.
 *
 * Header-only and inline on purpose. ALOGE then resolves against the LOG_TAG
 * of whoever included this, so a failure is signed by the service that caused
 * it rather than by a helper nobody can place.
 */

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <log/log.h>

namespace mocha {
namespace sysfs {

/* Raw bytes, for the nodes that take a structure rather than a number. */
inline bool write(const std::string& path, const void* data, size_t length) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);

    if (fd < 0) {
        ALOGE("cannot open %s: %s", path.c_str(), strerror(errno));
        return false;
    }

    ssize_t written = ::write(fd, data, length);
    int saved = errno;

    close(fd);

    if (written != static_cast<ssize_t>(length)) {
        ALOGE("cannot write %s: %s", path.c_str(), strerror(saved));
        return false;
    }
    return true;
}

inline bool write(const std::string& path, const std::string& value) {
    return write(path, value.data(), value.size());
}

inline bool write(const std::string& path, int value) {
    char buf[16];
    int length = snprintf(buf, sizeof(buf), "%d\n", value);

    return write(path, buf, length);
}

/*
 * Whether a write would be allowed, asked without performing one.
 *
 * For the services that must answer whether the hardware supports something
 * before being asked to do it -- and which should answer from the node rather
 * than with a constant.
 */
inline bool writable(const std::string& path) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);

    if (fd < 0) return false;

    close(fd);
    return true;
}

}  // namespace sysfs
}  // namespace mocha

#endif  // MOCHA_SYSFS_NODE_H
