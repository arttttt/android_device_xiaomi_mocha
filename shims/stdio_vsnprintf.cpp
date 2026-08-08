#include <unistd.h>
#include <dlfcn.h>
#include <stdlib.h>

#include <cutils/log.h>


static typeof(vsnprintf) *vsnprintf_real = NULL;

extern "C" int haxprintf(char* s, size_t n, const char* fmt, va_list ap) {
  /* While snprintf(3) specifies size_t stdio uses an int internally */
  if (n > INT_MAX)
    n = INT_MAX;

  if (!vsnprintf_real)
    vsnprintf_real = (typeof(vsnprintf)*)dlsym(RTLD_DEFAULT, "vsnprintf");

  if (vsnprintf_real)
    return vsnprintf_real(s, n, fmt, ap);

  /*
   * If we could not find vsnprintf, print an error and bail out,
   * we should not pretend everything's fine and return default_value.
   */
  ALOGE("Could not find vsnprintf, aborting");
  abort();

  return 0;
}
