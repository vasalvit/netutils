#pragma once

#define LOGGER_LEVEL_ERROR 1
#define LOGGER_LEVEL_INFO 2
#define LOGGER_LEVEL_TRACE 3

extern int g_logger_level;

extern void logger_print_error(const char *format, ...);
extern void logger_print_info(const char *format, ...);
extern void logger_print_trace(const char *format, ...);
