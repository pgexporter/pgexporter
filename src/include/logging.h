/*
 * Copyright (C) 2026 The pgexporter community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGEXPORTER_LOGGING_H
#define PGEXPORTER_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdbool.h>
#include <stdlib.h>

#define PGEXPORTER_LOGGING_TYPE_CONSOLE            0
#define PGEXPORTER_LOGGING_TYPE_FILE               1
#define PGEXPORTER_LOGGING_TYPE_SYSLOG             2

#define PGEXPORTER_LOGGING_LEVEL_DEBUG5            1
#define PGEXPORTER_LOGGING_LEVEL_DEBUG4            1
#define PGEXPORTER_LOGGING_LEVEL_DEBUG3            1
#define PGEXPORTER_LOGGING_LEVEL_DEBUG2            1
#define PGEXPORTER_LOGGING_LEVEL_DEBUG1            2
#define PGEXPORTER_LOGGING_LEVEL_INFO              3
#define PGEXPORTER_LOGGING_LEVEL_WARN              4
#define PGEXPORTER_LOGGING_LEVEL_ERROR             5
#define PGEXPORTER_LOGGING_LEVEL_FATAL             6

#define PGEXPORTER_LOGGING_MODE_CREATE             0
#define PGEXPORTER_LOGGING_MODE_APPEND             1

#define PGEXPORTER_LOGGING_ROTATION_DISABLED       0

#define PGEXPORTER_LOGGING_DEFAULT_LOG_LINE_PREFIX "%Y-%m-%d %H:%M:%S"

#define pgexporter_log_trace(...)                  pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_DEBUG5, __FILE__, __LINE__, __VA_ARGS__)
#define pgexporter_log_debug(...)                  pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_DEBUG1, __FILE__, __LINE__, __VA_ARGS__)
#define pgexporter_log_info(...)                   pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define pgexporter_log_warn(...)                   pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define pgexporter_log_error(...)                  pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define pgexporter_log_fatal(...)                  pgexporter_log_line(PGEXPORTER_LOGGING_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/**
 * Start the logging system
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_start_logging(void);

/**
 * Stop the logging system
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_stop_logging(void);

/**
 * Is the logging level enabled
 * @param level The level
 * @return True if enabled, otherwise false
 */
bool
pgexporter_log_is_enabled(int level);

/**
 * Log a line
 * @param level The level
 * @param file The file
 * @param line The line number
 * @param fmt The formatting code
 * @return 0 upon success, otherwise 1
 */
void
pgexporter_log_line(int level, char* file, int line, char* fmt, ...);

/**
 * Log a memory segment
 * @param data The data
 * @param size The size
 * @return 0 upon success, otherwise 1
 */
void
pgexporter_log_mem(void* data, size_t size);

/**
 * Print n bytes after ptr in binary format
 * @param ptr Pointer to the bytes
 * @param n Number of bytes
 */
void
pgexporter_print_bytes_binary(void* ptr, size_t n);

#ifdef __cplusplus
}
#endif

#endif
