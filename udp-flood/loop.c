#include "./loop.h"
#include "./logger.h"
#include <assert.h>

static void loop_dump_handle(uv_handle_t *handle, void *data);

void loop_term(uv_loop_t *loop, unsigned int index) {
  assert(NULL != loop);

  int err = uv_loop_close(loop);

  int retry = 0;
  for (retry = 0; UV_EBUSY == err && retry < 1000; ++retry) {
    uv_run(loop, UV_RUN_NOWAIT);
    err = uv_loop_close(loop);
  }

  if (UV_EBUSY == err) {
    logger_print_error("#%d: Found unclosed handles:\n", index);
    uv_walk(loop, loop_dump_handle, (void *)(uintptr_t)index);
  }

  uv_stop(loop);
}

int loop_run(uv_loop_t *loop) {
  assert(NULL != loop);

  return uv_run(loop, UV_RUN_DEFAULT);
}

void loop_stop(uv_loop_t *loop) {
  assert(NULL != loop);

  uv_stop(loop);
}

static void loop_dump_handle(uv_handle_t *handle, void *data) {
#define XX(uc, lc)                                                                                                             \
  case UV_##uc:                                                                                                                \
    logger_print_error("#%d:    Unclosed handle %s\n", #uc, (unsigned int)(uintptr_t)data);                                    \
    break;

  switch (handle->type) {
    UV_HANDLE_TYPE_MAP(XX)

  default:
    logger_print_error("#%d:    Unknown unclosed handle type\n", (unsigned int)(uintptr_t)data);
    break;
  }

#undef XX
}
