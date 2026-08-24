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

// Internal state to aggregate cursor corner brackets (TL, TR, BL, BR)
static int g_cur_min_x = -1;
static int g_cur_min_y = -1;
static int g_cur_max_x = -1;
static int g_cur_max_y = -1;
static int g_cur_corner_count = 0;
static Uint64 g_last_corner_tick = 0;

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
  g_cur_min_x = -1;
  g_cur_min_y = -1;
  g_cur_max_x = -1;
  g_cur_max_y = -1;
  g_cur_corner_count = 0;
  g_last_corner_tick = 0;

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
  Uint64 now_char = SDL_GetTicks();
  if ((bg_r + bg_g + bg_b >= 300) && px_x < 240 && (now_char - g_last_corner_tick >= 100) && col > 1) {
    if (row >= 0 && row < M8_SCREEN_ROWS && col >= 0 && col < M8_SCREEN_COLS) {
      g_screen_state.cursor_col = col;
      g_screen_state.cursor_row = row;
      g_screen_state.cursor_width = 1;
    }
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}

void ai_screen_on_draw_rect(int px_x, int px_y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  if (!screen_initialized) ai_screen_init();

  if (screen_mutex) SDL_LockMutex(screen_mutex);

  if (px_x == 0 && px_y == 0 && w >= 320 && h >= 240) {
    // Screen clear / background fill
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
    g_cur_corner_count = 0;
  } else if (w > 0 && h > 0 && px_x < 320 && px_y < 240) {
    Uint64 now = SDL_GetTicks();

    // 1. Check for small corner brackets of the M8 cursor (e.g. w <= 4, h <= 4)
    if (w <= 4 && h <= 4) {
      // If close to active corner cluster, under 4 corners, and within 20ms, aggregate
      if (g_cur_min_x >= 0 && (now - g_last_corner_tick < 20) && (g_cur_corner_count < 4) &&
          abs(px_x - g_cur_min_x) <= 180 && abs(px_y - g_cur_min_y) <= 18) {
        if (px_x < g_cur_min_x) g_cur_min_x = px_x;
        if (px_y < g_cur_min_y) g_cur_min_y = px_y;
        if (px_x + w > g_cur_max_x) g_cur_max_x = px_x + w;
        if (px_y + h > g_cur_max_y) g_cur_max_y = px_y + h;
        g_cur_corner_count++;
      } else {
        // Start a new corner cluster
        g_cur_min_x = px_x;
        g_cur_min_y = px_y;
        g_cur_max_x = px_x + w;
        g_cur_max_y = px_y + h;
        g_cur_corner_count = 1;
      }
      g_last_corner_tick = now;

      // An authentic M8 cursor bracket has multiple corner segments and spans at least 6x6 pixels
      if (g_cur_corner_count >= 3 && (g_cur_max_x - g_cur_min_x) >= 6 && (g_cur_max_y - g_cur_min_y) >= 6) {
        int col = (g_cur_min_x + 1) / 8;
        int row = (g_cur_min_y + 1) / 8;

        // Correct for M8 16-pixel hardware bar gap rows (9, 14, 19, 24) on tracker screens
        if (strcmp(g_screen_state.active_screen, "SONG") == 0 ||
            strcmp(g_screen_state.active_screen, "CHAIN") == 0 ||
            strcmp(g_screen_state.active_screen, "PHRASE") == 0 ||
            strcmp(g_screen_state.active_screen, "TABLE") == 0 ||
            strcmp(g_screen_state.active_screen, "GROOVE") == 0) {
          if (row == 9) row = 8;
          else if (row == 14) row = 13;
          else if (row == 19) row = 18;
          else if (row == 24) row = 23;
        }

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
    }
    // 2. Check for solid block selection highlights (e.g. in menus / project view / file browser)
    else if (w >= 6 && h >= 6 && w <= 220 && h <= 18) {
      // If a corner bracket cursor was drawn in this frame, step markers at col <= 1 must not override it
      int col = px_x / 8;
      int row = px_y / 8;
      int cols_wide = (w + 7) / 8;
      if (cols_wide < 1) cols_wide = 1;
      if (cols_wide > 20) cols_wide = 20;

      if ((now - g_last_corner_tick >= 100) || (col > 1) ||
          strcmp(g_screen_state.active_screen, "FILE_BROWSER") == 0) {
        if (col >= 0 && col < M8_SCREEN_COLS && row >= 0 && row < M8_SCREEN_ROWS) {
          g_screen_state.cursor_col = col;
          g_screen_state.cursor_row = row;
          g_screen_state.cursor_width = cols_wide;
          g_screen_state.cursor_height = 1;
        }
      }
    }
    // 3. Clear text cells on background color fills (w >= 8 && h >= 6)
    else if ((r == 0 && g == 0 && b == 0) ||
             (r == g_screen_state.bg_theme_r && g == g_screen_state.bg_theme_g && b == g_screen_state.bg_theme_b)) {
      if (w >= 8 && h >= 6 && px_x < 320 && px_y < 240) {
        int start_c = px_x / 8;
        int end_c = (px_x + w + 7) / 8;
        int start_r = px_y / 8;
        int end_r = (px_y + h + 7) / 8;
        if (start_c < 0) start_c = 0;
        if (end_c > M8_SCREEN_COLS) end_c = M8_SCREEN_COLS;
        if (start_r < 0) start_r = 0;
        if (end_r > M8_SCREEN_ROWS) end_r = M8_SCREEN_ROWS;
        for (int r_idx = start_r; r_idx < end_r; r_idx++) {
          for (int c_idx = start_c; c_idx < end_c; c_idx++) {
            g_screen_state.text[r_idx][c_idx] = ' ';
          }
        }
      }
    }
  }

  if (screen_mutex) SDL_UnlockMutex(screen_mutex);
}

static Uint64 g_last_waveform_tick = 0;

void ai_screen_on_waveform(const uint8_t *data, uint16_t size) {
  (void)data;
  (void)size;
  g_last_waveform_tick = SDL_GetTicks();
}

static void str_trim(char *str) {
  char *start = str;
  while (isspace((unsigned char)*start)) start++;
  if (start != str) memmove(str, start, strlen(start) + 1);
  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) str[--len] = '\0';
}

static void extract_token_at(const char *line, int col, int width, char *out_buf, size_t out_len) {
  out_buf[0] = '\0';
  if (!line || col < 0 || col >= (int)strlen(line)) return;

  int len = (int)strlen(line);

  // If width is specified from snap_cursor_to_token, copy that exact range
  if (width > 1 && col + width <= len + 1) {
    int copy_len = width;
    if (col + copy_len > len) copy_len = len - col;
    if (copy_len > (int)out_len - 1) copy_len = (int)out_len - 1;
    memcpy(out_buf, line + col, copy_len);
    out_buf[copy_len] = '\0';
    str_trim(out_buf);
    return;
  }

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

static void str_clean_punct(char *str) {
  if (!str) return;
  size_t len = strlen(str);
  while (len > 0 && (str[len - 1] == ':' || str[len - 1] == '.' || str[len - 1] == ',' ||
                     str[len - 1] == '-' || str[len - 1] == '>' || str[len - 1] == '<' ||
                     str[len - 1] == '?' || str[len - 1] == '!' || str[len - 1] == '=')) {
    str[--len] = '\0';
  }
}

static void extract_left_label(const char *line, int col, char *out_buf, size_t out_len) {
  out_buf[0] = '\0';
  if (!line || col <= 0) return;

  // Scan backwards past spaces
  int p = col - 1;
  while (p >= 0 && line[p] == ' ') p--;
  if (p < 0) return;

  // Read the immediate word to the left
  int word1_end = p + 1;
  while (p > 0 && line[p - 1] != ' ') p--;
  int word1_start = p;

  // Check if there is another preceding word that forms a compound label
  // e.g. "LOOP ST", "INST TYPE", "PLAY MODE", "FINE TUNE", "CUT OFF", "MOD RATE", "MOD DEST"
  int prev_p = word1_start - 1;
  while (prev_p >= 0 && line[prev_p] == ' ') prev_p--;
  if (prev_p >= 0) {
    int word0_end = prev_p + 1;
    while (prev_p > 0 && line[prev_p - 1] != ' ') prev_p--;
    int word0_start = prev_p;

    char word0[32] = {0};
    int len0 = word0_end - word0_start;
    if (len0 > 0 && len0 < (int)sizeof(word0)) {
      memcpy(word0, line + word0_start, len0);
      word0[len0] = '\0';
      str_trim(word0);
      str_clean_punct(word0);
      for (size_t i = 0; i < strlen(word0); i++) word0[i] = (char)toupper((unsigned char)word0[i]);

      if (strcmp(word0, "LOOP") == 0 || strcmp(word0, "INST") == 0 || strcmp(word0, "PLAY") == 0 ||
          strcmp(word0, "FINE") == 0 || strcmp(word0, "CUT") == 0 || strcmp(word0, "MOD") == 0 ||
          strcmp(word0, "LIVE") == 0 || strcmp(word0, "MIDI") == 0 || strcmp(word0, "EXP") == 0) {
        word1_start = word0_start;
      }
    }
  }

  int label_len = word1_end - word1_start;
  if (label_len > (int)out_len - 1) label_len = (int)out_len - 1;
  memcpy(out_buf, line + word1_start, label_len);
  out_buf[label_len] = '\0';
  str_trim(out_buf);
  str_clean_punct(out_buf);
}

static void standardize_parameter_label(char *label, size_t label_len) {
  if (!label || strlen(label) == 0) return;
  str_trim(label);
  str_clean_punct(label);

  size_t len = strlen(label);
  for (size_t i = 0; i < len; i++) {
    label[i] = (char)toupper((unsigned char)label[i]);
  }

  if (strcmp(label, "SMP") == 0 || strcmp(label, "SAMPLE") == 0) snprintf(label, label_len, "SAMPLE");
  else if (strcmp(label, "SLC") == 0 || strcmp(label, "SLICE") == 0) snprintf(label, label_len, "SLICE");
  else if (strcmp(label, "PLY") == 0 || strcmp(label, "PLAY") == 0 || strcmp(label, "PLAY MODE") == 0) snprintf(label, label_len, "PLAY_MODE");
  else if (strcmp(label, "STA") == 0 || strcmp(label, "START") == 0) snprintf(label, label_len, "START");
  else if (strcmp(label, "LOOP ST") == 0 || strcmp(label, "LOOPST") == 0 || strcmp(label, "LOOP") == 0 || strcmp(label, "ST") == 0) snprintf(label, label_len, "LOOP_START");
  else if (strcmp(label, "LEN") == 0 || strcmp(label, "LENGTH") == 0) snprintf(label, label_len, "LENGTH");
  else if (strcmp(label, "DEG") == 0 || strcmp(label, "DEGRADE") == 0) snprintf(label, label_len, "DEGRADE");
  else if (strcmp(label, "FIL") == 0 || strcmp(label, "FILTER") == 0) snprintf(label, label_len, "FILTER_TYPE");
  else if (strcmp(label, "CUT") == 0 || strcmp(label, "CUTOFF") == 0 || strcmp(label, "CUT OFF") == 0) snprintf(label, label_len, "CUTOFF");
  else if (strcmp(label, "RES") == 0 || strcmp(label, "RESO") == 0 || strcmp(label, "RESONANCE") == 0) snprintf(label, label_len, "RESONANCE");
  else if (strcmp(label, "VOL") == 0 || strcmp(label, "VOLUME") == 0) snprintf(label, label_len, "VOLUME");
  else if (strcmp(label, "PAN") == 0) snprintf(label, label_len, "PAN");
  else if (strcmp(label, "DRY") == 0) snprintf(label, label_len, "DRY");
  else if (strcmp(label, "CHO") == 0 || strcmp(label, "CHORUS") == 0) snprintf(label, label_len, "CHORUS");
  else if (strcmp(label, "DEL") == 0 || strcmp(label, "DELAY") == 0) snprintf(label, label_len, "DELAY");
  else if (strcmp(label, "REV") == 0 || strcmp(label, "REVERB") == 0) snprintf(label, label_len, "REVERB");
  else if (strcmp(label, "PIT") == 0 || strcmp(label, "PITCH") == 0) snprintf(label, label_len, "PITCH");
  else if (strcmp(label, "FINE") == 0 || strcmp(label, "FINETUNE") == 0 || strcmp(label, "FINE TUNE") == 0) snprintf(label, label_len, "FINETUNE");
  else if (strcmp(label, "TRANS") == 0 || strcmp(label, "TRANSP") == 0 || strcmp(label, "TRANSPOSE") == 0) snprintf(label, label_len, "TRANSPOSE");
  else if (strcmp(label, "TBL") == 0 || strcmp(label, "TABLE") == 0) snprintf(label, label_len, "TABLE");
  else if (strcmp(label, "INST TYPE") == 0 || strcmp(label, "TYPE") == 0) snprintf(label, label_len, "INST_TYPE");
  else if (strcmp(label, "NAME") == 0 || strcmp(label, "INST NAME") == 0) snprintf(label, label_len, "NAME");
  else if (strcmp(label, "AMP") == 0) snprintf(label, label_len, "AMP");
  else if (strcmp(label, "LIM") == 0 || strcmp(label, "LIMIT") == 0) snprintf(label, label_len, "LIMIT");
  else if (strcmp(label, "ALGO") == 0 || strcmp(label, "ALGORITHM") == 0) snprintf(label, label_len, "ALGO");

  // FMSYNTH Operators 1..4
  else if (strcmp(label, "OP1") == 0 || strcmp(label, "OP1 RAT") == 0 || strcmp(label, "OP1 RATIO") == 0 || strcmp(label, "OP1_RAT") == 0 || strcmp(label, "OP1_RATIO") == 0) snprintf(label, label_len, "OP1_RATIO");
  else if (strcmp(label, "OP1 LEV") == 0 || strcmp(label, "OP1 LEVEL") == 0 || strcmp(label, "OP1_LEV") == 0 || strcmp(label, "OP1_LEVEL") == 0) snprintf(label, label_len, "OP1_LEVEL");
  else if (strcmp(label, "OP1 FB") == 0 || strcmp(label, "OP1 FEEDBACK") == 0 || strcmp(label, "OP1_FB") == 0 || strcmp(label, "OP1_FEEDBACK") == 0) snprintf(label, label_len, "OP1_FEEDBACK");
  else if (strcmp(label, "OP1 MOD") == 0 || strcmp(label, "OP1_MOD") == 0) snprintf(label, label_len, "OP1_MOD");
  else if (strcmp(label, "OP1 SHP") == 0 || strcmp(label, "OP1 SHAPE") == 0 || strcmp(label, "OP1_SHP") == 0 || strcmp(label, "OP1_SHAPE") == 0) snprintf(label, label_len, "OP1_SHAPE");
  else if (strcmp(label, "OP1 DET") == 0 || strcmp(label, "OP1 DETUNE") == 0 || strcmp(label, "OP1_DET") == 0 || strcmp(label, "OP1_DETUNE") == 0) snprintf(label, label_len, "OP1_DETUNE");
  else if (strcmp(label, "OP1 WAVE") == 0 || strcmp(label, "OP1_WAVE") == 0) snprintf(label, label_len, "OP1_WAVE");

  else if (strcmp(label, "OP2") == 0 || strcmp(label, "OP2 RAT") == 0 || strcmp(label, "OP2 RATIO") == 0 || strcmp(label, "OP2_RAT") == 0 || strcmp(label, "OP2_RATIO") == 0) snprintf(label, label_len, "OP2_RATIO");
  else if (strcmp(label, "OP2 LEV") == 0 || strcmp(label, "OP2 LEVEL") == 0 || strcmp(label, "OP2_LEV") == 0 || strcmp(label, "OP2_LEVEL") == 0) snprintf(label, label_len, "OP2_LEVEL");
  else if (strcmp(label, "OP2 FB") == 0 || strcmp(label, "OP2 FEEDBACK") == 0 || strcmp(label, "OP2_FB") == 0 || strcmp(label, "OP2_FEEDBACK") == 0) snprintf(label, label_len, "OP2_FEEDBACK");
  else if (strcmp(label, "OP2 MOD") == 0 || strcmp(label, "OP2_MOD") == 0) snprintf(label, label_len, "OP2_MOD");
  else if (strcmp(label, "OP2 SHP") == 0 || strcmp(label, "OP2 SHAPE") == 0 || strcmp(label, "OP2_SHP") == 0 || strcmp(label, "OP2_SHAPE") == 0) snprintf(label, label_len, "OP2_SHAPE");
  else if (strcmp(label, "OP2 DET") == 0 || strcmp(label, "OP2 DETUNE") == 0 || strcmp(label, "OP2_DET") == 0 || strcmp(label, "OP2_DETUNE") == 0) snprintf(label, label_len, "OP2_DETUNE");
  else if (strcmp(label, "OP2 WAVE") == 0 || strcmp(label, "OP2_WAVE") == 0) snprintf(label, label_len, "OP2_WAVE");

  else if (strcmp(label, "OP3") == 0 || strcmp(label, "OP3 RAT") == 0 || strcmp(label, "OP3 RATIO") == 0 || strcmp(label, "OP3_RAT") == 0 || strcmp(label, "OP3_RATIO") == 0) snprintf(label, label_len, "OP3_RATIO");
  else if (strcmp(label, "OP3 LEV") == 0 || strcmp(label, "OP3 LEVEL") == 0 || strcmp(label, "OP3_LEV") == 0 || strcmp(label, "OP3_LEVEL") == 0) snprintf(label, label_len, "OP3_LEVEL");
  else if (strcmp(label, "OP3 FB") == 0 || strcmp(label, "OP3 FEEDBACK") == 0 || strcmp(label, "OP3_FB") == 0 || strcmp(label, "OP3_FEEDBACK") == 0) snprintf(label, label_len, "OP3_FEEDBACK");
  else if (strcmp(label, "OP3 MOD") == 0 || strcmp(label, "OP3_MOD") == 0) snprintf(label, label_len, "OP3_MOD");
  else if (strcmp(label, "OP3 SHP") == 0 || strcmp(label, "OP3 SHAPE") == 0 || strcmp(label, "OP3_SHP") == 0 || strcmp(label, "OP3_SHAPE") == 0) snprintf(label, label_len, "OP3_SHAPE");
  else if (strcmp(label, "OP3 DET") == 0 || strcmp(label, "OP3 DETUNE") == 0 || strcmp(label, "OP3_DET") == 0 || strcmp(label, "OP3_DETUNE") == 0) snprintf(label, label_len, "OP3_DETUNE");
  else if (strcmp(label, "OP3 WAVE") == 0 || strcmp(label, "OP3_WAVE") == 0) snprintf(label, label_len, "OP3_WAVE");

  else if (strcmp(label, "OP4") == 0 || strcmp(label, "OP4 RAT") == 0 || strcmp(label, "OP4 RATIO") == 0 || strcmp(label, "OP4_RAT") == 0 || strcmp(label, "OP4_RATIO") == 0) snprintf(label, label_len, "OP4_RATIO");
  else if (strcmp(label, "OP4 LEV") == 0 || strcmp(label, "OP4 LEVEL") == 0 || strcmp(label, "OP4_LEV") == 0 || strcmp(label, "OP4_LEVEL") == 0) snprintf(label, label_len, "OP4_LEVEL");
  else if (strcmp(label, "OP4 FB") == 0 || strcmp(label, "OP4 FEEDBACK") == 0 || strcmp(label, "OP4_FB") == 0 || strcmp(label, "OP4_FEEDBACK") == 0) snprintf(label, label_len, "OP4_FEEDBACK");
  else if (strcmp(label, "OP4 MOD") == 0 || strcmp(label, "OP4_MOD") == 0) snprintf(label, label_len, "OP4_MOD");
  else if (strcmp(label, "OP4 SHP") == 0 || strcmp(label, "OP4 SHAPE") == 0 || strcmp(label, "OP4_SHP") == 0 || strcmp(label, "OP4_SHAPE") == 0) snprintf(label, label_len, "OP4_SHAPE");
  else if (strcmp(label, "OP4 DET") == 0 || strcmp(label, "OP4 DETUNE") == 0 || strcmp(label, "OP4_DET") == 0 || strcmp(label, "OP4_DETUNE") == 0) snprintf(label, label_len, "OP4_DETUNE");
  else if (strcmp(label, "OP4 WAVE") == 0 || strcmp(label, "OP4_WAVE") == 0) snprintf(label, label_len, "OP4_WAVE");

  else if (strcmp(label, "RAT") == 0 || strcmp(label, "RATIO") == 0) snprintf(label, label_len, "RATIO");
  else if (strcmp(label, "LEV") == 0 || strcmp(label, "LEVEL") == 0) snprintf(label, label_len, "LEVEL");
  else if (strcmp(label, "FB") == 0 || strcmp(label, "FEEDBACK") == 0) snprintf(label, label_len, "FEEDBACK");
  else if (strcmp(label, "DET") == 0 || strcmp(label, "DETUNE") == 0) snprintf(label, label_len, "DETUNE");
  else if (strcmp(label, "WAVE") == 0) snprintf(label, label_len, "WAVE");
}

static int is_known_param_label(const char *tok) {
  if (!tok || strlen(tok) == 0) return 0;
  char clean[32];
  snprintf(clean, sizeof(clean), "%s", tok);
  str_clean_punct(clean);
  str_trim(clean);
  for (size_t i = 0; i < strlen(clean); i++) clean[i] = (char)toupper((unsigned char)clean[i]);

  const char *known_labels[] = {
      "TYPE", "NAME", "TRANSP", "TRANS", "TABLE", "TBL",
      "SAMPLE", "SMP", "SLICE", "SLC", "PLAY", "PLY",
      "START", "STA", "LOOP", "ST", "LENGTH", "LEN",
      "DEGRADE", "DEG", "FILTER", "FIL", "CUTOFF", "CUT",
      "RES", "RESO", "RESONANCE", "AMP", "LIMIT", "LIM",
      "VOLUME", "VOL", "PAN", "DRY", "CHORUS", "CHO",
      "DELAY", "DEL", "REVERB", "REV", "PITCH", "PIT",
      "FINE", "FINETUNE", "RATE", "DEST", "AMT", "SHAPE",
      "SHP", "TRIG", "TRG", "ATTACK", "ATK", "HOLD", "HLD",
      "DECAY", "DEC", "SUSTAIN", "SUS", "RELEASE", "REL",
      "SOURCE", "SRC", "LOW", "HIGH", "OSC", "SIZE",
      "MULT", "WARP", "MIRROR", "SCAN", "TIMBRE", "COLOR",
      "REDUCE", "ALGO", "ALGORITHM", "RATIO", "RAT", "LEVS",
      "LEV", "LEVEL", "FB", "FEEDBACK", "MOD", "DET", "DETUNE",
      "OP1", "OP2", "OP3", "OP4", "WAVE", "CARRIER", "MODULATOR",
      "PORT", "CHANNEL", "CHN", "BANK", "PROGRAM", "PGM",
      "CC1", "CC2", "CC3", "CC4", "TEMPO", "GROOVE", "SCALE",
      "EQ", "CHO_RETURN", "DEL_RETURN", "REV_RETURN", "OUTPUT_VOL",
      "INPUT_VOL", "INPUT_PAN", "INPUT_LIMIT", "MIX_DC", "INPUT_SOURCE",
      "LIMITER", "INPUT_CHORUS", "USB_CHORUS", "DJ_FILTER", "INPUT_DELAY",
      "USB_DELAY", "OTT", "INPUT_REVERB", "USB_REVERB", "GAIN", "FREQ", "MODE"
  };
  int count = (int)(sizeof(known_labels) / sizeof(known_labels[0]));
  for (int i = 0; i < count; i++) {
    if (strcmp(clean, known_labels[i]) == 0) return 1;
  }
  return 0;
}

typedef struct {
  int start_col;
  int end_col;
  char text[32];
  int is_label;
} row_token_t;

static int resolve_multi_column_row(const char *line, int cursor_col,
                                   char *out_input, size_t input_len,
                                   char *out_value, size_t value_len) {
  if (!line || cursor_col < 0) return 0;

  row_token_t tokens[16];
  int token_count = 0;
  int len = (int)strlen(line);
  int i = 0;

  while (i < len && token_count < 16) {
    while (i < len && line[i] == ' ') i++;
    if (i >= len) break;

    int start = i;
    while (i < len && line[i] != ' ') i++;
    int end = i;

    int tlen = end - start;
    if (tlen > 31) tlen = 31;
    tokens[token_count].start_col = start;
    tokens[token_count].end_col = end;
    memcpy(tokens[token_count].text, line + start, tlen);
    tokens[token_count].text[tlen] = '\0';
    str_trim(tokens[token_count].text);

    // Check if token ends with ':' or '.' (e.g. "TRANSP.") or is a known label
    int has_colon = 0;
    int tlen_text = (int)strlen(tokens[token_count].text);
    if (tlen_text > 0 && (tokens[token_count].text[tlen_text - 1] == ':' ||
                          (tokens[token_count].text[tlen_text - 1] == '.' &&
                           !strstr(tokens[token_count].text, ".WAV") && !strstr(tokens[token_count].text, ".wav") &&
                           !strstr(tokens[token_count].text, ".AIF") && !strstr(tokens[token_count].text, ".aif") &&
                           !strstr(tokens[token_count].text, ".M8S") && !strstr(tokens[token_count].text, ".m8s") &&
                           !strstr(tokens[token_count].text, ".M8I") && !strstr(tokens[token_count].text, ".m8i")))) {
      has_colon = 1;
    }
    tokens[token_count].is_label = has_colon || is_known_param_label(tokens[token_count].text);

    token_count++;
  }

  if (token_count == 0) return 0;

  // Merge compound labels like "LOOP" + "ST", "INST" + "TYPE", "PLAY" + "MODE", "FINE" + "TUNE", "CUT" + "OFF"
  for (int t = 0; t < token_count - 1; t++) {
    char clean0[32], clean1[32];
    snprintf(clean0, sizeof(clean0), "%s", tokens[t].text);
    snprintf(clean1, sizeof(clean1), "%s", tokens[t + 1].text);
    str_clean_punct(clean0); str_clean_punct(clean1);
    for (size_t c = 0; c < strlen(clean0); c++) clean0[c] = (char)toupper((unsigned char)clean0[c]);
    for (size_t c = 0; c < strlen(clean1); c++) clean1[c] = (char)toupper((unsigned char)clean1[c]);

    if ((strcmp(clean0, "LOOP") == 0 && strcmp(clean1, "ST") == 0) ||
        (strcmp(clean0, "INST") == 0 && strcmp(clean1, "TYPE") == 0) ||
        (strcmp(clean0, "PLAY") == 0 && strcmp(clean1, "MODE") == 0) ||
        (strcmp(clean0, "FINE") == 0 && strcmp(clean1, "TUNE") == 0) ||
        (strcmp(clean0, "CUT") == 0 && strcmp(clean1, "OFF") == 0) ||
        ((strncmp(clean0, "OP", 2) == 0 && isdigit((unsigned char)clean0[2])) &&
         (strcmp(clean1, "RAT") == 0 || strcmp(clean1, "RATIO") == 0 ||
          strcmp(clean1, "LEV") == 0 || strcmp(clean1, "LEVEL") == 0 ||
          strcmp(clean1, "FB") == 0 || strcmp(clean1, "FEEDBACK") == 0 ||
          strcmp(clean1, "MOD") == 0 || strcmp(clean1, "DET") == 0 ||
          strcmp(clean1, "DETUNE") == 0 || strcmp(clean1, "SHP") == 0 ||
          strcmp(clean1, "SHAPE") == 0 || strcmp(clean1, "WAVE") == 0)) ||
        (strcmp(clean0, "MOD") == 0 && (strcmp(clean1, "RATE") == 0 || strcmp(clean1, "DEST") == 0 || strcmp(clean1, "AMT") == 0))) {
      snprintf(tokens[t].text, sizeof(tokens[t].text), "%s %s", clean0, clean1);
      tokens[t].end_col = tokens[t + 1].end_col;
      tokens[t].is_label = 1;
      // Shift remaining tokens left
      for (int k = t + 1; k < token_count - 1; k++) {
        tokens[k] = tokens[k + 1];
      }
      token_count--;
    }
  }

  // Detect if row has an operator prefix (e.g. OP1, OP2, OP3, OP4)
  char op_prefix[8] = {0};
  if (token_count > 0) {
    char first_clean[32];
    snprintf(first_clean, sizeof(first_clean), "%s", tokens[0].text);
    str_clean_punct(first_clean);
    str_trim(first_clean);
    for (size_t c = 0; c < strlen(first_clean); c++) first_clean[c] = (char)toupper((unsigned char)first_clean[c]);
    if (strncmp(first_clean, "OP", 2) == 0 && isdigit((unsigned char)first_clean[2])) {
      snprintf(op_prefix, sizeof(op_prefix), "OP%c", first_clean[2]);
    }
  }

  // Find token at or closest to cursor_col
  int cur_idx = -1;
  for (int t = 0; t < token_count; t++) {
    if (cursor_col >= tokens[t].start_col && cursor_col <= tokens[t].end_col) {
      cur_idx = t;
      break;
    }
  }
  if (cur_idx == -1) {
    // Pick nearest token
    int min_dist = 999;
    for (int t = 0; t < token_count; t++) {
      int dist = abs(cursor_col - tokens[t].start_col);
      if (dist < min_dist) {
        min_dist = dist;
        cur_idx = t;
      }
    }
  }

  if (cur_idx == -1) return 0;

  char resolved_label[64] = {0};
  char resolved_value[64] = {0};

  if (tokens[cur_idx].is_label) {
    // Cursor is directly on the LABEL
    snprintf(resolved_label, sizeof(resolved_label), "%s", tokens[cur_idx].text);
    // Corresponding value is the next token if it is a value
    if (cur_idx + 1 < token_count && !tokens[cur_idx + 1].is_label) {
      snprintf(resolved_value, sizeof(resolved_value), "%s", tokens[cur_idx + 1].text);
    } else if (cur_idx + 1 < token_count) {
      snprintf(resolved_value, sizeof(resolved_value), "%s", tokens[cur_idx + 1].text);
    } else {
      snprintf(resolved_value, sizeof(resolved_value), "%s", tokens[cur_idx].text);
    }
  } else {
    // Cursor is on a VALUE
    snprintf(resolved_value, sizeof(resolved_value), "%s", tokens[cur_idx].text);
    // Find closest preceding label
    int label_idx = -1;
    for (int t = cur_idx - 1; t >= 0; t--) {
      if (tokens[t].is_label) {
        label_idx = t;
        break;
      }
    }
    if (label_idx != -1) {
      snprintf(resolved_label, sizeof(resolved_label), "%s", tokens[label_idx].text);
    } else {
      snprintf(resolved_label, sizeof(resolved_label), "%s", tokens[cur_idx].text);
    }
  }

  // If row has an operator prefix and resolved_label is a sub-parameter, prefix it
  if (strlen(op_prefix) > 0 && strncmp(resolved_label, "OP", 2) != 0) {
    char clean_sub[32];
    snprintf(clean_sub, sizeof(clean_sub), "%s", resolved_label);
    str_clean_punct(clean_sub);
    str_trim(clean_sub);
    for (size_t c = 0; c < strlen(clean_sub); c++) clean_sub[c] = (char)toupper((unsigned char)clean_sub[c]);

    if (strcmp(clean_sub, "LEV") == 0 || strcmp(clean_sub, "LEVEL") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_LEVEL", op_prefix);
    } else if (strcmp(clean_sub, "FB") == 0 || strcmp(clean_sub, "FEEDBACK") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_FEEDBACK", op_prefix);
    } else if (strcmp(clean_sub, "MOD") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_MOD", op_prefix);
    } else if (strcmp(clean_sub, "DET") == 0 || strcmp(clean_sub, "DETUNE") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_DETUNE", op_prefix);
    } else if (strcmp(clean_sub, "SHP") == 0 || strcmp(clean_sub, "SHAPE") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_SHAPE", op_prefix);
    } else if (strcmp(clean_sub, "RAT") == 0 || strcmp(clean_sub, "RATIO") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_RATIO", op_prefix);
    } else if (strcmp(clean_sub, "WAVE") == 0) {
      snprintf(resolved_label, sizeof(resolved_label), "%s_WAVE", op_prefix);
    }
  }

  standardize_parameter_label(resolved_label, sizeof(resolved_label));
  str_clean_punct(resolved_value);
  str_trim(resolved_value);

  if (strlen(resolved_label) > 0) {
    snprintf(out_input, input_len, "%s", resolved_label);
    if (strlen(resolved_value) > 0) {
      snprintf(out_value, value_len, "%s", resolved_value);
    }
    return 1;
  }
  return 0;
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
    {"PROJECT", 5, 6, 12, 24, "TEMPO"},
    {"PROJECT", 7, 7, 12, 24, "TRANSPOSE"},
    {"PROJECT", 8, 8, 12, 26, "GROOVE"},
    {"PROJECT", 10, 10, 12, 28, "SCALE"},
    {"PROJECT", 11, 11, 12, 28, "LIVE_QUANTIZ"},
    {"PROJECT", 13, 13, 12, 21, "MIDI_SETTINGS"},
    {"PROJECT", 13, 13, 22, 32, "MIDI_MAPPINGS"},
    {"PROJECT", 16, 16, 12, 26, "NAME"},
    {"PROJECT", 17, 17, 12, 17, "PROJECT_LOAD"},
    {"PROJECT", 17, 17, 18, 22, "PROJECT_SAVE"},
    {"PROJECT", 17, 17, 23, 28, "PROJECT_NEW"},
    {"PROJECT", 18, 18, 12, 19, "EXPORT_RENDER"},
    {"PROJECT", 18, 18, 20, 28, "EXPORT_BUNDLE"},
    {"PROJECT", 20, 20, 12, 20, "CLEAR_PHRASES"},
    {"PROJECT", 20, 20, 21, 30, "CLEAR_INST_TBL"},
    {"PROJECT", 21, 21, 12, 30, "INST_POOL"},
    {"PROJECT", 23, 23, 12, 30, "TIME_STATS"},
    {"PROJECT", 25, 25, 12, 30, "SYSTEM_SETTINGS"},

    // MIXER Screen
    {"MIXER", 5, 6, 12, 17, "OUTPUT_VOL"},
    // Track Volume Faders (Row 15)
    {"MIXER", 14, 15, 0, 2, "TRACK1_VOL"},
    {"MIXER", 14, 15, 3, 5, "TRACK2_VOL"},
    {"MIXER", 14, 15, 6, 8, "TRACK3_VOL"},
    {"MIXER", 14, 15, 9, 11, "TRACK4_VOL"},
    {"MIXER", 14, 15, 12, 14, "TRACK5_VOL"},
    {"MIXER", 14, 15, 15, 17, "TRACK6_VOL"},
    {"MIXER", 14, 15, 18, 20, "TRACK7_VOL"},
    {"MIXER", 14, 15, 21, 24, "TRACK8_VOL"},
    // Returns & Inputs & Master FX (Rows 20..26)
    {"MIXER", 20, 20, 26, 31, "EQ"},
    {"MIXER", 21, 21, 0, 2, "CHO_RETURN"},
    {"MIXER", 21, 21, 3, 5, "DEL_RETURN"},
    {"MIXER", 21, 21, 6, 8, "REV_RETURN"},
    {"MIXER", 21, 21, 12, 14, "INPUT_VOL"},
    {"MIXER", 21, 21, 15, 17, "INPUT_PAN"},
    {"MIXER", 21, 21, 18, 21, "INPUT_LIMIT"},
    {"MIXER", 21, 21, 26, 31, "MIX_DC"},
    {"MIXER", 22, 22, 18, 24, "INPUT_SOURCE"},
    {"MIXER", 22, 22, 26, 31, "LIMITER"},
    {"MIXER", 23, 23, 12, 16, "INPUT_CHORUS"},
    {"MIXER", 23, 23, 18, 22, "USB_CHORUS"},
    {"MIXER", 23, 23, 26, 31, "DJ_FILTER"},
    {"MIXER", 25, 25, 12, 16, "INPUT_DELAY"},
    {"MIXER", 25, 25, 18, 22, "USB_DELAY"},
    {"MIXER", 25, 25, 26, 31, "OTT"},
    {"MIXER", 26, 26, 12, 16, "INPUT_REVERB"},
    {"MIXER", 26, 26, 18, 22, "USB_REVERB"},

    // EQUALIZER (EQ) Screen (3 Bands: LOW, MID, HIGH)
    // LOW Band (Cols 6..17)
    {"EQ", 21, 21, 6, 17, "LOW_GAIN"},
    {"EQ", 22, 22, 6, 17, "LOW_FREQ"},
    {"EQ", 23, 23, 6, 17, "LOW_Q"},
    {"EQ", 25, 25, 6, 17, "LOW_TYPE"},
    {"EQ", 26, 26, 6, 17, "LOW_MODE"},
    // MID Band (Cols 18..28)
    {"EQ", 21, 21, 18, 28, "MID_GAIN"},
    {"EQ", 22, 22, 18, 28, "MID_FREQ"},
    {"EQ", 23, 23, 18, 28, "MID_Q"},
    {"EQ", 25, 25, 18, 28, "MID_TYPE"},
    {"EQ", 26, 26, 18, 28, "MID_MODE"},
    // HIGH Band (Cols 29..39)
    {"EQ", 21, 21, 29, 39, "HIGH_GAIN"},
    {"EQ", 22, 22, 29, 39, "HIGH_FREQ"},
    {"EQ", 23, 23, 29, 39, "HIGH_Q"},
    {"EQ", 25, 25, 29, 39, "HIGH_TYPE"},
    {"EQ", 26, 26, 29, 39, "HIGH_MODE"},

    // TABLE Header (Table index e.g. TABLE 00)
    {"TABLE", 0, 2, 5, 12, "TABLE_NUM"},

    // GROOVE Header (Groove index e.g. GROOVE 00)
    {"GROOVE", 0, 2, 5, 12, "GROOVE_NUM"},

    // SCALE Header
    {"SCALE", 0, 0, 5, 10, "SCALE_NUM"},
    {"SCALE", 0, 0, 11, 20, "KEY"},
    {"SCALE", 1, 1, 4, 30, "NAME"},

    // KEYBOARD / Name Picker
    {"KEYBOARD", 2, 6, 0, 39, "NAME_BUFFER"},
    {"KEYBOARD", 8, 22, 0, 39, "KEY_CHAR"},

    // FILE_BROWSER Header
    {"FILE_BROWSER", 0, 1, 0, 39, "CURRENT_PATH"},

    // CONFIRM_DIALOG Action
    {"CONFIRM_DIALOG", 0, 29, 0, 39, "CONFIRM_ACTION"},
};

static void snap_cursor_to_token(int row, int *col, int *width) {
  if (row < 0 || row >= M8_SCREEN_ROWS || !col || !width) return;
  const char *line = g_screen_state.text[row];
  int len = (int)strlen(line);
  int c = *col;
  if (c < 0 || c >= len) return;

  // If pointing at a space, scan adjacent characters to find the intended token on this line
  if (line[c] == ' ') {
    int forward = c;
    while (forward < len && line[forward] == ' ') forward++;
    int backward = c;
    while (backward >= 0 && line[backward] == ' ') backward--;
    if (forward < len) {
      c = forward;
    } else if (backward >= 0) {
      c = backward;
    } else {
      return;
    }
  }

  // Multi-word phrase matches on PROJECT screen
  if (strstr(line, "VIEW INST.POOL") != NULL && c >= 12 && c <= 28) {
    const char *p = strstr(line, "VIEW INST.POOL");
    *col = (int)(p - line);
    *width = 14;
    return;
  }
  if (strstr(line, "VIEW TIME STATS") != NULL && c >= 12 && c <= 28) {
    const char *p = strstr(line, "VIEW TIME STATS");
    *col = (int)(p - line);
    *width = 15;
    return;
  }
  if (strstr(line, "SCALE") != NULL && c >= 12 && c <= 28) {
    const char *p = line + 14;
    while (*p == ' ') p++;
    int start = (int)(p - line);
    int end = start;
    while (end < len && (end < 30) && line[end] != '\0') {
      if (line[end] == ' ' && line[end+1] == ' ' && line[end+2] == ' ') break;
      end++;
    }
    while (end > start && line[end - 1] == ' ') end--;
    if (end > start) {
      *col = start;
      *width = end - start;
      return;
    }
  }
  if (strstr(line, "LIVE QUANTIZ") != NULL && c >= 12 && c <= 28) {
    const char *p = line + 14;
    while (*p == ' ') p++;
    int start = (int)(p - line);
    int end = start;
    while (end < len && (end < 30) && line[end] != '\0') {
      if (line[end] == ' ' && line[end+1] == ' ' && line[end+2] == ' ') break;
      end++;
    }
    while (end > start && line[end - 1] == ' ') end--;
    if (end > start) {
      *col = start;
      *width = end - start;
      return;
    }
  }

  // PHRASE Screen: Note, Vol, Inst, FX1, FX1_Val, FX2, FX2_Val, FX3, FX3_Val
  if (strcmp(g_screen_state.active_screen, "PHRASE") == 0 && row >= 3 && row <= 28) {
    if (c >= 2 && c <= 5) {
      *col = 3;
      *width = 3;
      return;
    } else if (c >= 6 && c <= 8) {
      *col = 7;
      *width = 2;
      return;
    } else if (c >= 9 && c <= 11) {
      *col = 10;
      *width = 2;
      return;
    } else if (c >= 12 && c <= 14) {
      *col = 13;
      *width = 3;
      return;
    } else if (c >= 15 && c <= 17) {
      *col = 16;
      *width = 2;
      return;
    } else if (c >= 18 && c <= 20) {
      *col = 19;
      *width = 3;
      return;
    } else if (c >= 21 && c <= 23) {
      *col = 22;
      *width = 2;
      return;
    } else if (c >= 24 && c <= 26) {
      *col = 25;
      *width = 3;
      return;
    } else if (c >= 27 && c <= 30) {
      *col = 28;
      *width = 2;
      return;
    }
  }

  // TABLE Screen: Note, Vol, FX1, FX1_Val, FX2, FX2_Val, FX3, FX3_Val
  if (strcmp(g_screen_state.active_screen, "TABLE") == 0 && row >= 3 && row <= 28) {
    if (c <= 1) {
      *col = 0;
      *width = 2;
      return;
    } else if (c >= 2 && c <= 5) {
      *col = 3;
      *width = 3;
      return;
    } else if (c >= 6 && c <= 8) {
      *col = 7;
      *width = 2;
      return;
    }

    int is_packed = (c <= 12) || (len > 10 && line[10] != ' ') || (len > 9 && line[9] != ' ');
    if (is_packed && c <= 12) is_packed = 1;
    else if (!is_packed && len > 13 && line[13] != ' ' && len > 10 && line[10] == ' ') is_packed = 0;

    if (is_packed) {
      if (c >= 9 && c <= 12) {
        *col = 10;
        *width = 3;
        return;
      } else if (c >= 13 && c <= 15) {
        *col = 13;
        *width = 2;
        return;
      } else if (c >= 16 && c <= 18) {
        *col = 16;
        *width = 3;
        return;
      } else if (c >= 19 && c <= 21) {
        *col = 19;
        *width = 2;
        return;
      } else if (c >= 22 && c <= 24) {
        *col = 22;
        *width = 3;
        return;
      } else if (c >= 25 && c <= 28) {
        *col = 25;
        *width = 2;
        return;
      }
    } else {
      if (c >= 12 && c <= 14) {
        *col = 13;
        *width = 3;
        return;
      } else if (c >= 15 && c <= 17) {
        *col = 16;
        *width = 2;
        return;
      } else if (c >= 18 && c <= 20) {
        *col = 19;
        *width = 3;
        return;
      } else if (c >= 21 && c <= 23) {
        *col = 22;
        *width = 2;
        return;
      } else if (c >= 24 && c <= 26) {
        *col = 25;
        *width = 3;
        return;
      } else if (c >= 27 && c <= 30) {
        *col = 28;
        *width = 2;
        return;
      }
    }
  }

  // INST_MODS Screen multi-word token snapping (e.g. MOD RATE, MOD AMT, MOD BOTH)
  if (strcmp(g_screen_state.active_screen, "INST_MODS") == 0 && row >= 2 && row <= 28) {
    if (strstr(line, "MOD RATE") != NULL && c >= 6 && c <= 25) {
      const char *p = strstr(line, "MOD RATE");
      *col = (int)(p - line);
      *width = 8;
      return;
    }
    if (strstr(line, "MOD AMT") != NULL && c >= 6 && c <= 25) {
      const char *p = strstr(line, "MOD AMT");
      *col = (int)(p - line);
      *width = 7;
      return;
    }
    if (strstr(line, "MOD BOTH") != NULL && c >= 6 && c <= 25) {
      const char *p = strstr(line, "MOD BOTH");
      *col = (int)(p - line);
      *width = 8;
      return;
    }
  }

  // GROOVE Screen: Step (col 0..1, width 2), Ticks (col 2..8, width 2)
  if (strcmp(g_screen_state.active_screen, "GROOVE") == 0 && row >= 3 && row <= 28) {
    if (c <= 1) {
      *col = 0;
      *width = 2;
      return;
    } else if (c >= 2 && c <= 8) {
      int tc = 3;
      if (tc < len && line[tc] == ' ' && tc + 1 < len && line[tc + 1] != ' ') tc++;
      *col = tc;
      *width = 2;
      return;
    }
  }

  // SCALE Screen: Note (col 0..3), Enable (col 4..7), Offset (col 8..18)
  if (strcmp(g_screen_state.active_screen, "SCALE") == 0 && row >= 3 && row <= 28) {
    if (c <= 3) {
      int sc = 0;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = sc;
      while (ec < len && line[ec] != ' ') ec++;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 2;
      return;
    } else if (c >= 4 && c <= 7) {
      int sc = 4;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = sc;
      while (ec < len && line[ec] != ' ') ec++;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 3;
      return;
    } else if (c >= 8) {
      int sc = 8;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = sc;
      while (ec < len && line[ec] != ' ') ec++;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 6;
      return;
    }
  }

  // INST_POOL Screen: Slot (col 0..3), Type (col 4..12), Name (col 13..39)
  if (strcmp(g_screen_state.active_screen, "INST_POOL") == 0 && row >= 3 && row <= 28) {
    if (c <= 3) {
      int sc = 0;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = sc;
      while (ec < len && line[ec] != ' ') ec++;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 2;
      return;
    } else if (c >= 4 && c <= 12) {
      int sc = 4;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = sc;
      while (ec < len && line[ec] != ' ') ec++;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 6;
      return;
    } else if (c >= 13) {
      int sc = 13;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = len;
      while (ec > sc && line[ec - 1] == ' ') ec--;
      *col = sc;
      *width = (ec > sc) ? (ec - sc) : 4;
      return;
    }
  }

  // FILE_BROWSER Screen (Directory Traversal / File Picker)
  if (strcmp(g_screen_state.active_screen, "FILE_BROWSER") == 0) {
    if (row <= 1) {
      const char *path_pos = strchr(line, '/');
      if (path_pos != NULL) {
        int sc = (int)(path_pos - line);
        int ec = len;
        while (ec > sc && line[ec - 1] == ' ') ec--;
        *col = sc;
        *width = (ec > sc) ? (ec - sc) : 1;
        return;
      }
    }
    int start = c;
    int end = c;
    while (start > 0 && line[start - 1] != ' ') start--;
    while (end < len && line[end] != ' ') end++;
    if (end > start) {
      *col = start;
      *width = end - start;
      return;
    }
  }

  // KEYBOARD / Character Picker Screen
  if (strcmp(g_screen_state.active_screen, "KEYBOARD") == 0) {
    if (row <= 6) {
      // Name Buffer row: snap to the full name token if pointing to a non-empty name
      int sc = 0;
      while (sc < len && line[sc] == ' ') sc++;
      int ec = len;
      while (ec > sc && line[ec - 1] == ' ') ec--;
      if (ec > sc) {
        *col = sc;
        *width = ec - sc;
        return;
      }
    } else {
      // Character grid row: check multi-character buttons first
      const char *multi_btns[] = {"SPACE", "CANCEL", "CLEAR", "DELETE", "DEL", "CLR", "OK", NULL};
      for (int b = 0; multi_btns[b] != NULL; b++) {
        const char *btn = multi_btns[b];
        const char *pos = strstr(line, btn);
        if (pos != NULL) {
          int b_col = (int)(pos - line);
          int b_len = (int)strlen(btn);
          if (c >= b_col && c < b_col + b_len) {
            *col = b_col;
            *width = b_len;
            return;
          }
        }
      }
      // Single character cell in letter grid
      if (line[c] != ' ') {
        *col = c;
        *width = 1;
        return;
      }
    }
  }

  // CONFIRM_DIALOG Screen (Confirmation / Alert Prompts)
  if (strcmp(g_screen_state.active_screen, "CONFIRM_DIALOG") == 0) {
    const char *dialog_btns[] = {"CANCEL", "DELETE", "CLEAR", "CONFIRM", "YES", "NO", "OK", NULL};
    for (int b = 0; dialog_btns[b] != NULL; b++) {
      const char *btn = dialog_btns[b];
      const char *pos = strstr(line, btn);
      if (pos != NULL) {
        int b_col = (int)(pos - line);
        int b_len = (int)strlen(btn);
        if (c >= b_col && c < b_col + b_len) {
          *col = b_col;
          *width = b_len;
          return;
        }
      }
    }
  }

  int start = c;
  int end = c;

  while (start > 0 && line[start - 1] != ' ') start--;
  while (end < len && line[end] != ' ') end++;

  int token_len = end - start;
  if (token_len > 0) {
    *col = start;
    *width = token_len;
  }
}

static void analyze_screen_state(void) {
  // 1. Identify Header text from the first non-empty row (checking rows 0 to 14 for headers or centered dialog prompts)
  char header_raw[M8_SCREEN_COLS + 1] = {0};
  for (int r = 0; r <= 14; r++) {
    char temp[M8_SCREEN_COLS + 1];
    snprintf(temp, sizeof(temp), "%s", g_screen_state.text[r]);
    str_trim(temp);
    if (strlen(temp) > 0) {
      snprintf(header_raw, sizeof(header_raw), "%s", temp);
      break;
    }
  }
  snprintf(g_screen_state.header_text, sizeof(g_screen_state.header_text), "%s", header_raw);

  // Check play state: active waveform stream, play arrow glyphs (> / 0x10 / 0x1A), or PLAY keyword
  Uint64 now = SDL_GetTicks();
  int is_playing = 0;
  if (g_last_waveform_tick > 0 && (now - g_last_waveform_tick < 800)) {
    is_playing = 1;
  }
  if (strstr(header_raw, ">") != NULL || strstr(header_raw, "PLAY") != NULL) {
    is_playing = 1;
  }
  for (int r = 0; r < M8_SCREEN_ROWS && !is_playing; r++) {
    const char *line = g_screen_state.text[r];
    if (strchr(line, '>') != NULL || strstr(line, "T>") != NULL) {
      is_playing = 1;
      break;
    }
  }

  if (is_playing) {
    snprintf(g_screen_state.play_state, sizeof(g_screen_state.play_state), "PLAYING");
  } else {
    snprintf(g_screen_state.play_state, sizeof(g_screen_state.play_state), "STOPPED");
  }

  // Check for Directory Traversal / File Browser entries on rows 1..12
  int has_dir_entry = 0;
  for (int r = 1; r < 12; r++) {
    const char *line = g_screen_state.text[r];
    int p = 0;
    while (line[p] == ' ' && p < 10) p++;
    if (strstr(line, "/..") != NULL || (line[p] == '/' && line[p + 1] != ' ' && line[p + 1] != '\0')) {
      has_dir_entry = 1;
      break;
    }
  }

  // Check for Keyboard / Character Picker Modal
  int is_keyboard = 0;
  if (!has_dir_entry && strstr(header_raw, "DIRECTORY") == NULL && strstr(header_raw, "DIR:") == NULL) {
    for (int r = 4; r < 25; r++) {
      if (strstr(g_screen_state.text[r], "SPACE") != NULL ||
          strstr(g_screen_state.text[r], "CLEAR") != NULL ||
          strstr(g_screen_state.text[r], "A B C D E") != NULL ||
          strstr(g_screen_state.text[r], "Q W E R T") != NULL ||
          strstr(g_screen_state.text[r], "1 2 3 4 5") != NULL) {
        is_keyboard = 1;
        break;
      }
    }
  }

  // Check for Confirmation Dialog / Modal (e.g. "LOSE CHANGES TO CURRENT SONG?", "OVERWRITE?", "DELETE?", "OK CANCEL", "YES NO")
  int is_confirm_dialog = 0;
  if (!is_keyboard && !has_dir_entry) {
    for (int r = 0; r < M8_SCREEN_ROWS; r++) {
      const char *line = g_screen_state.text[r];
      if (strchr(line, '?') != NULL ||
          strstr(line, "LOSE CHANGES") != NULL ||
          strstr(line, "OVERWRITE") != NULL ||
          strstr(line, "ARE YOU SURE") != NULL ||
          ((strstr(line, "OK") != NULL || strstr(line, "YES") != NULL) &&
           (strstr(line, "CANCEL") != NULL || strstr(line, "NO") != NULL))) {
        is_confirm_dialog = 1;
        break;
      }
    }
  }

  if (strncmp(header_raw, "PROJECT", 7) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "PROJECT");
  } else if (strncmp(header_raw, "SONG", 4) == 0 || strstr(header_raw, "LIVE") != NULL) {
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
  } else if (strstr(header_raw, "EQ") != NULL || strstr(header_raw, "EQUALIZER") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "EQ");
  } else if (strncmp(header_raw, "MIXER", 5) == 0 || strncmp(header_raw, "MIX", 3) == 0) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "MIXER");
  } else if (strstr(header_raw, "POOL") != NULL || strstr(header_raw, "INST. POOL") != NULL ||
             strstr(header_raw, "INST POOL") != NULL || strstr(header_raw, "INST.POOL") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "INST_POOL");
  } else if (strstr(header_raw, "MODS") != NULL || strstr(header_raw, "MODULATOR") != NULL ||
             strstr(header_raw, "MODIFIER") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "INST_MODS");
  } else if (strncmp(header_raw, "INST", 4) == 0 || strstr(header_raw, "SYNTH") != NULL ||
             strstr(header_raw, "SAMPLER") != NULL || strstr(header_raw, "WAVSYN") != NULL ||
             strstr(header_raw, "MACRO") != NULL || strstr(header_raw, "FMSYN") != NULL ||
             strstr(header_raw, "HYPER") != NULL) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "INSTRUMENT");
  } else if (is_confirm_dialog) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "CONFIRM_DIALOG");
  } else if (is_keyboard) {
    snprintf(g_screen_state.active_screen, sizeof(g_screen_state.active_screen), "KEYBOARD");
  } else if (has_dir_entry ||
             strstr(header_raw, "DIRECTORY") != NULL || strstr(header_raw, "DIR:") != NULL ||
             strstr(header_raw, "LOAD") != NULL || strstr(header_raw, "SAVE") != NULL ||
             strstr(header_raw, "IMPORT") != NULL || strstr(header_raw, "EXPORT") != NULL ||
             strstr(header_raw, "BROWSER") != NULL || strstr(header_raw, "SELECT") != NULL) {
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

  // 2. Resolve Active Input & Value with Token Snapping
  int col = g_screen_state.cursor_col;
  int row = g_screen_state.cursor_row;
  int width = g_screen_state.cursor_width;

  // If cursor landed on an empty row (e.g. inter-line spacing or bottom bracket row),
  // snap to the adjacent row containing text
  if (row >= 0 && row < M8_SCREEN_ROWS) {
    int is_empty_row = 1;
    for (int i = 0; i < M8_SCREEN_COLS; i++) {
      if (g_screen_state.text[row][i] != ' ') {
        is_empty_row = 0;
        break;
      }
    }
    if (is_empty_row) {
      if (row > 0) {
        int prev_has_text = 0;
        for (int i = 0; i < M8_SCREEN_COLS; i++) {
          if (g_screen_state.text[row - 1][i] != ' ') {
            prev_has_text = 1;
            break;
          }
        }
        if (prev_has_text) {
          row = row - 1;
          g_screen_state.cursor_row = row;
        }
      } else if (row + 1 < M8_SCREEN_ROWS) {
        int next_has_text = 0;
        for (int i = 0; i < M8_SCREEN_COLS; i++) {
          if (g_screen_state.text[row + 1][i] != ' ') {
            next_has_text = 1;
            break;
          }
        }
        if (next_has_text) {
          row = row + 1;
          g_screen_state.cursor_row = row;
        }
      }
    }
  }

  if (col >= 0 && col < M8_SCREEN_COLS && row >= 0 && row < M8_SCREEN_ROWS) {
    snap_cursor_to_token(row, &col, &width);
    g_screen_state.cursor_col = col;
    g_screen_state.cursor_width = width;

    extract_token_at(g_screen_state.text[row], col, width, g_screen_state.current_value,
                     sizeof(g_screen_state.current_value));

    int matched = 0;

    // Special Dynamic Screen Matrix: SONG (Tracks 1..8 x Song Chains)
    if (strcmp(g_screen_state.active_screen, "SONG") == 0 && row >= 3 && row <= 28) {
      const char *line = g_screen_state.text[row];
      char row_hex[4] = "00";
      int p = 0;
      while (line[p] == ' ' && p < 4) p++;
      if (isxdigit((unsigned char)line[p]) && isxdigit((unsigned char)line[p + 1])) {
        row_hex[0] = (char)toupper((unsigned char)line[p]);
        row_hex[1] = (char)toupper((unsigned char)line[p + 1]);
        row_hex[2] = '\0';
      } else {
        snprintf(row_hex, sizeof(row_hex), "%02X", row >= 7 ? row - 7 : row);
      }

      int track_num = 0;
      if (col >= 3 && col <= 5) track_num = 1;
      else if (col >= 6 && col <= 8) track_num = 2;
      else if (col >= 9 && col <= 11) track_num = 3;
      else if (col >= 12 && col <= 14) track_num = 4;
      else if (col >= 15 && col <= 17) track_num = 5;
      else if (col >= 18 && col <= 20) track_num = 6;
      else if (col >= 21 && col <= 23) track_num = 7;
      else if (col >= 24 && col <= 27) track_num = 8;

      if (track_num >= 1 && track_num <= 8) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "TRACK%d_CHAIN_%s", track_num, row_hex);
        matched = 1;
      } else if (col <= 2) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "ROW_%s", row_hex);
        matched = 1;
      }
    }
    // Special Dynamic Screen Matrix: CHAIN (Steps 00..0F: Phrase & Transpose)
    else if (strcmp(g_screen_state.active_screen, "CHAIN") == 0 && row >= 3 && row <= 28) {
      const char *line = g_screen_state.text[row];
      char step_hex[4] = "00";
      int p = 0;
      while (line[p] == ' ' && p < 3) p++;
      if (isxdigit((unsigned char)line[p])) {
        if (isxdigit((unsigned char)line[p + 1])) {
          step_hex[0] = (char)toupper((unsigned char)line[p]);
          step_hex[1] = (char)toupper((unsigned char)line[p + 1]);
          step_hex[2] = '\0';
        } else {
          step_hex[0] = '0';
          step_hex[1] = (char)toupper((unsigned char)line[p]);
          step_hex[2] = '\0';
        }
      } else {
        snprintf(step_hex, sizeof(step_hex), "%02X", row >= 7 ? row - 7 : row);
      }

      if (col >= 2 && col <= 4) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "PHRASE_%s", step_hex);
        matched = 1;
      } else if (col >= 5 && col <= 8) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "TRANSPOSE_%s", step_hex);
        matched = 1;
      } else if (col <= 1) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "STEP_%s", step_hex);
        matched = 1;
      }
    }
    // Special Dynamic Screen Matrix: PHRASE (Steps 00..0F: Note, Volume, Instrument, FX1, FX1_Val, FX2, FX2_Val, FX3, FX3_Val)
    else if (strcmp(g_screen_state.active_screen, "PHRASE") == 0 && row >= 3 && row <= 28) {
      const char *line = g_screen_state.text[row];
      char step_hex[4] = "00";
      int p = 0;
      while (line[p] == ' ' || line[p] == '<') p++;
      if (isxdigit((unsigned char)line[p])) {
        if (isxdigit((unsigned char)line[p + 1])) {
          step_hex[0] = (char)toupper((unsigned char)line[p]);
          step_hex[1] = (char)toupper((unsigned char)line[p + 1]);
          step_hex[2] = '\0';
        } else {
          step_hex[0] = '0';
          step_hex[1] = (char)toupper((unsigned char)line[p]);
          step_hex[2] = '\0';
        }
      } else {
        snprintf(step_hex, sizeof(step_hex), "%02X", row >= 7 ? row - 7 : row);
      }

      if (col >= 2 && col <= 5) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "NOTE_%s", step_hex);
        matched = 1;
      } else if (col >= 6 && col <= 8) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "VOLUME_%s", step_hex);
        matched = 1;
      } else if (col >= 9 && col <= 11) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "INSTRUMENT_%s", step_hex);
        matched = 1;
      } else if (col >= 12 && col <= 14) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX1_%s", step_hex);
        matched = 1;
      } else if (col >= 15 && col <= 17) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX1_VAL_%s", step_hex);
        matched = 1;
      } else if (col >= 18 && col <= 20) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX2_%s", step_hex);
        matched = 1;
      } else if (col >= 21 && col <= 23) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX2_VAL_%s", step_hex);
        matched = 1;
      } else if (col >= 24 && col <= 26) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX3_%s", step_hex);
        matched = 1;
      } else if (col >= 27 && col <= 31) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "FX3_VAL_%s", step_hex);
        matched = 1;
      } else if (col <= 1) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "STEP_%s", step_hex);
        matched = 1;
      }
    }
    // Special Dynamic Screen Matrix: TABLE (Steps 00..0F: Note, Volume, FX1, FX1_Val, FX2, FX2_Val, FX3, FX3_Val)
    else if (strcmp(g_screen_state.active_screen, "TABLE") == 0 && row >= 3 && row <= 28) {
      const char *line = g_screen_state.text[row];
      char step_hex[4] = "00";
      int p = 0;
      while (line[p] == ' ' || line[p] == '<') p++;
      if (isxdigit((unsigned char)line[p])) {
        if (isxdigit((unsigned char)line[p + 1])) {
          step_hex[0] = (char)toupper((unsigned char)line[p]);
          step_hex[1] = (char)toupper((unsigned char)line[p + 1]);
          step_hex[2] = '\0';
        } else {
          step_hex[0] = '0';
          step_hex[1] = (char)toupper((unsigned char)line[p]);
          step_hex[2] = '\0';
        }
      } else {
        snprintf(step_hex, sizeof(step_hex), "%02X", row >= 7 ? row - 7 : row);
      }

      int is_packed = (col <= 12) || (strlen(line) > 10 && line[10] != ' ') || (strlen(line) > 9 && line[9] != ' ');
      if (is_packed && col <= 12) is_packed = 1;
      else if (!is_packed && strlen(line) > 13 && line[13] != ' ' && strlen(line) > 10 && line[10] == ' ') is_packed = 0;

      if (col <= 1) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "STEP_%s", step_hex);
        matched = 1;
      } else if (col >= 2 && col <= 5) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "NOTE_%s", step_hex);
        matched = 1;
      } else if (col >= 6 && col <= 8) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "VOLUME_%s", step_hex);
        matched = 1;
      } else if (is_packed) {
        if (col >= 9 && col <= 12) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX1_%s", step_hex);
          matched = 1;
        } else if (col >= 13 && col <= 15) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX1_VAL_%s", step_hex);
          matched = 1;
        } else if (col >= 16 && col <= 18) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX2_%s", step_hex);
          matched = 1;
        } else if (col >= 19 && col <= 21) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX2_VAL_%s", step_hex);
          matched = 1;
        } else if (col >= 22 && col <= 24) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX3_%s", step_hex);
          matched = 1;
        } else if (col >= 25 && col <= 28) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX3_VAL_%s", step_hex);
          matched = 1;
        }
      } else {
        if (col >= 12 && col <= 14) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX1_%s", step_hex);
          matched = 1;
        } else if (col >= 15 && col <= 17) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX1_VAL_%s", step_hex);
          matched = 1;
        } else if (col >= 18 && col <= 20) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX2_%s", step_hex);
          matched = 1;
        } else if (col >= 21 && col <= 23) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX2_VAL_%s", step_hex);
          matched = 1;
        } else if (col >= 24 && col <= 26) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX3_%s", step_hex);
          matched = 1;
        } else if (col >= 27 && col <= 31) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "FX3_VAL_%s", step_hex);
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: INST_MODS (Modulators 1..4: Type, Dest, Amount, and engine parameters)
    else if (strcmp(g_screen_state.active_screen, "INST_MODS") == 0) {
      if (row <= 2 && col >= 5 && col <= 12) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "INST_NUM");
        matched = 1;
      } else {
        // Find which MOD slot (1..4) the current row belongs to
        int mod_slot = 1;
        for (int r = row; r >= 0; r--) {
          const char *l = g_screen_state.text[r];
          if (strstr(l, "MOD 4") || strstr(l, "MOD4") || strstr(l, "MOD.4")) { mod_slot = 4; break; }
          if (strstr(l, "MOD 3") || strstr(l, "MOD3") || strstr(l, "MOD.3")) { mod_slot = 3; break; }
          if (strstr(l, "MOD 2") || strstr(l, "MOD2") || strstr(l, "MOD.2")) { mod_slot = 2; break; }
          if (strstr(l, "MOD 1") || strstr(l, "MOD1") || strstr(l, "MOD.1")) { mod_slot = 1; break; }
        }

        const char *line = g_screen_state.text[row];
        // Check if cursor is on the "MOD <N> [TYPE]" header line
        if (strstr(line, "MOD 1") || strstr(line, "MOD 2") || strstr(line, "MOD 3") || strstr(line, "MOD 4") ||
            strstr(line, "MOD1") || strstr(line, "MOD2") || strstr(line, "MOD3") || strstr(line, "MOD4")) {
          if (col >= 6) {
            snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "MOD%d_TYPE", mod_slot);
            matched = 1;
          } else {
            snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "MOD%d_SLOT", mod_slot);
            matched = 1;
          }
        } else {
          // Parameter line under MOD slot: extract left label
          char left_label[32] = {0};
          extract_left_label(line, col, left_label, sizeof(left_label));
          if (strlen(left_label) > 0) {
            char std_label[32] = {0};
            for (size_t i = 0; i < strlen(left_label) && i < sizeof(std_label) - 1; i++) {
              std_label[i] = (char)toupper((unsigned char)left_label[i]);
            }
            if (strcmp(std_label, "AMOUNT") == 0) snprintf(std_label, sizeof(std_label), "AMT");
            else if (strcmp(std_label, "FREQ") == 0 || strcmp(std_label, "RATE") == 0) snprintf(std_label, sizeof(std_label), "FRQ");
            else if (strcmp(std_label, "SHAPE") == 0 || strcmp(std_label, "OSC") == 0) snprintf(std_label, sizeof(std_label), "SHP");
            else if (strcmp(std_label, "TRIG") == 0 || strcmp(std_label, "RE-TRIG") == 0) snprintf(std_label, sizeof(std_label), "TRG");
            else if (strcmp(std_label, "ATTACK") == 0) snprintf(std_label, sizeof(std_label), "ATK");
            else if (strcmp(std_label, "HOLD") == 0) snprintf(std_label, sizeof(std_label), "HLD");
            else if (strcmp(std_label, "DECAY") == 0) snprintf(std_label, sizeof(std_label), "DEC");
            else if (strcmp(std_label, "SUSTAIN") == 0) snprintf(std_label, sizeof(std_label), "SUS");
            else if (strcmp(std_label, "RELEASE") == 0) snprintf(std_label, sizeof(std_label), "REL");

            snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "MOD%d_%s", mod_slot, std_label);
            matched = 1;
          } else {
            snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "MOD%d_PARAM", mod_slot);
            matched = 1;
          }
        }
      }
    }

    // Special Dynamic Screen Matrix: GROOVE (Steps 00..0F: Step, Ticks, and Header Groove Num)
    else if (strcmp(g_screen_state.active_screen, "GROOVE") == 0) {
      if (row <= 2 && col >= 5 && col <= 12) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "GROOVE_NUM");
        matched = 1;
      } else if (row >= 3 && row <= 28) {
        char step_hex[4] = "00";
        char raw_step[8] = {0};
        extract_token_at(g_screen_state.text[row], 0, 2, raw_step, sizeof(raw_step));
        if (strlen(raw_step) >= 2 && isxdigit((unsigned char)raw_step[0]) &&
            isxdigit((unsigned char)raw_step[1])) {
          snprintf(step_hex, sizeof(step_hex), "%c%c", (char)toupper((unsigned char)raw_step[0]),
                   (char)toupper((unsigned char)raw_step[1]));
        } else {
          int step_idx = row - 3;
          if (step_idx < 0) step_idx = 0;
          if (step_idx > 15) step_idx = 15;
          snprintf(step_hex, sizeof(step_hex), "%02X", step_idx);
        }

        if (col <= 1) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "STEP_%s", step_hex);
          matched = 1;
        } else {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "TICKS_%s", step_hex);
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: SCALE (12 Note Intervals: Note, Enable, Offset, and Header Scale/Key/Name)
    else if (strcmp(g_screen_state.active_screen, "SCALE") == 0) {
      const char *line = g_screen_state.text[row];
      if (row == 0 || strstr(line, "SCALE") != NULL) {
        if (col >= 5 && col <= 10) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "SCALE_NUM");
          matched = 1;
        } else if (col >= 11 || strstr(line, "KEY") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "KEY");
          matched = 1;
        }
      } else if (row == 1 || strstr(line, "NAME") != NULL) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NAME");
        matched = 1;
      } else if (row >= 3 && row <= 28) {
        char raw_note[8] = {0};
        extract_token_at(line, 0, 3, raw_note, sizeof(raw_note));
        int interval = -1;
        if (strncmp(raw_note, "C#", 2) == 0) interval = 1;
        else if (strncmp(raw_note, "C", 1) == 0) interval = 0;
        else if (strncmp(raw_note, "D#", 2) == 0) interval = 3;
        else if (strncmp(raw_note, "D", 1) == 0) interval = 2;
        else if (strncmp(raw_note, "E", 1) == 0) interval = 4;
        else if (strncmp(raw_note, "F#", 2) == 0) interval = 6;
        else if (strncmp(raw_note, "F", 1) == 0) interval = 5;
        else if (strncmp(raw_note, "G#", 2) == 0) interval = 8;
        else if (strncmp(raw_note, "G", 1) == 0) interval = 7;
        else if (strncmp(raw_note, "A#", 2) == 0) interval = 10;
        else if (strncmp(raw_note, "A", 1) == 0) interval = 9;
        else if (strncmp(raw_note, "B", 1) == 0) interval = 11;

        char int_hex[4] = "00";
        if (interval >= 0 && interval <= 11) {
          snprintf(int_hex, sizeof(int_hex), "%02X", interval);
        } else {
          int idx = row - 3;
          if (idx < 0) idx = 0;
          if (idx > 11) idx = 11;
          snprintf(int_hex, sizeof(int_hex), "%02X", idx);
        }

        if (col <= 3) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "NOTE_%s", int_hex);
          matched = 1;
        } else if (col >= 4 && col <= 7) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "ENABLE_%s", int_hex);
          matched = 1;
        } else {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                   "OFFSET_%s", int_hex);
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: INST_POOL (Instrument slots: Slot, Type, Name)
    else if (strcmp(g_screen_state.active_screen, "INST_POOL") == 0 && row >= 3 && row <= 28) {
      const char *line = g_screen_state.text[row];
      char raw_slot[8] = {0};
      extract_token_at(line, 0, 3, raw_slot, sizeof(raw_slot));
      char slot_hex[4] = "00";
      if (strlen(raw_slot) >= 2 && isxdigit((unsigned char)raw_slot[0]) &&
          isxdigit((unsigned char)raw_slot[1])) {
        snprintf(slot_hex, sizeof(slot_hex), "%c%c", (char)toupper((unsigned char)raw_slot[0]),
                 (char)toupper((unsigned char)raw_slot[1]));
      } else {
        int idx = row - 3;
        if (idx < 0) idx = 0;
        if (idx > 63) idx = 63;
        snprintf(slot_hex, sizeof(slot_hex), "%02X", idx);
      }

      if (col <= 3) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "INST_%s", slot_hex);
        matched = 1;
      } else if (col >= 4 && col <= 12) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "TYPE_%s", slot_hex);
        matched = 1;
      } else {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input),
                 "NAME_%s", slot_hex);
        matched = 1;
      }
    }

    // Special Dynamic Screen Matrix: INSTRUMENT (Sampler, Wavsynth, Macro, FMSynth, Hyper, MIDI)
    else if (strcmp(g_screen_state.active_screen, "INSTRUMENT") == 0) {
      const char *line = g_screen_state.text[row];
      // 1. Header row 0: Instrument number (e.g. SAMPLER 00 or INSTRUMENT 00)
      if (row == 0 && (strstr(line, "SAMPLER") != NULL || strstr(line, "INST") != NULL ||
                       strstr(line, "WAVSYN") != NULL || strstr(line, "MACRO") != NULL ||
                       strstr(line, "FMSYN") != NULL || strstr(line, "HYPER") != NULL ||
                       strstr(line, "MIDI") != NULL)) {
        if (col >= 5 && col <= 15) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "INST_NUM");
          matched = 1;
        }
      }
      // 2. Header rows 1..3: TYPE, NAME, TRANSP, TABLE
      if (!matched && row <= 3) {
        if (strstr(line, "TYPE") != NULL && col <= 14) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "INST_TYPE");
          matched = 1;
        } else if (strstr(line, "NAME") != NULL && col >= 15) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NAME");
          matched = 1;
        } else if ((strstr(line, "TRANS") != NULL || strstr(line, "TRANSP") != NULL) && col <= 14) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "TRANSPOSE");
          matched = 1;
        } else if (strstr(line, "TABLE") != NULL && col >= 15) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "TABLE");
          matched = 1;
        }
      }

      // 3. Parameter Rows: Advanced Multi-Column Row Resolver (Left, Middle, Right columns)
      if (!matched) {
        if (resolve_multi_column_row(line, col, g_screen_state.active_input, sizeof(g_screen_state.active_input),
                                     g_screen_state.current_value, sizeof(g_screen_state.current_value))) {
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: FILE_BROWSER (Directory Traversal / File Picker)
    else if (strcmp(g_screen_state.active_screen, "FILE_BROWSER") == 0) {
      if (row <= 1) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CURRENT_PATH");
        matched = 1;
      } else {
        const char *val = g_screen_state.current_value;
        if (strcmp(val, "/..") == 0 || strstr(val, "/..") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "PARENT_DIR");
          matched = 1;
        } else if (val[0] == '/' || (strlen(val) > 0 && val[strlen(val) - 1] == '/')) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "DIRECTORY_ITEM");
          matched = 1;
        } else if (strstr(val, ".M8S") != NULL || strstr(val, ".m8s") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "SONG_FILE");
          matched = 1;
        } else if (strstr(val, ".M8I") != NULL || strstr(val, ".m8i") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "INSTRUMENT_FILE");
          matched = 1;
        } else if (strstr(val, ".WAV") != NULL || strstr(val, ".wav") != NULL ||
                   strstr(val, ".AIF") != NULL || strstr(val, ".aif") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "SAMPLE_FILE");
          matched = 1;
        } else if (strstr(val, "LOAD") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "LOAD_BTN");
          matched = 1;
        } else if (strstr(val, "CANCEL") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CANCEL_BTN");
          matched = 1;
        } else if (strstr(val, "SELECT") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "SELECT_BTN");
          matched = 1;
        } else if (strstr(val, "NEW") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NEW_BTN");
          matched = 1;
        } else if (strstr(val, "DELETE") != NULL) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "DELETE_BTN");
          matched = 1;
        } else {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "FILE_ITEM");
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: KEYBOARD / Character Picker
    else if (strcmp(g_screen_state.active_screen, "KEYBOARD") == 0) {
      if (row <= 6) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NAME_BUFFER");
        matched = 1;
      } else {
        const char *val = g_screen_state.current_value;
        if (strcmp(val, "SPACE") == 0) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "SPACE_BTN");
          matched = 1;
        } else if (strcmp(val, "OK") == 0) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "OK_BTN");
          matched = 1;
        } else if (strcmp(val, "CANCEL") == 0) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CANCEL_BTN");
          matched = 1;
        } else if (strcmp(val, "CLEAR") == 0 || strcmp(val, "CLR") == 0) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CLEAR_BTN");
          matched = 1;
        } else if (strcmp(val, "DEL") == 0 || strcmp(val, "DELETE") == 0) {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "DELETE_BTN");
          matched = 1;
        } else if (strlen(val) == 1 && val[0] != ' ') {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "PICKER_CHAR");
          matched = 1;
        } else {
          snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "KEY_CHAR");
          matched = 1;
        }
      }
    }

    // Special Dynamic Screen Matrix: CONFIRM_DIALOG (Modal Prompts & Alerts)
    else if (strcmp(g_screen_state.active_screen, "CONFIRM_DIALOG") == 0) {
      const char *val = g_screen_state.current_value;
      if (strcmp(val, "OK") == 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "OK_BTN");
        matched = 1;
      } else if (strcmp(val, "CANCEL") == 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CANCEL_BTN");
        matched = 1;
      } else if (strcmp(val, "YES") == 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "YES_BTN");
        matched = 1;
      } else if (strcmp(val, "NO") == 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "NO_BTN");
        matched = 1;
      } else if (strcmp(val, "DELETE") == 0 || strcmp(val, "DEL") == 0) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "DELETE_BTN");
        matched = 1;
      } else if (strchr(val, '?') != NULL) {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "PROMPT_TEXT");
        matched = 1;
      } else {
        snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "ACTION_BTN");
        matched = 1;
      }
    }

    // Try static UI map if not already matched
    if (!matched) {
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
    }

    // If not matched, try Advanced Multi-Column Row Resolver
    if (!matched) {
      if (resolve_multi_column_row(g_screen_state.text[row], col, g_screen_state.active_input, sizeof(g_screen_state.active_input),
                                   g_screen_state.current_value, sizeof(g_screen_state.current_value))) {
        matched = 1;
      }
    }

    // If still not matched, fallback to row/col coordinate
    if (!matched) {
      snprintf(g_screen_state.active_input, sizeof(g_screen_state.active_input), "CELL_R%02d_C%02d", row, col);
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
