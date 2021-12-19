#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define VERSION "1.0"
#define YEARS "2021"
#define AUTHOR "Alexander Vasilevsky <vasalvit@gmail.com>"

#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_PORT 55555
#define DEFAULT_SIZE 512
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

typedef struct _arguments_t *arguments_p;
typedef struct _application_t *application_p;
typedef struct _worker_t *worker_p;

typedef struct _arguments_t {
  bool show_help;
  bool show_version;

  bool quiet_mode;
  bool verbose_mode;

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

  uv_loop_t loop;
  uv_signal_t sigint;

  worker_p workers;
  size_t workers_count;
} application_t;

typedef struct _worker_t {
  int index;
  application_p app;
  arguments_p args;

  uv_async_t async;
} worker_t;

static bool parse_args(arguments_p args, int argc, char **argv);

static void show_help(void);
static void show_version(void);

static void logger_print(const char *format, ...);
static void logger_noop(const char *format, ...);

static void sigint_handler(uv_signal_t *, int);

static void handle_closed(uv_handle_t *);

static void dump_handle(uv_handle_t *, void *);

static bool worker_start(worker_p worker, int index, application_p app, arguments_p args);
static void worker_stop(worker_p worker);

static void worker_send(uv_async_t *handle);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // parse arguments

  arguments_t args = {0};
  if (!parse_args(&args, argc, argv)) {
    return EXIT_FAILURE;
  }

  if (args.show_help) {
    show_help();
    return EXIT_SUCCESS;
  } else if (args.show_version) {
    show_version();
    return EXIT_SUCCESS;
  }

  // init application

  application_t app = {0};

  app.print_error = logger_print;
  app.print_info = args.quiet_mode ? logger_noop : logger_print;
  app.print_trace = args.quiet_mode ? logger_noop : (args.verbose_mode ? logger_print : logger_noop);

  // init loop

  int rc = uv_loop_init(&app.loop);
  if (rc) {
    app.print_error("uv_loop_init failed: %s\n", uv_strerror(rc));
    return EXIT_FAILURE;
  }

  // start workers

  app.workers_count = (size_t)args.workers_count;

  app.workers = (worker_p)calloc(app.workers_count, sizeof(*app.workers));
  if (!app.workers) {
    app.print_error("Not enough memory for workers\n");
    return EXIT_FAILURE;
  }

  size_t worker_index = 0;
  for (worker_index = 0; worker_index < app.workers_count; ++worker_index) {
    if (!worker_start(&app.workers[worker_index], (int)worker_index + 1, &app, &args)) {
      return EXIT_FAILURE;
    }
  }

  // init Ctrl+C handler

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

  // start loop

  app.print_info("Press Ctrl+C to stop\n");

  uv_run(&app.loop, UV_RUN_DEFAULT);

  // stop workers

  for (worker_index = 0; worker_index < app.workers_count; ++worker_index) {
    worker_stop(&app.workers[worker_index]);
  }

  // stop loop

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

  // term application

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
  printf("    -v, --verbose    Verbose mode\n");
  printf("    -q, --quiet      Quiet mode\n");
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
  printf("  * Destination address could have '%%d' symbols, in this case a random number will be used in this position\n");
  printf("  * Destination address could be IPv4 (with dots) or IPv6 (with colons)\n");
  printf("  * `--port-min` and `--port-max` could be used to randomize the destination port\n");
  printf("  * `--size-min` and `--size-max` could be used to randomize the datagram size\n");
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

static bool worker_start(worker_p worker, int index, application_p app, arguments_p args) {
  worker->index = index;
  worker->app = app;
  worker->args = args;

  // initialize async

  int rc = uv_async_init(&app->loop, &worker->async, worker_send);
  if (rc) {
    app->print_error("#%d: uv_async_init failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  uv_handle_set_data((uv_handle_t *)&worker->async, worker);

  // start worker

  rc = uv_async_send(&worker->async);
  if (rc) {
    app->print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(rc));
    return false;
  }

  worker->app->print_trace("#%d: Worker started\n", worker->index);

  return true;
}

static void worker_stop(worker_p worker) {
  uv_close((uv_handle_t *)&worker->async, handle_closed);

  worker->app->print_trace("#%d: Worker stopped\n", worker->index);
}

static void worker_send(uv_async_t *handle) {
  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)handle);

  worker->app->print_info("#%d: TODO: Send data\n", worker->index);
}
