#include "./worker.h"
#include "./globals.h"
#include "./logger.h"
#include "./loop.h"
#include "./random.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <uv.h>

#undef countof
#define countof(_x) (sizeof((_x)) / sizeof((_x)[0]))

typedef enum _worker_state_e {
  worker_state_unknown,
  worker_state_failed,
  worker_state_ready,
  worker_state_stopped,
} worker_state_e;

typedef union _sockaddr_any {
  struct sockaddr addr;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
} sockaddr_any;

typedef struct _worker_t {
  unsigned int index;
  bool threaded;

  custom_atomic_int refs_counter;
  worker_state_e state;

  uv_loop_t *loop;
  uv_async_t term;
  uv_async_t send;
  uv_timer_t wait;

  sockaddr_any sockaddr;
  char address[256];
  char port[16];

  uv_udp_t socket;
  uv_udp_send_t send_request;
  uv_getaddrinfo_t addr_request;

  uint8_t *datagram;
  size_t datagram_max_size;
  uv_buf_t buf;

  // these variables are valid only if index != 0
  uv_thread_t thread;
  uv_mutex_t mutex;
  uv_cond_t cond;
} worker_t;

static bool worker_init(worker_p worker);

static worker_p worker_retain(worker_p worker);
static void worker_release(worker_p worker);

static bool worker_is_stopped(worker_p worker);
static void worker_set_state(worker_p worker, worker_state_e state);

static void worker_thread_proc(worker_p worker);

static void worker_async_term(uv_async_t *async);
static void worker_handle_closed(uv_handle_t *handle);

static void worker_async_send(uv_async_t *async);
static void worker_timer_timeout(uv_timer_t *timer);
static void worker_request_addr_completed(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
static void worker_request_send_completed(uv_udp_send_t *req, int status);

worker_p worker_create_in_loop(uv_loop_t *loop, unsigned int index) {
  worker_p worker = (worker_p)calloc(1, sizeof(*worker));
  if (!worker) {
    logger_print_error("#%d: calloc failed: %s\n", index, uv_strerror(ENOMEM));
    return NULL;
  }
  worker_retain(worker);

  worker->threaded = false;
  worker->index = index;
  worker->state = worker_state_unknown;
  worker->loop = loop;

  if (!worker_init(worker)) {
    worker_release(worker);
    return NULL;
  }

  logger_print_trace("#%d: Worker started\n", worker->index);
  return worker;
}

worker_p worker_create_in_thread(unsigned int index) {
  worker_p worker = (worker_p)calloc(1, sizeof(*worker));
  if (!worker) {
    logger_print_error("#%d: calloc failed: %s\n", index, uv_strerror(ENOMEM));
    return NULL;
  }
  worker_retain(worker);

  worker->threaded = true;
  worker->index = index;
  worker->state = worker_state_unknown;

  int err = uv_mutex_init(&worker->mutex);
  if (0 != err) {
    logger_print_error("#%d: uv_mutex_init failed: %s\n", index, uv_strerror(err));
    worker_release(worker);
    return NULL;
  }

  err = uv_cond_init(&worker->cond);
  if (0 != err) {
    logger_print_error("#%d: uv_cond_init failed: %s\n", index, uv_strerror(err));
    uv_mutex_destroy(&worker->mutex);
    worker_release(worker);
    return NULL;
  }

  err = uv_thread_create(&worker->thread, (uv_thread_cb)worker_thread_proc, worker);
  if (0 != err) {
    logger_print_error("#%d: uv_thread_create failed: %s\n", index, uv_strerror(err));
    uv_cond_destroy(&worker->cond);
    uv_mutex_destroy(&worker->mutex);
    worker_release(worker);
    return NULL;
  }

  uv_mutex_lock(&worker->mutex);
  while (worker_state_unknown == worker->state) {
    uv_cond_wait(&worker->cond, &worker->mutex);
  }

  worker_state_e state = worker->state;
  uv_mutex_unlock(&worker->mutex);

  if (worker_state_failed == state) {
    uv_thread_join(&worker->thread);

    uv_cond_destroy(&worker->cond);
    uv_mutex_destroy(&worker->mutex);
    worker_release(worker);
    return NULL;
  }

  logger_print_trace("#%d: Worker started\n", worker->index);
  return worker;
}

void worker_destroy(worker_p worker) {
  assert(NULL != worker);

  int index = worker->index;

  if (!worker->threaded) {
    uv_async_send(&worker->term);
  } else {
    uv_async_send(&worker->term);

    uv_thread_join(&worker->thread);
    uv_cond_destroy(&worker->cond);
    uv_mutex_destroy(&worker->mutex);
  }

  logger_print_trace("#%d: Worker stopped\n", index);

  worker_release(worker);
}

static worker_p worker_retain(worker_p worker) {
  assert(NULL != worker);

  custom_atomic_fetch_add(&worker->refs_counter, 1);

  return worker;
}
static void worker_release(worker_p worker) {
  assert(NULL != worker);

  if (1 == custom_atomic_fetch_sub(&worker->refs_counter, 1)) {
    free(worker->datagram);
    free(worker);
  }
}

static bool worker_init(worker_p worker) {
  assert(NULL != worker);

#if defined(PLATFORM_WINDOWS)
  DWORD_PTR process_affinity = 0;
  DWORD_PTR system_affinity = 0;
  if (0 != GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity)) {
    DWORD_PTR affinity_mask = 1;

    int index = worker->index;
    while (index > 1) {
      affinity_mask = affinity_mask << 1;
      if (0 == affinity_mask) {
        affinity_mask = 1;
      }

      if (affinity_mask & process_affinity) {
        --index;
      }
    }

    if (0 != SetThreadAffinityMask(GetCurrentThread(), affinity_mask)) {
      logger_print_trace("#%d: Use affinity mask 0x%" PRIx64 "\n", worker->index, (uint64_t)affinity_mask);
    }
  }
#endif /*PLATFORM_WINDOWS*/

  worker->datagram_max_size = g_arg_size_max;
  worker->datagram = (uint8_t *)malloc(worker->datagram_max_size);
  if (NULL == worker->datagram) {
    logger_print_error("#%d: malloc failed: %s\n", worker->index, uv_strerror(ENOMEM));
    return false;
  }

  int err = uv_async_init(worker->loop, &worker->term, worker_async_term);
  if (err) {
    logger_print_error("#%d: uv_async_init(term) failed: %s\n", worker->index, uv_strerror(err));
    return false;
  }
  uv_handle_set_data((uv_handle_t *)&worker->term, worker_retain(worker));

  err = uv_async_init(worker->loop, &worker->send, worker_async_send);
  if (err) {
    logger_print_error("#%d: uv_async_init(send) failed: %s\n", worker->index, uv_strerror(err));
    uv_close((uv_handle_t *)&worker->term, worker_handle_closed);
    return false;
  }
  uv_handle_set_data((uv_handle_t *)&worker->send, worker_retain(worker));

  err = uv_timer_init(worker->loop, &worker->wait);
  if (err) {
    logger_print_error("#%d: uv_timer_init(wait) failed: %s\n", worker->index, uv_strerror(err));
    uv_close((uv_handle_t *)&worker->term, worker_handle_closed);
    uv_close((uv_handle_t *)&worker->send, worker_handle_closed);
    return false;
  }
  uv_handle_set_data((uv_handle_t *)&worker->wait, worker_retain(worker));

  err = uv_udp_init(worker->loop, &worker->socket);
  if (err) {
    logger_print_error("#%d: uv_udp_init failed: %s\n", worker->index, uv_strerror(err));
    uv_close((uv_handle_t *)&worker->term, worker_handle_closed);
    uv_close((uv_handle_t *)&worker->send, worker_handle_closed);
    uv_close((uv_handle_t *)&worker->wait, worker_handle_closed);
    return false;
  }
  uv_handle_set_data((uv_handle_t *)&worker->socket, worker_retain(worker));

  uv_async_send(&worker->send);

  return true;
}

static void worker_async_term(uv_async_t *async) {
  assert(NULL != async);

  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)async);
  assert(NULL != worker);

  if (worker->threaded) {
    loop_stop(worker->loop);
  }

  worker_set_state(worker, worker_state_stopped);

  uv_cancel((uv_req_t *)&worker->send_request);
  uv_cancel((uv_req_t *)&worker->addr_request);

  uv_close((uv_handle_t *)&worker->term, worker_handle_closed);
  uv_close((uv_handle_t *)&worker->send, worker_handle_closed);
  uv_close((uv_handle_t *)&worker->wait, worker_handle_closed);
  uv_close((uv_handle_t *)&worker->socket, worker_handle_closed);
}

static void worker_thread_proc(worker_p worker) {
  assert(NULL != worker);
  assert(0 != worker->index);

  worker_retain(worker);

  uv_loop_t loop;
  int err = uv_loop_init(&loop);
  if (0 != err) {
    logger_print_error("#%d: uv_loop_init failed: %s\n", worker->index, uv_strerror(err));
    worker_set_state(worker, worker_state_failed);
    worker_release(worker);
    return;
  }

  worker->loop = &loop;

  bool initialized = worker_init(worker);
  if (!initialized) {
    worker_set_state(worker, worker_state_failed);
    worker_release(worker);
    return;
  }

  worker_set_state(worker, worker_state_ready);

  loop_run(&loop);
  loop_term(&loop, worker->index);

  worker_release(worker);
}

static const char *worker_get_state_name(worker_state_e state) {
  switch (state) {
  case worker_state_unknown:
    return "UNKNOWN";
  case worker_state_failed:
    return "FAILED";
  case worker_state_ready:
    return "READY";
  case worker_state_stopped:
    return "STOPPED";
  default:
    return "INVALID";
  }
}

static bool worker_is_stopped(worker_p worker) {
  assert(NULL != worker);

  worker_state_e state = worker_state_unknown;

  if (worker->threaded) {
    uv_mutex_lock(&worker->mutex);
    state = worker->state;
    uv_mutex_unlock(&worker->mutex);
  } else {
    state = worker->state;
  }

  return worker_state_stopped == state;
}

static void worker_set_state(worker_p worker, worker_state_e state) {
  assert(NULL != worker);

  if (worker->threaded) {
    uv_mutex_lock(&worker->mutex);

    if (worker->state != state) {
      logger_print_trace("#%d: Switch from '%s' to '%s'\n", worker->index, worker_get_state_name(worker->state),
                         worker_get_state_name(state));

      worker->state = state;

      if (worker_state_ready == state || worker_state_failed == state) {
        uv_cond_signal(&worker->cond);
      }
    }

    uv_mutex_unlock(&worker->mutex);
  } else {
    if (worker->state != state) {
      logger_print_trace("#%d: Switch from '%s' to '%s'\n", worker->index, worker_get_state_name(worker->state),
                         worker_get_state_name(state));

      worker->state = state;
    }
  }
}

static void worker_handle_closed(uv_handle_t *handle) {
  assert(NULL != handle);

  worker_p worker = (worker_p)uv_handle_get_data(handle);
  assert(NULL != worker);

  worker_release(worker);
}

static void worker_timer_timeout(uv_timer_t *timer) {
  assert(NULL != timer);

  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)timer);
  assert(NULL != worker);

  uv_timer_stop(&worker->wait);

  if (worker_is_stopped(worker)) {
    return;
  }

  int err = uv_async_send(&worker->send);
  if (err) {
    logger_print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(err));
    return;
  }
}

static void worker_async_send(uv_async_t *async) {
  assert(NULL != async);

  worker_p worker = (worker_p)uv_handle_get_data((uv_handle_t *)async);
  assert(NULL != worker);

  if (worker_is_stopped(worker)) {
    return;
  }

  worker->address[0] = 0;
  const char *address = g_arg_address;
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
    if (g_arg_is_ipv4) {
      sprintf_s(buffer, countof(buffer), "%d", random() % 256);
    } else {
      sprintf_s(buffer, countof(buffer), "%04x", random() % 65536);
    }

    strcat_s(worker->address, sizeof(worker->address), buffer);

    address = ptr + 1;
  }

  if (g_arg_port_min == g_arg_port_max) {
    sprintf_s(worker->port, countof(worker->port), "%d", g_arg_port_min);
  } else {
    sprintf_s(worker->port, countof(worker->port), "%d", g_arg_port_min + random() % (g_arg_port_max - g_arg_port_min + 1));
  }

  struct addrinfo hints = {0};
  hints.ai_family = g_arg_is_ipv4 ? AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_CANONNAME;

  int err =
      uv_getaddrinfo(worker->loop, &worker->addr_request, worker_request_addr_completed, worker->address, worker->port, &hints);
  if (err) {
    logger_print_error("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                       uv_strerror(err));
    return;
  }

  uv_req_set_data((uv_req_t *)&worker->addr_request, worker_retain(worker));
}

static void worker_request_addr_completed(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
  assert(NULL != req);

  worker_p worker = (worker_p)uv_req_get_data((uv_req_t *)req);

  if (worker_is_stopped(worker) || UV_ECANCELED == status) {
    worker_release(worker);
    return;
  }

  if (status) {
    logger_print_error("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                       uv_strerror(status));
    worker_release(worker);
    return;
  } else if (NULL == res) {
    logger_print_error("#%d: uv_getaddrinfo(%s, %s) failed: %s\n", worker->index, worker->address, worker->port,
                       uv_strerror(UV_EINVAL));
    worker_release(worker);
    return;
  }

  memcpy_s(&worker->sockaddr, sizeof(worker->sockaddr), res->ai_addr, res->ai_addrlen);

  uv_freeaddrinfo(res);

  int size = (g_arg_size_min == g_arg_size_max) ? (g_arg_size_min)
                                                : (g_arg_size_min + random() % (g_arg_size_max - g_arg_size_min + 1));

  int index = 0;
  for (index = 0; index < size; ++index) {
    worker->datagram[index] = random() % 256;
  }

  worker->buf.base = (char *)worker->datagram;
  worker->buf.len = size;

  logger_print_trace("#%d: Sending %d bytes to %s %s\n", worker->index, worker->buf.len, worker->address, worker->port);

  int err = uv_udp_send(&worker->send_request, &worker->socket, &worker->buf, 1, &worker->sockaddr.addr,
                        worker_request_send_completed);
  if (err) {
    logger_print_error("#%d: uv_udp_send(%s, %s) failed: %s\n", worker->index, worker->address, worker->port, uv_strerror(err));
    worker_release(worker);
    return;
  }

  uv_req_set_data((uv_req_t *)&worker->send_request, worker_retain(worker));

  worker_release(worker);
}

static void worker_request_send_completed(uv_udp_send_t *req, int status) {
  assert(NULL != req);

  worker_p worker = (worker_p)uv_req_get_data((uv_req_t *)req);
  assert(NULL != worker);

  if (worker_is_stopped(worker) || UV_ECANCELED == status) {
    worker_release(worker);
    return;
  }

  custom_atomic_fetch_add(&g_stats_sent_operations, 1);
  custom_atomic_fetch_add(&g_stats_sent_bytes, worker->buf.len);

  if (0 == g_arg_timeout_ms) {
    int err = uv_async_send(&worker->send);
    if (err) {
      logger_print_error("#%d: uv_async_send failed: %s\n", worker->index, uv_strerror(err));
      worker_release(worker);
      return;
    }
  } else {
    logger_print_trace("#%d: Waiting for %dms\n", worker->index, g_arg_timeout_ms);

    int err = uv_timer_start(&worker->wait, worker_timer_timeout, g_arg_timeout_ms, 0);
    if (err) {
      logger_print_error("#%d: uv_timer_start failed: %s\n", worker->index, uv_strerror(err));
      worker_release(worker);
      return;
    }
  }

  worker_release(worker);
}
