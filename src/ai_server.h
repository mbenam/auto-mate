// ai_server.h - AI Agent Local TCP Server for m8c
#ifndef AI_SERVER_H_
#define AI_SERVER_H_

#include <stdint.h>
#include <SDL3/SDL.h>
#include "common.h"

#define AI_SERVER_PORT 9123
#define AI_SERVER_HOST "127.0.0.1"

#define AI_SCREENSHOT_WIDTH 320
#define AI_SCREENSHOT_HEIGHT 240
#define AI_SCREENSHOT_BYTES_PER_PIXEL 3
#define AI_SCREENSHOT_BUFFER_SIZE (AI_SCREENSHOT_WIDTH * AI_SCREENSHOT_HEIGHT * AI_SCREENSHOT_BYTES_PER_PIXEL) // 230,400 bytes

// Custom SDL Event IDs
extern Uint32 AI_INPUT_EVENT;
extern Uint32 AI_SCREENSHOT_EVENT;

// Global shared pixel buffer (230,400 bytes RGB24)
extern void *shared_pixel_buffer;

// Initialize the AI server subsystem and start the TCP listener thread
int ai_server_init(void);

// Stop the AI server thread and clean up socket resources
void ai_server_shutdown(void);

// Handle AI_INPUT_EVENT in the main SDL thread
void ai_server_handle_event(struct app_context *ctx, const SDL_Event *event);

// Handle AI_SCREENSHOT_EVENT in the main SDL thread
void ai_server_handle_screenshot(struct app_context *ctx);

// Feature D: Direct Audio to WAV Recording
extern bool ai_is_recording;
extern FILE *ai_wav_file;
extern uint32_t wav_data_size;

int ai_server_rec_start(const char *filename);
int ai_server_rec_stop(uint32_t *out_size);
void ai_server_record_audio(const void *audio_buffer, size_t byte_count);

// Parse key string into bitmask (public for testing)
int ai_server_parse_key_combination(const char *input, uint8_t *out_mask, char *err_buf, size_t err_buf_len);

#endif // AI_SERVER_H_
