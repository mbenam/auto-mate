// ai_screen.c - Virtual Screen Rebuilder & Cursor State Tracker for M8
#include "ai_screen.h"
#include "ai_logger.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static m8_screen_state_s g_screen_state;
static SDL_Mutex *screen_mutex = NULL;
static int screen_initialized = 0;

void ai_screen_init(void) {
  if (!screen_mutex) {
    screen_mutex = SDL_CreateMutex();
  }

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  memset(&g_screen_state, 0, sizeof(g_screen_state));
  for (int r = 0; r < M8_SCREEN_ROWS; r++) {
    memset(g_screen_state.text[r], ' ', M8_SCREEN_COLS);
    g_screen_state.text[r][M8_SCREEN_COLS] = '\0';
  }

  g_screen_state.cursor_col = -1;
  g_screen_state.cursor_row = -1;
  g_screen_state.cursor_width = 1;
  g_screen_state.cursor_height = 1;

  snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "UNKNOWN");
  snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "UNKNOWN_INPUT");
  snprintf(g_screen_state.play_state, sizeof(g_screen_state.play_state), "STOPPED");

  screen_initialized = 1;

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
  ai_log("SYS", "Virtual screen state tracker initialized (%dx%d grid)", M8_SCREEN_COLS, M8_SCREEN_ROWS);
}

void ai_screen_shutdown(void) {
  screen_initialized = 0;
  if (screen_mutex) {
    SDL_LockMutex(screen_mutex);
    SDL_UnlockMutex(screen_mutex);
    // Keep mutex alive for safety during test cycles
  }
}

void ai_screen_reset(void) {
  if (!screen_mutex) screen_mutex = SDL_CreateMutex();
  if (screen_mutex) SDL_LockMutex(screen_mutex);

  for (int r = 0; r < M8_SCREEN_ROWS; r++) {
    memset(g_screen_state.text[r], ' ', M8_SCREEN_COLS);
    g_screen_state.text[r][M8_SCREEN_COLS] = '\0';
  }
  g_screen_state.cursor_col = -1;
  g_screen_state.cursor_row = -1;

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}

void ai_screen_on_draw_char(int c, int px_x, int px_y,
                            uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
                            uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
  if (!screen_initialized) ai_screen_init();

  int col = px_x / 8;
  int row = px_y / 8;

  if (col < 0 || col >= M8_SCREEN_COLS || row < 0 || row >= M8_SCREEN_ROWS) {
    return;
  }

  char ascii_char = ' ';
  if (c >= 32 && c <= 126) {
    ascii_char = (char)c;
  } else if (c == 0) {
    ascii_char = ' ';
  } else {
    // Other glyph or block character
    ascii_char = (char)(c & 0x7F);
    if (ascii_char < 32 || ascii_char > 126) ascii_char = '?';
  }

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  g_screen_state.text[row][col] = ascii_char;
  g_screen_state.fg_r[row][col] = fg_r;
  g_screen_state.fg_g[row][col] = fg_g;
  g_screen_state.fg_b[row][col] = fg_b;
  g_screen_state.bg_r[row][col] = bg_r;
  g_screen_state.bg_g[row][col] = bg_g;
  g_screen_state.bg_b[row][col] = bg_b;

  // Inverted selection highlight detection (e.g. in file browser / name picker modals)
  if ((bg_r > 50 || bg_g > 50 || bg_b > 50) && px_x < 240) {
    if (row >= 0 && row < M8_SCREEN_ROWS && col >= 0 && col < M8_SCREEN_COLS) {
      g_screen_state.cursor_col = col;
      g_screen_state.cursor_row = row;
      g_screen_state.cursor_width = 1;
    }
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}

// Internal state to aggregate cursor corner brackets (TL, TR, BL, BR)
static int g_cur_min_x = -1;
static int g_cur_min_y = -1;
static int g_cur_max_x = -1;
static int g_cur_max_y = -1;
static Uint64 g_last_corner_tick = 0;

void ai_screen_on_draw_rect(int px_x, int px_y, int w, int h,
                            uint8_t r, uint8_t g, uint8_t b) {
  if (!screen_initialized) ai_screen_init();

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  // Fullscreen background fill / screen clear
  if (px_x == 0 && px_y <= 0 && w >= 300 && h >= 220) {
    g_screen_state.bg_theme_r = r;
    g_screen_state.bg_theme_g = g;
    g_screen_state.bg_theme_b = b;
    // Clear text grid
    for (int row = 0; row < M8_SCREEN_ROWS; row++) {
      memset(g_screen_state.text[row], ' ', M8_SCREEN_COLS);
      g_screen_state.text[row][M8_SCREEN_COLS] = '\0';
    }
    g_cur_min_x = -1;
    g_cur_min_y = -1;
    g_cur_max_x = -1;
    g_cur_max_y = -1;
  } else if (w > 0 && h > 0 && px_x < 240) {
    Uint64 now = SDL_GetTicks();

    // 1. Check for small corner brackets of the M8 cursor (e.g. w <= 4, h <= 4)
    if (w <= 4 && h <= 4) {
      // If close to active corner cluster and within 150ms, aggregate
      if (g_cur_min_x >= 0 && (now - g_last_corner_tick < 150) &&
          abs(px_x - g_cur_min_x) <= 180 && abs(px_y - g_cur_min_y) <= 18) {
        if (px_x < g_cur_min_x) g_cur_min_x = px_x;
        if (px_y < g_cur_min_y) g_cur_min_y = px_y;
        if (px_x + w > g_cur_max_x) g_cur_max_x = px_x + w;
        if (px_y + h > g_cur_max_y) g_cur_max_y = px_y + h;
      } else {
        // Start a new corner cluster
        g_cur_min_x = px_x;
        g_cur_min_y = px_y;
        g_cur_max_x = px_x + w;
        g_cur_max_y = px_y + h;
      }
      g_last_corner_tick = now;

      // The true cursor position is always at the top-left minimum (min_x, min_y)
      int col = g_cur_min_x / 8;
      int row = g_cur_min_y / 8;
      int cols_wide = (g_cur_max_x - g_cur_min_x + 7) / 8;
      if (cols_wide < 1) cols_wide = 1;
      if (cols_wide > 20) cols_wide = 20;

      if (col >= 0 && col < M8_SCREEN_COLS && row >= 0 && row < M8_SCREEN_ROWS) {
        g_screen_state.cursor_col = col;
        g_screen_state.cursor_row = row;
        g_screen_state.cursor_width = cols_wide;
        g_screen_state.cursor_height = 1;
      }
    }
    // 2. Check for solid block selection highlights (e.g. in menus / project view)
    else if (w >= 6 && h >= 6 && w <= 220 && h <= 18) {
      int col = px_x / 8;
      int row = px_y / 8;
      int cols_wide = (w + 7) / 8;
      if (cols_wide < 1) cols_wide = 1;
      if (cols_wide > 20) cols_wide = 20;

      if (col >= 0 && col < M8_SCREEN_COLS && row >= 0 && row < M8_SCREEN_ROWS) {
        g_screen_state.cursor_col = col;
        g_screen_state.cursor_row = row;
        g_screen_state.cursor_width = cols_wide;
        g_screen_state.cursor_height = 1;
      }
    }
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}

static void str_trim(char *str) {
  char *start = str;
  while (isspace((unsigned char)*start)) start++;
  if (start != str) memmove(str, start, strlen(start) + 1);
  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) str[--len] = '\0';
}

static void extract_token_at(const char *line, int col, char *out_buf, size_t out_len) {
  out_buf[0] = '\0';
  if (!line || col < 0 || col >= (int)strlen(line)) return;

  int len = (int)strlen(line);
  int c = col;

  // If pointing to space, check adjacent characters
  if (line[c] == ' ') {
    if (c + 1 < len && line[c + 1] != ' ') {
      c = c + 1;
    } else if (c - 1 >= 0 && line[c - 1] != ' ') {
      c = c - 1;
    } else {
      out_buf[0] = '\0';
      return;
    }
  }

  int start = c;
  int end = c;

  while (start > 0 && line[start - 1] != ' ') start--;
  while (end < len && line[end] != ' ') end++;

  int token_len = end - start;
  if (token_len > (int)out_len - 1) token_len = (int)out_len - 1;
  memcpy(out_buf, line + start, token_len);
  out_buf[token_len] = '\0';
}

static void extract_left_label(const char *line, int col, char *out_buf, size_t out_len) {
  out_buf[0] = '\0';
  if (!line || col <= 0) return;

  // Scan backwards past current token and spaces
  int p = col - 1;
  while (p >= 0 && line[p] != ' ') p--; // past previous word if cursor was on value
  while (p >= 0 && line[p] == ' ') p--; // past spaces

  if (p < 0) return;

  int end = p + 1;
  while (p > 0 && line[p - 1] != ' ') p--;
  int start = p;

  int label_len = end - start;
  if (label_len > (int)out_len - 1) label_len = (int)out_len - 1;
  memcpy(out_buf, line + start, label_len);
  out_buf[label_len] = '\0';
}

// Static UI Field mapping for canonical screens
typedef struct {
  const char *screen_prefix;
  int min_row;
  int max_row;
  int min_col;
  int max_col;
  const char *input_id;
} m8_static_field_map_s;

static const m8_static_field_map_s g_field_map[] = {
    // PHRASE Screen (Steps 00..0F on rows 3..18)
    {"PHRASE", 3, 18, 0, 1, "STEP"},
    {"PHRASE", 3, 18, 2, 5, "NOTE"},
    {"PHRASE", 3, 18, 6, 8, "VELOCITY"},
    {"PHRASE", 3, 18, 9, 11, "INSTRUMENT"},
    {"PHRASE", 3, 18, 12, 16, "FX1"},
    {"PHRASE", 3, 18, 17, 21, "FX2"},
    {"PHRASE", 3, 18, 22, 26, "FX3"},

    // CHAIN Screen (Steps 00..0F on rows 3..18)
    {"CHAIN", 3, 18, 0, 1, "STEP"},
    {"CHAIN", 3, 18, 2, 4, "PHRASE"},
    {"CHAIN", 3, 18, 5, 7, "TRANSPOSE"},

    // SONG Screen (Tracks 1..8 on rows 3..24)
    {"SONG", 3, 24, 0, 1, "ROW"},
    {"SONG", 3, 24, 2, 4, "TRACK1"},
    {"SONG", 3, 24, 5, 7, "TRACK2"},
    {"SONG", 3, 24, 8, 10, "TRACK3"},
    {"SONG", 3, 24, 11, 13, "TRACK4"},
    {"SONG", 3, 24, 14, 16, "TRACK5"},
    {"SONG", 3, 24, 17, 19, "TRACK6"},
    {"SONG", 3, 24, 20, 22, "TRACK7"},
    {"SONG", 3, 24, 23, 25, "TRACK8"},

    // PROJECT Screen
    {"PROJECT", 3, 5, 13, 24, "TEMPO"},
    {"PROJECT", 6, 6, 13, 24, "TRANSPOSE"},
    {"PROJECT", 7, 7, 13, 26, "GROOVE"},
    {"PROJECT", 8, 9, 13, 28, "SCALE"},
    {"PROJECT", 10, 11, 13, 28, "LIVE_QUANTIZ"},
    {"PROJECT", 12, 13, 12, 21, "MIDI_SETTINGS"},
    {"PROJECT", 12, 13, 22, 32, "MIDI_MAPPINGS"},
    {"PROJECT", 14, 15, 12, 26, "SONG_NAME"},
    {"PROJECT", 16, 16, 12, 17, "PROJECT_LOAD"},
    {"PROJECT", 16, 16, 18, 22, "PROJECT_SAVE"},
    {"PROJECT", 16, 16, 23, 28, "PROJECT_NEW"},
    {"PROJECT", 17, 17, 12, 19, "EXPORT_RENDER"},
    {"PROJECT", 17, 17, 20, 28, "EXPORT_BUNDLE"},
    {"PROJECT", 18, 18, 12, 20, "CLEAR_PHRASES"},
    {"PROJECT", 18, 18, 21, 30, "CLEAR_INST_TBL"},
    {"PROJECT", 19, 20, 12, 30, "VIEW_INST_POOL"},
    {"PROJECT", 21, 21, 12, 30, "VIEW_TIME_STATS"},
    {"PROJECT", 22, 23, 12, 30, "SYSTEM_SETTINGS"},

    // TABLE Screen (Steps 00..0F on rows 3..18)
    {"TABLE", 3, 18, 0, 1, "STEP"},
    {"TABLE", 3, 18, 2, 5, "NOTE"},
    {"TABLE", 3, 18, 6, 8, "VOLUME"},
    {"TABLE", 3, 18, 9, 13, "FX1"},
    {"TABLE", 3, 18, 14, 18, "FX2"},

    // GROOVE Screen
    {"GROOVE", 3, 18, 0, 1, "STEP"},
    {"GROOVE", 3, 18, 3, 6, "TICKS"},

    // KEYBOARD / Name Picker
    {"KEYBOARD", 2, 6, 0, 39, "NAME_BUFFER"},
    {"KEYBOARD", 8, 22, 0, 39, "KEY_CHAR"},
};

static void analyze_screen_state(void) {
  // 1. Identify Screen from rows 0 to 4
  char header_raw[M8_SCREEN_COLS + 1] = {0};
  for (int r = 0; r <= 4; r++) {
    char temp[M8_SCREEN_COLS + 1];
    snprintf(temp, sizeof(temp), "%s", g_screen_state.text[r]);
    str_trim(temp);
    if (strlen(temp) > 0) {
      snprintf(header_raw, sizeof(header_raw), "%s", temp);
      break;
    }
  }
  snprintf(g_screen_state.header_text, sizeof(g_screen_state.header_text), "%s", header_raw);

  // Check play state in header
  if (strstr(header_raw, ">") != NULL || strstr(header_raw, "PLAY") != NULL) {
    snprintf(g_screen_state.play_state, sizeof(g_screen_state.play_state), "PLAYING");
  } else {
    snprintf(g_screen_state.play_state, sizeof(g_screen_state.play_state), "STOPPED");
  }

  // Check for Keyboard / Name Picker Modal
  int is_keyboard = 0;
  for (int r = 10; r < 25; r++) {
    if (strstr(g_screen_state.text[r], "SPACE") != NULL || strstr(g_screen_state.text[r], "CANCEL") != NULL) {
      is_keyboard = 1;
      break;
    }
  }

  if (is_keyboard) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "KEYBOARD");
  } else if (strncmp(header_raw, "SONG", 4) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "SONG");
  } else if (strncmp(header_raw, "CHAIN", 5) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "CHAIN");
  } else if (strncmp(header_raw, "PHRASE", 6) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "PHRASE");
  } else if (strncmp(header_raw, "TABLE", 5) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "TABLE");
  } else if (strncmp(header_raw, "GROOVE", 6) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "GROOVE");
  } else if (strncmp(header_raw, "SCALE", 5) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "SCALE");
  } else if (strncmp(header_raw, "EFFECTS", 7) == 0 || strncmp(header_raw, "FX", 2) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "EFFECTS");
  } else if (strncmp(header_raw, "PROJECT", 7) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "PROJECT");
  } else if (strncmp(header_raw, "MIXER", 5) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "MIXER");
  } else if (strncmp(header_raw, "INST", 4) == 0 || strstr(header_raw, "SYNTH") != NULL ||
             strstr(header_raw, "SAMPLER") != NULL || strstr(header_raw, "WAVSYN") != NULL ||
             strstr(header_raw, "MACRO") != NULL || strstr(header_raw, "FMSYN") != NULL ||
             strstr(header_raw, "HYPER") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "INSTRUMENT");
  } else if (strstr(header_raw, "LOAD") != NULL || strstr(header_raw, "SAVE") != NULL ||
             strstr(header_raw, "IMPORT") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "FILE_BROWSER");
  } else {
    // Default to first word of header
    char first_word[32] = {0};
    sscanf(header_raw, "%31s", first_word);
    if (strlen(first_word) > 0) {
      snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "%s", first_word);
    } else {
      snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "UNKNOWN");
    }
  }

  // 2. Resolve Active Input & Value
  int col = g_screen_state.cursor_col;
  int row = g_screen_state.cursor_row;

  if (col >= 0 && col < M8_SCREEN_COLS && row >= 0 && row < M8_SCREEN_ROWS) {
    extract_token_at(g_screen_state.text[row], col, g_screen_state.current_value,
                     sizeof(g_screen_state.current_value));

    // Try static UI map first
    int matched = 0;
    int map_count = sizeof(g_field_map) / sizeof(g_field_map[0]);
    for (int i = 0; i < map_count; i++) {
      if (strncmp(g_screen_state.active_screen, g_field_map[i].screen_prefix,
                  strlen(g_field_map[i].screen_prefix)) == 0) {
        if (row >= g_field_map[i].min_row && row <= g_field_map[i].max_row &&
            col >= g_field_map[i].min_col && col <= g_field_map[i].max_col) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "%s",
                   g_field_map[i].input_id);
          matched = 1;
          break;
        }
      }
    }

    // If not matched, try Left-Label scanning heuristic
    if (!matched) {
      char left_label[32] = {0};
      extract_left_label(g_screen_state.text[row], col, left_label, sizeof(left_label));
      if (strlen(left_label) > 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "%s", left_label);
      } else {
        // Fallback to row/col coordinate
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CELL_R%02d_C%02d", row, col);
      }
    }
  } else {
    snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NO_CURSOR");
    snprintf(g_screen_state.current_value, sizeof(g_screen_state.current_value), "");
  }
}

int ai_screen_get_state_json(char *out_buf, size_t out_len) {
  if (!out_buf || out_len == 0) return 0;
  if (!screen_initialized) ai_screen_init();

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  analyze_screen_state();

  char cursor_line[M8_SCREEN_COLS + 16] = {0};
  int row = g_screen_state.cursor_row;
  int col = g_screen_state.cursor_col;

  if (row >= 0 && row < M8_SCREEN_ROWS) {
    const char *orig = g_screen_state.text[row];
    if (col >= 0 && col < M8_SCREEN_COLS) {
      int w = g_screen_state.cursor_width > 0 ? g_screen_state.cursor_width : 1;
      // Build marked line: "prefix [val] suffix"
      char pre[M8_SCREEN_COLS + 1] = {0};
      char mid[M8_SCREEN_COLS + 1] = {0};
      char post[M8_SCREEN_COLS + 1] = {0};

      int pre_len = (col < M8_SCREEN_COLS) ? col : M8_SCREEN_COLS;
      memcpy(pre, orig, pre_len);
      pre[pre_len] = '\0';

      int mid_len = (col + w <= M8_SCREEN_COLS) ? w : (M8_SCREEN_COLS - col);
      if (mid_len < 1) mid_len = 1;
      memcpy(mid, orig + col, mid_len);
      mid[mid_len] = '\0';

      if (col + mid_len < M8_SCREEN_COLS) {
        snprintf(post, sizeof(post), "%s", orig + col + mid_len);
      }

      snprintf(cursor_line, sizeof(cursor_line), "%s[%s]%s", pre, mid, post);
    } else {
      snprintf(cursor_line, sizeof(cursor_line), "%s", orig);
    }
  }

  // Escape any quotes in string fields
  char esc_header[128] = {0};
  char esc_value[64] = {0};
  char esc_line[128] = {0};

  for (size_t i = 0, j = 0; i < strlen(g_screen_state.header_text) && j < sizeof(esc_header) - 2; i++) {
    if (g_screen_state.header_text[i] == '"' || g_screen_state.header_text[i] == '\\') esc_header[j++] = '\\';
    esc_header[j++] = g_screen_state.header_text[i];
  }
  for (size_t i = 0, j = 0; i < strlen(g_screen_state.current_value) && j < sizeof(esc_value) - 2; i++) {
    if (g_screen_state.current_value[i] == '"' || g_screen_state.current_value[i] == '\\') esc_value[j++] = '\\';
    esc_value[j++] = g_screen_state.current_value[i];
  }
  for (size_t i = 0, j = 0; i < strlen(cursor_line) && j < sizeof(esc_line) - 2; i++) {
    if (cursor_line[i] == '"' || cursor_line[i] == '\\') esc_line[j++] = '\\';
    esc_line[j++] = cursor_line[i];
  }

  int written = snprintf(out_buf, out_len,
                         "{\"screen\":\"%s\","
                         "\"cursor_col\":%d,"
                         "\"cursor_row\":%d,"
                         "\"cursor_width\":%d,"
                         "\"input\":\"%s\","
                         "\"value\":\"%s\","
                         "\"header\":\"%s\","
                         "\"play_state\":\"%s\","
                         "\"cursor_text_line\":\"%s\"}",
                         g_screen_state.active_screen,
                         g_screen_state.cursor_col,
                         g_screen_state.cursor_row,
                         g_screen_state.cursor_width,
                         g_screen_state.active_input,
                         esc_value,
                         esc_header,
                         g_screen_state.play_state,
                         esc_line);

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
  return written;
}

int ai_screen_get_text_grid(char *out_buf, size_t out_len, int marked_cursor) {
  if (!out_buf || out_len == 0) return 0;
  if (!screen_initialized) ai_screen_init();

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  out_buf[0] = '\0';
  size_t offset = 0;

  for (int r = 0; r < M8_SCREEN_ROWS; r++) {
    char line_buf[M8_SCREEN_COLS + 16];
    const char *orig = g_screen_state.text[r];

    if (marked_cursor && r == g_screen_state.cursor_row &&
        g_screen_state.cursor_col >= 0 && g_screen_state.cursor_col < M8_SCREEN_COLS) {
      int col = g_screen_state.cursor_col;
      int w = g_screen_state.cursor_width > 0 ? g_screen_state.cursor_width : 1;

      char pre[M8_SCREEN_COLS + 1] = {0};
      char mid[M8_SCREEN_COLS + 1] = {0};
      char post[M8_SCREEN_COLS + 1] = {0};

      int pre_len = (col < M8_SCREEN_COLS) ? col : M8_SCREEN_COLS;
      memcpy(pre, orig, pre_len);
      pre[pre_len] = '\0';

      int mid_len = (col + w <= M8_SCREEN_COLS) ? w : (M8_SCREEN_COLS - col);
      if (mid_len < 1) mid_len = 1;
      memcpy(mid, orig + col, mid_len);
      mid[mid_len] = '\0';

      if (col + mid_len < M8_SCREEN_COLS) {
        snprintf(post, sizeof(post), "%s", orig + col + mid_len);
      }

      snprintf(line_buf, sizeof(line_buf), "%s[%s]%s", pre, mid, post);
    } else {
      snprintf(line_buf, sizeof(line_buf), "%s", orig);
    }

    size_t llen = strlen(line_buf);
    if (offset + llen + 2 >= out_len) break;
    memcpy(out_buf + offset, line_buf, llen);
    offset += llen;
    out_buf[offset++] = '\n';
    out_buf[offset] = '\0';
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
  return (int)offset;
}

void ai_screen_get_cursor(int *out_col, int *out_row, char *out_input, size_t input_len) {
  if (!screen_initialized) ai_screen_init();

  if (screen_mutex) SDL_LockMutex(screen_mutex);
  analyze_screen_state();

  if (out_col) *out_col = g_screen_state.cursor_col;
  if (out_row) *out_row = g_screen_state.cursor_row;
  if (out_input && input_len > 0) {
    snprintf(out_input, input_len, "%s", g_screen_state.active_input);
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}
