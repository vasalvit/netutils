#pragma once

#include "./atomic.h"
#include <stdint.h>

extern custom_atomic_size_t g_stats_sent_bytes;
extern custom_atomic_size_t g_stats_sent_operations;

extern const char *g_arg_address;
extern bool g_arg_is_ipv4;
extern int g_arg_port_min, g_arg_port_max;
extern int g_arg_size_min, g_arg_size_max;
extern int g_arg_timeout_ms;
extern int g_arg_workers_count;
