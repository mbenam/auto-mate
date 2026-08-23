// ai_logger.h - Unified Activity Logger for m8c and AI Agent
#ifndef AI_LOGGER_H_
#define AI_LOGGER_H_

#include <stddef.h>
#include <stdint.h>

#define AI_LOGGER_MAX_ENTRIES 256
#define AI_LOGGER_LINE_MAX 256

// Initialize the logger with an optional file output path (pass NULL for stdout/ringbuffer only)
int ai_logger_init(const char *log_filepath);

// Shutdown logger and close file handle
void ai_logger_shutdown(void);

// Log an event with category and printf formatting
void ai_log(const char *category, const char *fmt, ...);

// Retrieve up to max_lines of recent log history into out_buf (thread-safe)
// Returns number of lines written
int ai_logger_get_recent(char *out_buf, size_t out_len, int max_lines);

// Flush log outputs
void ai_logger_flush(void);

#endif // AI_LOGGER_H_
