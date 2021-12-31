#pragma once

#include <stdbool.h>
#include <uv.h>

typedef struct _worker_t *worker_p;

extern worker_p worker_create_in_loop(uv_loop_t *loop, unsigned int index);
extern worker_p worker_create_in_thread(unsigned int index);

extern void worker_destroy(worker_p worker);
