#pragma once

#include <uv.h>

extern void loop_term(uv_loop_t *loop, unsigned int index);

extern int loop_run(uv_loop_t *loop);
extern void loop_stop(uv_loop_t *loop);
