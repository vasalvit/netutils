#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define VERSION "1.0"
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

typedef struct _arguments_t *arguments_p;
typedef struct _application_t *application_p;
typedef struct _worker_t *worker_p;

typedef struct _arguments_t {
  bool show_help;
  bool show_version;

  bool quiet_mode;
  bool verbose_mode;

  bool raw_stats;

  bool is_ipv4;
  const char *address;
  int port_min, port_max;
  int size_min, size_max;

  int timeout_ms;
  int workers_count;
} arguments_t;

typedef struct _application_t {
  void (*print_error)(const char *format, ...);
  void (*print_info)(const char *format, ...);
  void (*print_trace)(const char *format, ...);

  arguments_t args;

  uv_loop_t loop;
  uv_signal_t sigint;

  uint64_t start_ns;
  uint64_t prev_ns;
  uint64_t sent_bytes;
  uint64_t sent_operations;
  uint64_t prev_sent_bytes;
  uint64_t prev_sent_operations;
  uv_timer_t stats_timer;

  worker_p workers;
  size_t workers_count;
} application_t;

typedef union _sockaddr_any {
  struct sockaddr addr;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
} sockaddr_any;

typedef struct _worker_t {
  int index;
  application_p app;
  arguments_p args;

  bool stopped;

  uv_async_t async;
  uv_timer_t timer;

  sockaddr_any sockaddr;
  char address[256];
  char port[16];

  uv_udp_t socket;
  uv_udp_send_t send_request;
  uv_getaddrinfo_t addr_request;

  uint8_t datagram[MAXIMAL_SIZE];
  uv_buf_t buf;
} worker_t;

static bool parse_args(arguments_p args, int argc, char **argv);

static void show_help(void);
static void show_version(void);

static void logger_print(const char *format, ...);
static void logger_noop(const char *format, ...);

static void sigint_handler(uv_signal_t *sigint, int signum);

static void handle_closed(uv_handle_t *handle);
static void dump_handle(uv_handle_t *handle, void *data);
static void print_stats(uv_timer_t *timer);

static bool worker_start(worker_p worker, int index, application_p app, arguments_p args);
static void worker_stop(worker_p worker);

static void worker_timeout(uv_timer_t *timer);
static void worker_send(uv_async_t *async);
static void worker_addr_completed(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
static void worker_send_completed(uv_udp_send_t *req, int status);

static unsigned int random(void);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  application_t app = {0};

  if (!parse_args(&app.args, argc, argv)) {
    return EXIT_FAILURE;
  }

  if (app.args.show_help) {
    show_help();
    return EXIT_SUCCESS;
  } else if (app.args.show_version) {
    show_version();
    return EXIT_SUCCESS;
  }

  app.print_error = logger_print;
  app.print_info = app.args.quiet_mode ? logger_noop : logger_print;
  app.print_trace = app.args.quiet_mode ? logger_noop : (app.args.verbose_mode ? logger_print : logger_noop);

  int rc = uv_loop_init(&app.loop);
  if (rc) {
    app.print_error("uv_loop_init failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  rc = uv_timer_init(&app.loop, &app.stats_timer);
  if (rc) {
    app.print_error("uv_timer_init failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  uv_handle_set_data((uv_handle_t *)&app.stats_timer, &app);

  app.start_ns = app.prev_ns = uv_hrtime();
  rc = uv_timer_start(&app.stats_timer, print_stats, 1 * 1000, 1 * 1000);
  if (rc) {
    app.print_error("uv_timer_start failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  app.workers_count = (size_t)app.args.workers_count;

  app.workers = (worker_p)calloc(app.workers_count, sizeof(*app.workers));
  if (!app.workers) {
    app.print_error("Not enough memory for workers\n");
    return EXIT_FAILURE;
  }

  size_t worker_index = 0;
  for (worker_index = 0; worker_index < app.workers_count; ++worker_index) {
    if (!worker_start(&app.workers[worker_index], (int)worker_index + 1, &app, &app.args)) {
      return EXIT_FAILURE;
    }
  }

  rc = uv_signal_init(&app.loop, &app.sigint);
  if (rc) {
    app.print_error("uv_signal_init failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  uv_handle_set_data((uv_handle_t *)&app.sigint, &app);

  rc = uv_signal_start(&app.sigint, sigint_handler, SIGINT);
  if (rc) {
    app.print_error("uv_signal_start failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  app.print_info("Press Ctrl+C to stop\n");

  uv_run(&app.loop, UV_RUN_DEFAULT);

  for (worker_index = 0; worker_index < app.workers_count; ++worker_index) {
    worker_stop(&app.workers[worker_index]);
  }

  uv_close((uv_handle_t *)&app.stats_timer, handle_closed);

  rc = uv_loop_close(&app.loop);

  int retry = 0;
  for (retry = 0; UV_EBUSY == rc && retry < 1000; ++retry) {
    uv_run(&app.loop, UV_RUN_NOWAIT);
    rc = uv_loop_close(&app.loop);
  }

  if (UV_EBUSY == rc) {
    app.print_error("Found unclosed handles:\n");
    uv_walk(&app.loop, dump_handle, &app);
  }

  free(app.workers);

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
  printf("  * Worker stops on the first error\n");
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

static bool parse_args(arguments_p args, int argc, char **argv) {
  bool parsed = true;

  args->address = DEFAULT_ADDRESS;
  args->port_min = args->port_max = DEFAULT_PORT;
  args->size_min = args->size_max = DEFAULT_SIZE;
  args->timeout_ms = DEFAULT_TIMEOUT;
  args->workers_count = DEFAULT_WORKERS;

  int argi = 0;
  for (argi = 1; argi < argc; ++argi) {
    const char *arg = argv[argi];

    bool has_next = argi + 1 < argc;
    const char *next_arg = (argi + 1 < argc ? argv[argi + 1] : "");

    if (0 == strcmp(arg, "--help")) {
      args->show_help = true;
    } else if (0 == strcmp(arg, "--version")) {
      args->show_version = true;
    }

    else if (0 == strcmp(arg, "-q") || 0 == strcmp(arg, "--quiet")) {
      args->quiet_mode = true;
    } else if (0 == strcmp(arg, "-v") || 0 == strcmp(arg, "--verbose")) {
      args->verbose_mode = true;
    } else if (0 == strcmp(arg, "--raw-stats")) {
      args->raw_stats = true;
    }

    else if (0 == strcmp(arg, "-a") || 0 == strcmp(arg, "--address")) {
      if (!has_next) {
        printf("Required address\n");
        parsed = false;
      } else {
        args->address = next_arg;
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-p") || 0 == strcmp(arg, "--port")) {
      if (!has_next) {
        printf("Required port\n");
        parsed = false;
      } else {
        args->port_min = args->port_max = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--port-min")) {
      if (!has_next) {
        printf("Required minimal port\n");
        parsed = false;
      } else {
        args->port_min = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--port-max")) {
      if (!has_next) {
        printf("Required maximal port\n");
        parsed = false;
      } else {
        args->port_max = atoi(next_arg);
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-s") || 0 == strcmp(arg, "--size")) {
      if (!has_next) {
        printf("Required size\n");
        parsed = false;
      } else {
        args->size_min = args->size_max = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--size-min")) {
      if (!has_next) {
        printf("Required minimal size\n");
        parsed = false;
      } else {
        args->size_min = atoi(next_arg);
      }

      ++argi;
    } else if (0 == strcmp(arg, "--size-max")) {
      if (!has_next) {
        printf("Required maximal size\n");
        parsed = false;
      } else {
        args->size_max = atoi(next_arg);
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-t") || 0 == strcmp(arg, "--timeout")) {
      if (!has_next) {
        printf("Required timeout\n");
        parsed = false;
      } else {
        args->timeout_ms = atoi(next_arg);
      }

      ++argi;
    }

    else if (0 == strcmp(arg, "-w") || 0 == strcmp(arg, "--workers")) {
      if (!has_next) {
        printf("Required workers count\n");
        parsed = false;
      } else {
        args->workers_count = atoi(next_arg);
      }

      ++argi;
    }

    else {
      printf("Uknown option %s\n", arg);
      parsed = false;
    }
  }

  if (parsed) {
    if (!(MINIMAL_PORT <= args->port_min && args->port_min <= args->port_max && args->port_max <= MAXIMAL_PORT)) {
      printf("Invalid minimal %d or maximal port %d\n", args->port_min, args->port_max);
      parsed = false;
    } else if (!(MINIMAL_SIZE <= args->size_min && args->size_min <= args->size_max && args->size_max <= MAXIMAL_SIZE)) {
      printf("Invalid minimal %d or maximal size %d\n", args->size_min, args->size_max);
      parsed = false;
    } else if (!(MINIMAL_TIMEOUT <= args->timeout_ms && args->timeout_ms <= MAXIMAL_TIMEOUT)) {
      printf("Invalid timeout %d\n", args->timeout_ms);
      parsed = false;
    } else if (!(MINIMAL_WORKERS <= args->workers_count && args->workers_count <= MAXIMAL_WORKERS)) {
      printf("Invalid workers count %d\n", args->workers_count);
      parsed = false;
    }

    bool is_ipv4 = strchr(args->address, '.');
    bool is_ipv6 = strchr(args->address, ':');

    if ((!is_ipv4 && !is_ipv6) || (is_ipv4 && is_ipv6)) {
      printf("Invalid address %s, IPv4 or IPv6 address is required\n", args->address);
      parsed = false;
    }

    args->is_ipv4 = is_ipv4;
  }

  return parsed;
}

static void logger_print(const char *format, ...) {
  va_list args;
  va_start(args, format);

  vprintf_s(format, args);

  va_end(args);
}

static void logger_noop(const char *format, ...) {
  (void)format;

  // do nothing
}

static void sigint_handler(uv_signal_t *sigint, int signum) {
  (void)signum;

  application_p app = (application_p)uv_handle_get_data((uv_handle_t *)sigint);
  assert(NULL != app);

  app->print_trace("Interrupted...\n");

  uv_signal_stop(&app->sigint);
  uv_close((uv_handle_t *)&app->sigint, handle_closed);

  uv_stop(&app->loop);
}

static void handle_closed(uv_handle_t *sigint) {
  (void)sigint;

  // do nothing
}

static void dump_handle(uv_handle_t *handle, void *data) {
  application_p app = (application_p)data;

#define XX(uc, lc)                                                                                                             \
  case UV_##uc:                                                                                                                \
    app->print_error("  Unclosed handle %s\n", #uc);                                                                           \
    break;

  switch (handle->type) {
    UV_HANDLE_TYPE_MAP(XX)

  default:
    app->print_error("  Unknown unclosed handle type\n");
    break;
  }

#undef XX
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

static void print_stats(uv_timer_t *timer) {
  application_p app = (application_p)uv_handle_get_data((uv_handle_t *)timer);

  uint64_t time_ns = uv_hrtime();
  uint64_t total_ns = time_ns - app->start_ns;
  app->prev_ns = time_ns;

  uint64_t total_bytes = app->sent_bytes;
  uint64_t tick_bytes = app->sent_bytes - app->prev_sent_bytes;
  app->prev_sent_bytes = total_bytes;

  uint64_t total_operations = app->sent_operations;
  uint64_t tick_operations = app->sent_operations - app->prev_sent_operations;
  app->prev_sent_operations = total_operations;

  if (app->args.raw_stats) {
    app->print_info("Elapsed %" PRIu64 " ms, %" PRIu64 " bytes/s and %" PRIu64 " op/s, total %" PRIu64 " bytes and %" PRIu64
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

    app->print_info("Elapsed %s, %s/s and %s/s, total %s and %s\n", time_str, tick_bytes_str, tick_operations_str,
                    total_bytes_str, total_operations_str);
  }
}

static bool worker_start(worker_p worker, int index, application_p app, arguments_p args) {
  worker->index = index;
  worker->app = app;
  worker->args = args;

  int rc = uv_async_init(&worker->app->loop, &worker->async, worker_send);
  if (rc) {
    worker->app->print_error("#%d: uv_async_init failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  uv_handle_set_data((uv_handle_t *)&worker->async, worker);

  rc = uv_timer_init(&worker->app->loop, &worker->timer);
  if (rc) {
    worker->app->print_error("#%d: uv_timer_init failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  uv_handle_set_data((uv_handle_t *)&worker->timer, worker);

  rc = uv_udp_init(&worker->app->loop, &worker->socket);
  if (rc) {
    worker->app->print_error("#%d: uv_udp_init failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  uv_handle_set_data((uv_handle_t *)&worker->socket, worker);

  rc = uv_async_send(&worker->async);
  if (rc) {
    worker->app->print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  worker->app->print_trace("#%d: Worker started\n", worker->index);

  return true;
}

static void worker_stop(worker_p worker) {
  worker->stopped = true;

  uv_cancel((uv_req_t *)&worker->send_request);
  uv_cancel((uv_req_t *)&worker->addr_request);

  uv_close((uv_handle_t *)&worker->async, handle_closed);
  uv_close((uv_handle_t *)&worker->timer, handle_closed);
  uv_close((uv_handle_t *)&worker->socket, handle_closed);

  worker->app->print_trace("#%d: Worker stopped\n", worker->index);
}

static void worker_send(uv_async_t *async) {
  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)async);

  if (worker->stopped) {
    return;
  }

  worker->address[0] = 0;

  const char *address = worker->args->address;
  while (*address) {
    const char *ptr = strchr(address, '*');
    if (!ptr) {
      strcat_s(worker->address, sizeof(worker->address), address);
      break;
    }

    if (address != ptr) {
      strncat_s(worker->address, sizeof(worker->address), address, ptr - address);
    }

    char buffer[10];
    if (worker->args->is_ipv4) {
      sprintf_s(buffer, countof(buffer), "%d", random() % 256);
    } else {
      sprintf_s(buffer, countof(buffer), "%04x", random() % 65536);
    }

    strcat_s(worker->address, sizeof(worker->address), buffer);

    address = ptr + 1;
  }

  if (worker->args->port_min == worker->args->port_max) {
    sprintf_s(worker->port, countof(worker->port), "%d", worker->args->port_min);
  } else {
    sprintf_s(worker->port, countof(worker->port), "%d",
              worker->args->port_min + random() % (worker->args->port_max - worker->args->port_min + 1));
  }

  struct addrinfo hints = {0};
  hints.ai_family = worker->args->is_ipv4 ? AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_CANONNAME;

  int rc =
      uv_getaddrinfo(&worker->app->loop, &worker->addr_request, worker_addr_completed, worker->address, worker->port, &hints);
  if (rc) {
    worker->app->print_info("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                            uv_strerror(rc));
    return;
  }

  uv_req_set_data((uv_req_t *)&worker->addr_request, worker);
}

static void worker_timeout(uv_timer_t *timer) {
  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)timer);

  if (worker->stopped) {
    return;
  }

  uv_timer_stop(&worker->timer);

  int rc = uv_async_send(&worker->async);
  if (rc) {
    worker->app->print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(rc));
    return;
  }
}

static void worker_addr_completed(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
  worker_p worker = (worker_p)uv_req_get_data((uv_req_t *)req);

  if (worker->stopped) {
    return;
  }

  if (status) {
    worker->app->print_info("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                            uv_strerror(status));
    return;
  } else if (!res) {
    worker->app->print_info("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                            uv_strerror(UV_EINVAL));
    return;
  }

  memcpy_s(&worker->sockaddr, sizeof(worker->sockaddr), res->ai_addr, res->ai_addrlen);

  uv_freeaddrinfo(res);

  int size = (worker->args->size_min == worker->args->size_max)
                 ? (worker->args->size_min)
                 : (worker->args->size_min + random() % (worker->args->size_max - worker->args->size_min + 1));

  int index = 0;
  for (index = 0; index < size; ++index) {
    worker->datagram[index] = random() % 256;
  }

  worker->buf.base = (char *)worker->datagram;
  worker->buf.len = size;

  worker->app->print_trace("#%d: Send %d bytes to %s %s\n", worker->index, worker->buf.len, worker->address, worker->port);

  int rc = uv_udp_send(&worker->send_request, &worker->socket, &worker->buf, 1, &worker->sockaddr.addr, worker_send_completed);
  if (rc) {
    worker->app->print_error("#%d: uv_udp_send(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                             uv_strerror(rc));
    return;
  }

  uv_req_set_data((uv_req_t *)&worker->send_request, worker);
}

static void worker_send_completed(uv_udp_send_t *req, int status) {
  worker_p worker = (worker_p)uv_req_get_data((uv_req_t *)req);

  if (worker->stopped) {
    return;
  }

  if (status) {
    worker->app->print_info("#%d: uv_udp_send(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                            uv_strerror(status));
    return;
  }

  worker->app->sent_operations++;
  worker->app->sent_bytes += worker->buf.len;

  if (0 == worker->args->timeout_ms) {
    int rc = uv_async_send(&worker->async);
    if (rc) {
      worker->app->print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(rc));
      return;
    }
  } else {
    int rc = uv_timer_start(&worker->timer, worker_timeout, worker->args->timeout_ms, 0);
    if (rc) {
      worker->app->print_error("#%d: uv_timer_start failed: %s\n", worker->index, uv_strerror(rc));
      return;
    }
  }
}

static unsigned int random(void) {
  static unsigned int v = 0, u = 0;

  if (0 == v && 0 == u) {
    v = (unsigned int)(uintptr_t)uv_os_getpid();
    u = (unsigned int)(uintptr_t)uv_hrtime();
  }

  v = 36969 * (v & 65535) + (v >> 16);
  u = 18000 * (u & 65535) + (u >> 16);

  return (v << 16) + (u & 65535);
}
