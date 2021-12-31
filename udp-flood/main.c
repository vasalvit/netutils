#include "./globals.h"
#include "./logger.h"
#include "./loop.h"
#include "./platform.h"
#include "./worker.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define VERSION "2.0"
#define YEARS "2021"
#define AUTHOR "Alexander Vasilevsky <vasalvit@gmail.com>"

#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_PORT 55555
#define DEFAULT_SIZE 4096
#define DEFAULT_TIMEOUT 0
#define DEFAULT_WORKERS 1

#define MINIMAL_PORT 1
#define MAXIMAL_PORT 65535
#define MINIMAL_SIZE 1
#define MAXIMAL_SIZE 4096
#define MINIMAL_TIMEOUT 0
#define MAXIMAL_TIMEOUT 60 * 60 * 1000
#define MINIMAL_WORKERS 1
#define MAXIMAL_WORKERS 1024

#undef countof
#define countof(_x) (sizeof((_x)) / sizeof((_x)[0]))

custom_atomic_size_t g_stats_sent_bytes = 0;
custom_atomic_size_t g_stats_sent_operations = 0;
static bool s_stats_raw = false;
static uint64_t s_stats_start_ns = 0, s_stats_prev_ns = 0;
static uint64_t s_stats_prev_sent_bytes = 0;
static uint64_t s_stats_prev_sent_operations = 0;

const char *g_arg_address = DEFAULT_ADDRESS;
bool g_arg_is_ipv4 = true;
int g_arg_port_min = DEFAULT_PORT, g_arg_port_max = DEFAULT_PORT;
int g_arg_size_min = DEFAULT_SIZE, g_arg_size_max = DEFAULT_SIZE;
int g_arg_timeout_ms = DEFAULT_TIMEOUT;
int g_arg_workers_count = DEFAULT_WORKERS;

typedef enum _parse_result_e {
  parse_result_exit,
  parse_result_show_help,
  parse_result_show_version,
  parse_result_continue,
} parse_result_e;

static parse_result_e parse_args(int argc, char **argv);

static void show_help(void);
static void show_version(void);

static void sigint_handler(uv_signal_t *sigint, int signum);
static void stats_handler(uv_timer_t *timer);
static void closed_handler(uv_handle_t *handle);

static unsigned int random(void);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

#if defined(COMPILER_MSVC) && defined(CONFIGURATION_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_EVERY_1024_DF);
#endif /*COMPILER_MSVC && CONFIGURATION_DEBUG*/

  parse_result_e parse_result = parse_args(argc, argv);
  switch (parse_result) {
  case parse_result_show_help:
    show_help();
    return EXIT_SUCCESS;

  case parse_result_show_version:
    show_version();
    return EXIT_SUCCESS;

  case parse_result_continue:
    break;

  default:
    return EXIT_FAILURE;
  }

  uv_loop_t loop = {0};

  int err = uv_loop_init(&loop);
  if (err) {
    logger_print_error("uv_loop_init failed: %s\n", uv_strerror(err));
    return EXIT_FAILURE;
  }

  uv_signal_t sigint = {0};

  err = uv_signal_init(&loop, &sigint);
  if (err) {
    logger_print_error("uv_signal_init failed: %s\n", uv_strerror(err));
    loop_term(&loop, 0);
    return EXIT_FAILURE;
  }

  uv_handle_set_data((uv_handle_t *)&sigint, &loop);

  err = uv_signal_start(&sigint, sigint_handler, SIGINT);
  if (err) {
    logger_print_error("uv_signal_start failed: %s\n", uv_strerror(err));
    uv_close((uv_handle_t *)&sigint, closed_handler);
    loop_term(&loop, 0);
    return EXIT_FAILURE;
  }

  uv_timer_t stats_timer = {0};
  err = uv_timer_init(&loop, &stats_timer);
  if (err) {
    logger_print_error("uv_timer_init failed: %s\n", uv_strerror(err));
    uv_close((uv_handle_t *)&sigint, closed_handler);
    loop_term(&loop, 0);
    return EXIT_FAILURE;
  }

  s_stats_start_ns = s_stats_prev_ns = uv_hrtime();
  err = uv_timer_start(&stats_timer, stats_handler, 1 * 1000, 1 * 1000);
  if (err) {
    logger_print_error("uv_timer_start failed: %s\n", uv_strerror(err));
    uv_close((uv_handle_t *)&stats_timer, closed_handler);
    uv_close((uv_handle_t *)&sigint, closed_handler);
    loop_term(&loop, 0);
    return EXIT_FAILURE;
  }

  worker_p *workers = (worker_p *)calloc(g_arg_workers_count, sizeof(*workers));
  if (NULL == workers) {
    logger_print_error("calloc failed: %s\n", uv_strerror(UV_ENOMEM));
    uv_close((uv_handle_t *)&stats_timer, closed_handler);
    uv_close((uv_handle_t *)&sigint, closed_handler);
    loop_term(&loop, 0);
    return EXIT_FAILURE;
  }

#if defined(PLATFORM_WINDOWS)
  DWORD_PTR process_affinity = 0;
  DWORD_PTR system_affinity = 0;

  if (0 != GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity)) {
    SetProcessAffinityMask(GetCurrentProcess(), system_affinity);

    logger_print_trace("Use affinity mask 0x%" PRIx64 "\n", system_affinity);
  }
#endif /*PLATFORM_WINDOWS*/

  logger_print_info("Starting %d workers...\n", g_arg_workers_count);

  int worker_index = 0;
  for (worker_index = 0; worker_index < g_arg_workers_count; ++worker_index) {
    if (0 == worker_index) {
      workers[worker_index] = worker_create_in_loop(&loop, worker_index + 1);
    } else {
      workers[worker_index] = worker_create_in_thread(worker_index + 1);
    }

    if (NULL == workers[worker_index]) {
      for (; worker_index >= 0; --worker_index) {
        worker_destroy(workers[worker_index]);
      }

      free(workers);
      uv_close((uv_handle_t *)&stats_timer, closed_handler);
      uv_close((uv_handle_t *)&sigint, closed_handler);
      loop_term(&loop, 0);
      return EXIT_FAILURE;
    }
  }

  logger_print_info("Press Ctrl+C to stop\n");

  loop_run(&loop);

  uv_close((uv_handle_t *)&stats_timer, closed_handler);
  uv_close((uv_handle_t *)&sigint, closed_handler);

  logger_print_info("Stopping %d workers...\n", g_arg_workers_count);

  for (worker_index = 0; worker_index < g_arg_workers_count; ++worker_index) {
    worker_destroy(workers[worker_index]);
  }

  loop_term(&loop, 0);

  free(workers);

  return EXIT_SUCCESS;
}

static void show_help(void) {
  // clang-format off

  printf("udp-flood <options>\n");
  printf("Version %s, (c) %s %s\n", VERSION, YEARS, AUTHOR);
  printf("\n");

  printf("Create a lot of UDP traffic.\n");
  printf("\n");

  printf("Logging options:\n");
  printf("    -v, --verbose      Verbose mode\n");
  printf("    -q, --quiet        Quiet mode\n");
  printf("        --raw-stats    Do not convert stats to minutes and Gbytes\n");
  printf("\n");

  printf("Flood options:\n");
  printf("    -a, --address <address>    Destination IP address\n");
  printf("    -p, --port <port>          Destination port\n");
  printf("        --port-min <port>      Minimal destination port\n");
  printf("        --port-max <port>      Maximal destination port\n");
  printf("    -s, --size <bytes>         Size of one datagram\n");
  printf("        --size-min <bytes>     Minimal size of one datagram\n");
  printf("        --size-max <bytes>     Maximal size of one datagram\n");
  printf("    -t, --timeout <ms>         Intervals between sendings for each worker\n");
  printf("    -w, --workers <count>      Workers count\n");
  printf("\n");

  printf("Notes:\n");
  printf("  * Destination address could have '*' symbols, in this case a random number will be used in this position\n");
  printf("  * Destination address could be IPv4 (with dots) or IPv6 (with colons)\n");
  printf("  * `--port-min` and `--port-max` could be used to randomize the destination port\n");
  printf("  * `--size-min` and `--size-max` could be used to randomize the datagram size\n");
  printf("  * Application sends random data, do not use a port if someone is listening to it\n");
  printf("  * `--workers` can be 0, in this case one worker will be created for each CPU\n");
  printf("  * A worker stops on the first error\n");
  printf("\n");

  printf("Defaults:\n");
  printf("    --address    %s\n", DEFAULT_ADDRESS);
  printf("    --port       %d\n", DEFAULT_PORT);
  printf("    --size       %d\n", DEFAULT_SIZE);
  printf("    --timeout    %d\n", DEFAULT_TIMEOUT);
  printf("    --workers    %d\n", DEFAULT_WORKERS);
  printf("\n");

  printf("Limits:\n");
  printf("    --port       %d <= port <= %d\n", MINIMAL_PORT, MAXIMAL_PORT);
  printf("    --size       %d <= size <= %d\n", MINIMAL_SIZE, MAXIMAL_SIZE);
  printf("    --timeout    %d <= timeout <= %d\n", MINIMAL_TIMEOUT, MAXIMAL_TIMEOUT);
  printf("    --workers    %d <= workers <= %d\n", MINIMAL_WORKERS, MAXIMAL_WORKERS);

  // clang-format on
}

static void show_version(void) {
  printf("Version %s, (c) %s %s\n", VERSION, YEARS, AUTHOR);
}

static parse_result_e parse_args(int argc, char **argv) {
  g_arg_address = DEFAULT_ADDRESS;
  g_arg_port_min = g_arg_port_max = DEFAULT_PORT;
  g_arg_size_min = g_arg_size_max = DEFAULT_SIZE;
  g_arg_timeout_ms = DEFAULT_TIMEOUT;
  g_arg_workers_count = DEFAULT_WORKERS;

  int argi = 0;
  for (argi = 1; argi < argc; ++argi) {
    const char *arg = argv[argi];

    bool has_next = argi + 1 < argc;
    const char *next_arg = (argi + 1 < argc ? argv[argi + 1] : "");

    if (0 == strcmp(arg, "--help")) {
      return parse_result_show_help;
    } else if (0 == strcmp(arg, "--version")) {
      return parse_result_show_version;
    }

    else if (0 == strcmp(arg, "-q") || 0 == strcmp(arg, "--quiet")) {
      g_logger_level = LOGGER_LEVEL_ERROR;
    } else if (0 == strcmp(arg, "-v") || 0 == strcmp(arg, "--verbose")) {
      g_logger_level = LOGGER_LEVEL_TRACE;
    } else if (0 == strcmp(arg, "--raw-stats")) {
      s_stats_raw = true;
    }

    else if (0 == strcmp(arg, "-a") || 0 == strcmp(arg, "--address")) {
      if (!has_next) {
        printf("Required address\n");
        return parse_result_exit;
      } else {
        g_arg_address = next_arg;
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-p") || 0 == strcmp(arg, "--port")) {
      if (!has_next) {
        printf("Required port\n");
        return parse_result_exit;
      } else {
        g_arg_port_min = g_arg_port_max = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--port-min")) {
      if (!has_next) {
        printf("Required minimal port\n");
        return parse_result_exit;
      } else {
        g_arg_port_min = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--port-max")) {
      if (!has_next) {
        printf("Required maximal port\n");
        return parse_result_exit;
      } else {
        g_arg_port_max = atoi(next_arg);
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-s") || 0 == strcmp(arg, "--size")) {
      if (!has_next) {
        printf("Required size\n");
        return parse_result_exit;
      } else {
        g_arg_size_min = g_arg_size_max = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--size-min")) {
      if (!has_next) {
        printf("Required minimal size\n");
        return parse_result_exit;
      } else {
        g_arg_size_min = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--size-max")) {
      if (!has_next) {
        printf("Required maximal size\n");
        return parse_result_exit;
      } else {
        g_arg_size_max = atoi(next_arg);
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-t") || 0 == strcmp(arg, "--timeout")) {
      if (!has_next) {
        printf("Required timeout\n");
        return parse_result_exit;
      } else {
        g_arg_timeout_ms = atoi(next_arg);
        if (g_arg_timeout_ms < 0) {
          printf("Timeout should be greater or equal to 0\n");
          return parse_result_exit;
        }
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-w") || 0 == strcmp(arg, "--workers")) {
      if (!has_next) {
        printf("Required workers count\n");
        return parse_result_exit;
      } else {
        g_arg_workers_count = atoi(next_arg);
        if (g_arg_workers_count < 0) {
          printf("Workers count should be greater or equal to 0\n");
          return parse_result_exit;
        } else if (0 == g_arg_workers_count) {
          uv_cpu_info_t *cpu_info = NULL;
          int count = 0;

          if (0 == uv_cpu_info(&cpu_info, &count)) {
            uv_free_cpu_info(cpu_info, count);
            g_arg_workers_count = count;
          }

#if defined(PLATFORM_WINDOWS)
          if (0 == count) {
            SYSTEM_INFO system_info = {0};
            GetSystemInfo(&system_info);

            g_arg_workers_count = (int)system_info.dwNumberOfProcessors;
          }
#endif /*PLATFORM_WINDOWS*/

          if (0 == g_arg_workers_count) {
            printf("Cannot get count of CPUs, please specify workers count\n");
            return parse_result_exit;
          }
        }
      }

      ++argi;
    }

    else {
      printf("Uknown option %s\n", arg);
      return parse_result_exit;
    }
  }

  if (!(MINIMAL_PORT <= g_arg_port_min && g_arg_port_min <= g_arg_port_max && g_arg_port_max <= MAXIMAL_PORT)) {
    printf("Invalid minimal %d or maximal port %d\n", g_arg_port_min, g_arg_port_max);
    return parse_result_exit;
  } else if (!(MINIMAL_SIZE <= g_arg_size_min && g_arg_size_min <= g_arg_size_max && g_arg_size_max <= MAXIMAL_SIZE)) {
    printf("Invalid minimal %d or maximal size %d\n", g_arg_size_min, g_arg_size_max);
    return parse_result_exit;
  } else if (!(MINIMAL_TIMEOUT <= g_arg_timeout_ms && g_arg_timeout_ms <= MAXIMAL_TIMEOUT)) {
    printf("Invalid timeout %d\n", g_arg_timeout_ms);
    return parse_result_exit;
  } else if (!(MINIMAL_WORKERS <= g_arg_workers_count && g_arg_workers_count <= MAXIMAL_WORKERS)) {
    printf("Invalid workers count %d\n", g_arg_workers_count);
    return parse_result_exit;
  }

  bool is_ipv4 = strchr(g_arg_address, '.');
  bool is_ipv6 = strchr(g_arg_address, ':');

  if ((!is_ipv4 && !is_ipv6) || (is_ipv4 && is_ipv6)) {
    printf("Invalid address %s, IPv4 or IPv6 address is required\n", g_arg_address);
    return parse_result_exit;
  }

  g_arg_is_ipv4 = is_ipv4;

  return parse_result_continue;
}

static void sigint_handler(uv_signal_t *sigint, int signum) {
  (void)signum;

  uv_loop_t *loop = (uv_loop_t *)uv_handle_get_data((uv_handle_t *)sigint);
  assert(NULL != loop);

  logger_print_error("Interrupted...\n");

  loop_stop(loop);
}

static void closed_handler(uv_handle_t *sigint) {
  (void)sigint;

  // do nothing
}

static void humanize_time(char *buffer, size_t buffer_length, uint64_t time_ns) {
  uint64_t time_sec = (time_ns / 1000000 + 500) / 1000;

  uint64_t seconds = time_sec % 60;
  uint64_t minutes = time_sec / 60 % 60;
  uint64_t hours = time_sec / 3600;

  sprintf_s(buffer, buffer_length, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64, hours, minutes, seconds);
}

static void humanize_bytes(char *buffer, size_t buffer_length, uint64_t bytes) {
  if (bytes < 768ull) {
    sprintf_s(buffer, buffer_length, "%" PRIu64 " bytes", bytes);
  } else if (bytes < 768ull * 1024) {
    sprintf_s(buffer, buffer_length, "%.2f KiB", bytes / 1024.0f);
  } else if (bytes < 768ull * 1024 * 1024) {
    sprintf_s(buffer, buffer_length, "%.2f MiB", bytes / (1024.0f * 1024.0f));
  } else if (bytes < 768ull * 1024 * 1024 * 1024) {
    sprintf_s(buffer, buffer_length, "%.2f GiB", bytes / (1024.0f * 1024.0f * 1024.0f));
  } else if (bytes < 768ull * 1024 * 1024 * 1024 * 1024) {
    sprintf_s(buffer, buffer_length, "%.2f TiB", bytes / (1024.0f * 1024.0f * 1024.0f * 1024.0f));
  } else {
    sprintf_s(buffer, buffer_length, "%.2f PiB", bytes / (1024.0f * 1024.0f * 1024.0f * 1024.0f * 1024.0f));
  }
}

static void humanize_operations(char *buffer, size_t buffer_length, uint64_t operations) {
  if (operations < 700ull) {
    sprintf_s(buffer, buffer_length, "%" PRIu64 " operations", operations);
  } else if (operations < 700ull * 1000) {
    sprintf_s(buffer, buffer_length, "%.2f Kop", operations / 1.0E3f);
  } else if (operations < 700ull * 1000 * 1000) {
    sprintf_s(buffer, buffer_length, "%.2f Mop", operations / 1.0E6f);
  } else if (operations < 700ull * 1000 * 1000 * 1000) {
    sprintf_s(buffer, buffer_length, "%.2f Gop", operations / 1.0E9f);
  } else if (operations < 700ull * 1000 * 1000 * 1000 * 1000) {
    sprintf_s(buffer, buffer_length, "%.2f Top", operations / 1.0E12f);
  } else {
    sprintf_s(buffer, buffer_length, "%.2f Pop", operations / 1.0E15f);
  }
}

static void stats_handler(uv_timer_t *timer) {
  (void)timer;

  uint64_t time_ns = uv_hrtime();
  uint64_t total_ns = time_ns - s_stats_start_ns;
  s_stats_prev_ns = time_ns;

  uint64_t total_bytes = (uint64_t)custom_atomic_load(&g_stats_sent_bytes);
  uint64_t tick_bytes = total_bytes - s_stats_prev_sent_bytes;
  s_stats_prev_sent_bytes = total_bytes;

  uint64_t total_operations = (uint64_t)custom_atomic_load(&g_stats_sent_operations);
  uint64_t tick_operations = total_operations - s_stats_prev_sent_operations;
  s_stats_prev_sent_operations = total_operations;

  if (s_stats_raw) {
    logger_print_info("Elapsed %" PRIu64 " ms, %" PRIu64 " bytes/s and %" PRIu64 " op/s, total %" PRIu64 " bytes and %" PRIu64
                      " operations\n",
                      total_ns / (1 * 1000 * 1000), tick_bytes, tick_operations, total_bytes, total_operations);
  } else {
    char time_str[64] = {0};
    humanize_time(time_str, countof(time_str), total_ns);

    char total_bytes_str[64] = {0};
    humanize_bytes(total_bytes_str, countof(total_bytes_str), total_bytes);

    char total_operations_str[64] = {0};
    humanize_operations(total_operations_str, countof(total_operations_str), total_operations);

    char tick_bytes_str[64] = {0};
    humanize_bytes(tick_bytes_str, countof(tick_bytes_str), tick_bytes);

    char tick_operations_str[64] = {0};
    humanize_operations(tick_operations_str, countof(tick_operations_str), tick_operations);

    logger_print_info("Elapsed %s, %s/s and %s/s, total %s and %s\n", time_str, tick_bytes_str, tick_operations_str,
                      total_bytes_str, total_operations_str);
  }
}
