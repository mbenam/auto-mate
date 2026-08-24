// ai_screen.h - Virtual Screen Rebuilder & Cursor State Tracker for M8
#ifndef AI_SCREEN_H_
#define AI_SCREEN_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define M8_SCREEN_ROWS 30
#define M8_SCREEN_COLS 40

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char text[M8_SCREEN_ROWS][M8_SCREEN_COLS + 1]; // 40 cols + null terminator per row
  uint8_t fg_r[M8_SCREEN_ROWS][M8_SCREEN_COLS];
  uint8_t fg_g[M8_SCREEN_ROWS][M8_SCREEN_COLS];
  uint8_t fg_b[M8_SCREEN_ROWS][M8_SCREEN_COLS];
  uint8_t bg_r[M8_SCREEN_ROWS][M8_SCREEN_COLS];
  uint8_t bg_g[M8_SCREEN_ROWS][M8_SCREEN_COLS];
  uint8_t bg_b[M8_SCREEN_ROWS][M8_SCREEN_COLS];

  int cursor_col;
  int cursor_row;
  int cursor_width;
  int cursor_height;

  uint8_t bg_theme_r;
  uint8_t bg_theme_g;
  uint8_t bg_theme_b;

  char active_screen[32];
  char active_input[32];
  char current_value[32];
  char header_text[64];
  char play_state[16];
} m8_screen_state_s;

// Initialize virtual screen tracking system
void ai_screen_init(void);

// Shut down and clean up mutexes
void ai_screen_shutdown(void);

// Clear or reset virtual screen state
void ai_screen_reset(void);

// Intercept DRAW CHARACTER command from M8 SLIP stream
void ai_screen_on_draw_char(int c, int px_x, int px_y,
                            uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
                            uint8_t bg_r, uint8_t bg_g, uint8_t bg_b);

// Intercept DRAW RECTANGLE command from M8 SLIP stream
void ai_screen_on_draw_rect(int px_x, int px_y, int w, int h,
                            uint8_t r, uint8_t g, uint8_t b);

// Intercept DRAW OSCILLOSCOPE WAVEFORM command from M8 SLIP stream
void ai_screen_on_waveform(const uint8_t *data, uint16_t size);

// Get current state formatted as rich JSON for LLM / AI agent consumption
int ai_screen_get_state_json(char *out_buf, size_t out_len);

// Get full 40x30 text screen grid (marked_cursor=1 puts [brackets] around cursor target)
int ai_screen_get_text_grid(char *out_buf, size_t out_len, int marked_cursor);

// Fast query for cursor coordinates and active input field
void ai_screen_get_cursor(int *out_col, int *out_row, char *out_input, size_t input_len);

#ifdef __cplusplus
}
#endif

#endif // AI_SCREEN_H_
