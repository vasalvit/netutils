#include "./random.h"
#include <stdint.h>
#include <uv.h>

static __declspec(thread) unsigned int s_u = 0, s_v = 0;

unsigned int random(void) {
  if (0 == s_v && 0 == s_u) {
    s_v = (unsigned int)((uintptr_t)uv_os_getpid() * (uintptr_t)uv_thread_self());
    s_u = (unsigned int)(uintptr_t)uv_hrtime();
  }

  s_v = 36969 * (s_v & 65535) + (s_v >> 16);
  s_u = 18000 * (s_u & 65535) + (s_u >> 16);

  return (s_v << 16) + (s_u & 65535);
}
