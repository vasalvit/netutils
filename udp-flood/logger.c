#include "./logger.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int g_logger_level = LOGGER_LEVEL_INFO;

void logger_print_error(const char *format, ...) {
  assert(NULL != format);

  if (g_logger_level < LOGGER_LEVEL_ERROR) {
    return;
  }

  va_list args;
  va_start(args, format);

  vprintf_s(format, args);

  va_end(args);
}

void logger_print_info(const char *format, ...) {
  assert(NULL != format);

  if (g_logger_level < LOGGER_LEVEL_INFO) {
    return;
  }

  va_list args;
  va_start(args, format);

  vprintf_s(format, args);

  va_end(args);
}

void logger_print_trace(const char *format, ...) {
  assert(NULL != format);

  if (g_logger_level < LOGGER_LEVEL_TRACE) {
    return;
  }

  va_list args;
  va_start(args, format);

  vprintf_s(format, args);

  va_end(args);
}
