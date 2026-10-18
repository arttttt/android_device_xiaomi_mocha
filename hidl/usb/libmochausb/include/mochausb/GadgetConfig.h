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
#ifndef MOCHA_USB_GADGET_CONFIG_H
#define MOCHA_USB_GADGET_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

namespace mocha {
namespace usb {

/*
 * The gadget as configfs sees it: a directory of function instances, a
 * configuration built by linking some of them into it, and one file whose
 * contents decide whether any of it is on the bus.
 *
 * Only the composing is here. Whether the controller may be bound yet is a
 * question about daemons and belongs to FfsMonitor; what should be composed
 * is a question about the framework and belongs to Gadget.
 *
 * The skeleton itself -- the gadget, its strings, its configuration and every
 * function instance -- is made by init.tn8.usb.rc before the service starts,
 * because nothing in system/core creates it and the service is started too
 * late to be first.
 */
class GadgetConfig {
  public:
    /* Whether the endpoint files of every FunctionFS function belong to a
     * daemon that has described itself. Empty for a configuration with none. */
    std::vector<std::string> link(uint64_t functions);

    bool unlinkAll() const;

    /* Its identity on the bus, and the name the configuration answers with. */
    bool setIdentity(uint64_t functions) const;
    bool setConfigurationName(uint64_t functions) const;

    /* Whether the gadget answers a Windows host's request for Microsoft OS
     * descriptors. On for configurations with MTP or PTP. */
    bool setOsDescriptors(uint64_t functions) const;

    /* Writing the controller's name is what puts the gadget on the bus, and
     * writing "none" is what takes it off. */
    bool bind() const;
    void unbind() const;

    /* The controller's own word for the speed it is running at, as udc-core
     * spells it: "high-speed" and the like. Empty if it cannot be read. */
    std::string currentSpeed() const;

    /* What the gadget says it is before anything composes it. */
    void resetDeviceClass() const;

  private:
    bool linkOne(const char* function, int index) const;
    bool createFunction(const char* function) const;
    void setRndisAddresses() const;
    void removeFunction(const char* function) const;
};

}  // namespace usb
}  // namespace mocha

#endif  // MOCHA_USB_GADGET_CONFIG_H
