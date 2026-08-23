// ai_logger.c - Unified Activity Logger implementation
#include "ai_logger.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

static SDL_Mutex *logger_mutex = NULL;
static FILE *log_file = NULL;

static char log_ring[AI_LOGGER_MAX_ENTRIES][AI_LOGGER_LINE_MAX];
static int ring_head = 0;
static int ring_count = 0;
static int logger_initialized = 0;

static void ensure_parent_dir_exists(const char *filepath) {
  char dir[1024];
  strncpy(dir, filepath, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';

  char *last_slash = strrchr(dir, '/');
  char *last_bslash = strrchr(dir, '\\');
  char *sep = (last_slash > last_bslash) ? last_slash : last_bslash;

  if (sep) {
    *sep = '\0';
#ifdef _WIN32
    for (char *p = dir; *p; p++) {
      if (*p == '/' || *p == '\\') {
        char c = *p;
        *p = '\0';
        if (strlen(dir) > 0 && !(strlen(dir) == 2 && dir[1] == ':')) {
          CreateDirectoryA(dir, NULL);
        }
        *p = c;
      }
    }
    if (strlen(dir) > 0 && !(strlen(dir) == 2 && dir[1] == ':')) {
      CreateDirectoryA(dir, NULL);
    }
#else
    for (char *p = dir; *p; p++) {
      if (*p == '/') {
        *p = '\0';
        if (strlen(dir) > 0) {
          mkdir(dir, 0755);
        }
        *p = '/';
      }
    }
    if (strlen(dir) > 0) {
      mkdir(dir, 0755);
    }
#endif
  }
}

static void format_timestamp(char *buf, size_t buf_len) {
#ifdef _WIN32
  SYSTEMTIME st;
  GetLocalTime(&st);
  snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
           st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm *tm_info = localtime(&tv.tv_sec);
  char time_str[32];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
  snprintf(buf, buf_len, "%s.%03d", time_str, (int)(tv.tv_usec / 1000));
#endif
}

int ai_logger_init(const char *log_filepath) {
  if (!logger_mutex) {
    logger_mutex = SDL_CreateMutex();
  }

  if (logger_mutex) SDL_LockMutex(logger_mutex);

  ring_head = 0;
  ring_count = 0;
  memset(log_ring, 0, sizeof(log_ring));

  if (log_file) {
    fclose(log_file);
    log_file = NULL;
  }

  char resolved_path[1024] = {0};
  if (log_filepath && strlen(log_filepath) > 0) {
    // If filename has no folder prefix, put it in logs/ folder
    if (strchr(log_filepath, '/') == NULL && strchr(log_filepath, '\\') == NULL) {
      snprintf(resolved_path, sizeof(resolved_path), "logs/%s", log_filepath);
    } else {
      strncpy(resolved_path, log_filepath, sizeof(resolved_path) - 1);
      resolved_path[sizeof(resolved_path) - 1] = '\0';
    }

    ensure_parent_dir_exists(resolved_path);
    log_file = fopen(resolved_path, "w");
    if (!log_file) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open log file '%s'", resolved_path);
    }
  }

  logger_initialized = 1;

  if (logger_mutex) SDL_UnlockMutex(logger_mutex);

  ai_log("SYS", "Logger initialized%s%s", resolved_path[0] ? " with file: " : "", resolved_path[0] ? resolved_path : "");
  return 1;
}

void ai_logger_shutdown(void) {
  if (logger_mutex) SDL_LockMutex(logger_mutex);

  if (log_file) {
    fflush(log_file);
    fclose(log_file);
    log_file = NULL;
  }

  logger_initialized = 0;

  if (logger_mutex) {
    SDL_UnlockMutex(logger_mutex);
  }
}

void ai_log(const char *category, const char *fmt, ...) {
  char time_buf[32];
  format_timestamp(time_buf, sizeof(time_buf));

  char msg_buf[AI_LOGGER_LINE_MAX - 64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
  va_end(args);

  char full_line[AI_LOGGER_LINE_MAX];
  snprintf(full_line, sizeof(full_line), "[%s] [%-6s] %s",
           time_buf, category ? category : "APP", msg_buf);

  if (!logger_mutex) {
    logger_mutex = SDL_CreateMutex();
  }
  if (logger_mutex) SDL_LockMutex(logger_mutex);

  // Print to stdout
  printf("%s\n", full_line);
  fflush(stdout);

  // Write to log file if open
  if (log_file) {
    fputs(full_line, log_file);
    fputc('\n', log_file);
    fflush(log_file);
  }

  // Store in in-memory ring buffer
  int idx = (ring_head + ring_count) % AI_LOGGER_MAX_ENTRIES;
  if (ring_count < AI_LOGGER_MAX_ENTRIES) {
    ring_count++;
  } else {
    ring_head = (ring_head + 1) % AI_LOGGER_MAX_ENTRIES;
  }
  snprintf(log_ring[idx], sizeof(log_ring[idx]), "%s", full_line);

  if (logger_mutex) SDL_UnlockMutex(logger_mutex);
}

int ai_logger_get_recent(char *out_buf, size_t out_len, int max_lines) {
  if (!out_buf || out_len == 0 || max_lines <= 0) {
    return 0;
  }

  if (logger_mutex) SDL_LockMutex(logger_mutex);

  int available = ring_count;
  int lines_to_copy = (max_lines < available) ? max_lines : available;
  int start_idx = (ring_head + (available - lines_to_copy)) % AI_LOGGER_MAX_ENTRIES;

  out_buf[0] = '\0';
  size_t offset = 0;
  int copied = 0;

  for (int i = 0; i < lines_to_copy; i++) {
    int cur_idx = (start_idx + i) % AI_LOGGER_MAX_ENTRIES;
    size_t line_len = strlen(log_ring[cur_idx]);
    if (offset + line_len + 2 >= out_len) {
      break;
    }
    memcpy(out_buf + offset, log_ring[cur_idx], line_len);
    offset += line_len;
    out_buf[offset++] = '\n';
    out_buf[offset] = '\0';
    copied++;
  }

  if (logger_mutex) SDL_UnlockMutex(logger_mutex);
  return copied;
}

void ai_logger_flush(void) {
  if (logger_mutex) SDL_LockMutex(logger_mutex);
  fflush(stdout);
  if (log_file) {
    fflush(log_file);
  }
  if (logger_mutex) SDL_UnlockMutex(logger_mutex);
}
