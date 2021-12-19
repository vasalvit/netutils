#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

typedef struct _application_t {
  uv_loop_t loop;
  uv_signal_t sigint;
} application_t, *application_p;

static int help(void);

static void sigint_handler(uv_signal_t *, int);
static void sigint_closed(uv_handle_t *);

static void dump_handle(uv_handle_t *, void *);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  application_t app = {0};

  uv_loop_init(&app.loop);

  // init Ctrl+C handler

  uv_signal_init(&app.loop, &app.sigint);
  uv_handle_set_data((uv_handle_t *)&app.sigint, &app);
  uv_signal_start(&app.sigint, (uv_signal_cb)sigint_handler, SIGINT);

  // start loop

  printf("Press Ctrl+C to stop\n");

  uv_run(&app.loop, UV_RUN_DEFAULT);

  // stop loop

  int rc = uv_loop_close(&app.loop);
  int i = 0;
  for (i = 0; UV_EBUSY == rc && i < 1000; ++i) {
    uv_run(&app.loop, UV_RUN_NOWAIT);
    rc = uv_loop_close(&app.loop);
  }

  if (UV_EBUSY == rc) {
    printf("Found unclosed handles:\n");
    uv_walk(&app.loop, dump_handle, NULL);
  }

  return 0;
}

static int help(void) {
  printf("udp-flood <options>\n");
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
  printf("    -t, --timeout <ms>         Intervals between sends\n");
  printf("    -s, --size <bytes>         Size of one datagram\n");
  printf("        --size-min <bytes>     Minimal size of one datagram\n");
  printf("        --size-max <bytes>     Maximal size of one datagram\n");
  printf("    -w, --workers <count>      Workers count\n");
  printf("\n");

  printf("Notes:\n");
  printf("  * Destination address could have '%%d' symbols, in this case a\n");
  printf("    random number will be used in this position.\n");
  printf("  * Destination address could be IPv4 (with dots) or IPv6 (with\n");
  printf("    colons)\n");
  printf("  * `--port-min` and `--port-max` could be used to randomize\n");
  printf("    destination port\n");
  printf("  * `--size-min` and `--size-max` could be used to randomize\n");
  printf("    datagram size\n");
  printf("\n");

  printf("Defaults:\n");
  printf("    --address    127.0.0.1\n");
  printf("    --port       55555\n");
  printf("    --timeout    0\n");
  printf("    --size       512\n");
  printf("    --workers    1\n");
  printf("\n");

  return 0;
}

static void sigint_handler(uv_signal_t *sigint, int signum) {
  (void)signum;

  application_p app = (application_p)uv_handle_get_data((uv_handle_t *)sigint);
  assert(NULL != app);

  printf("Interrupted...\n");

  uv_signal_stop(&app->sigint);
  uv_close((uv_handle_t *)&app->sigint, sigint_closed);

  uv_stop(&app->loop);
}

static void sigint_closed(uv_handle_t *sigint) {
  (void)sigint;

  // do nothing
}

static void dump_handle(uv_handle_t *handle, void *data) {
  (void)data;

#define XX(uc, lc)                                                             \
  case UV_##uc:                                                                \
    printf("  Unclosed handle %s\n", #uc);                                     \
    break;

  switch (handle->type) {
    UV_HANDLE_TYPE_MAP(XX)

  default:
    printf("  Unknown unclosed handle type\n");
    break;
  }

#undef XX
}
