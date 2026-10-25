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
#ifndef MOCHA_POWER_PROVIDER_WATCH_H
#define MOCHA_POWER_PROVIDER_WATCH_H

#include <functional>
#include <thread>
#include <utility>

namespace aidl::android::hardware::power::impl::mocha {

/*
 * Watches the camera provider's state as init reports it, and says when the
 * provider has stopped running.
 *
 * The camera's hints arrive over setMode, which carries no binder of the
 * caller's: nothing to link to death, so a provider that crashes mid-stream
 * would leave its floor standing until the next boot. Init knows at once --
 * it sets init.svc.<name> to "restarting" -- and a property can be waited
 * on, so this waits on it. Every value but "running" means the provider
 * has no stream any more.
 *
 * The thread is detached: it waits for ever, as the service does.
 */
class ProviderWatch {
  public:
    explicit ProviderWatch(std::function<void()> gone);

  private:
    void loop();

    std::function<void()> mGone;
};

}  // namespace aidl::android::hardware::power::impl::mocha

#endif  // MOCHA_POWER_PROVIDER_WATCH_H
