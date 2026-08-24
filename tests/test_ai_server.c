// tests/test_ai_server.c - Unit and integration tests for AI Server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ai_server.h"
#include "ai_screen.h"
#include "input.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

int m8_send_msg_controller(const uint8_t input) {
  (void)input;
  return 1;
}

int renderer_read_pixels_rgb24(void *dst_buffer, int target_width, int target_height) {
  if (dst_buffer) {
    memset(dst_buffer, 0x42, target_width * target_height * 3);
  }
  return 1;
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(expr, msg) do { \
  tests_run++; \
  if (expr) { \
    tests_passed++; \
    printf("  [PASS] %s\n", msg); \
  } else { \
    printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
  } \
  fflush(stdout); \
} while(0)

static void test_single_keys(void) {
  printf("Running test_single_keys...\n");
  uint8_t mask = 0;
  char err[128] = {0};

  TEST_ASSERT(ai_server_parse_key_combination("UP", &mask, err, sizeof(err)) && mask == key_up, "UP matches key_up (0x40)");
  TEST_ASSERT(ai_server_parse_key_combination("down", &mask, err, sizeof(err)) && mask == key_down, "down matches key_down (0x20)");
  TEST_ASSERT(ai_server_parse_key_combination("LEFT", &mask, err, sizeof(err)) && mask == key_left, "LEFT matches key_left (0x80)");
  TEST_ASSERT(ai_server_parse_key_combination("right", &mask, err, sizeof(err)) && mask == key_right, "right matches key_right (0x04)");
  TEST_ASSERT(ai_server_parse_key_combination("EDIT", &mask, err, sizeof(err)) && mask == key_edit, "EDIT matches key_edit (0x01)");
  TEST_ASSERT(ai_server_parse_key_combination("opt", &mask, err, sizeof(err)) && mask == key_opt, "opt matches key_opt (0x02)");
  TEST_ASSERT(ai_server_parse_key_combination("OPTION", &mask, err, sizeof(err)) && mask == key_opt, "OPTION matches key_opt (0x02)");
  TEST_ASSERT(ai_server_parse_key_combination("SHIFT", &mask, err, sizeof(err)) && mask == key_select, "SHIFT matches key_select (0x10)");
  TEST_ASSERT(ai_server_parse_key_combination("select", &mask, err, sizeof(err)) && mask == key_select, "select matches key_select (0x10)");
  TEST_ASSERT(ai_server_parse_key_combination("PLAY", &mask, err, sizeof(err)) && mask == key_start, "PLAY matches key_start (0x08)");
  TEST_ASSERT(ai_server_parse_key_combination("start", &mask, err, sizeof(err)) && mask == key_start, "start matches key_start (0x08)");
}

static void test_key_combinations(void) {
  printf("Running test_key_combinations...\n");
  uint8_t mask = 0;
  char err[128] = {0};

  TEST_ASSERT(ai_server_parse_key_combination("SHIFT+PLAY", &mask, err, sizeof(err)) && mask == (key_select | key_start), "SHIFT+PLAY (0x18)");
  TEST_ASSERT(ai_server_parse_key_combination("EDIT+OPT", &mask, err, sizeof(err)) && mask == (key_edit | key_opt), "EDIT+OPT (0x03)");
  TEST_ASSERT(ai_server_parse_key_combination("UP+LEFT+EDIT", &mask, err, sizeof(err)) && mask == (key_up | key_left | key_edit), "UP+LEFT+EDIT (0xC1)");
  TEST_ASSERT(ai_server_parse_key_combination("  opt  +  edit  ", &mask, err, sizeof(err)) && mask == (key_opt | key_edit), "Whitespace tolerance in combination");
  TEST_ASSERT(ai_server_parse_key_combination("SHIFT|PLAY", &mask, err, sizeof(err)) && mask == (key_select | key_start), "Pipe delimiter in combination");
  TEST_ASSERT(ai_server_parse_key_combination("SHIFT,PLAY", &mask, err, sizeof(err)) && mask == (key_select | key_start), "Comma delimiter in combination");
  TEST_ASSERT(ai_server_parse_key_combination("UP DOWN LEFT RIGHT EDIT OPT SHIFT PLAY", &mask, err, sizeof(err)) && mask == 0xFF, "All 8 buttons combined equals 0xFF");
}

static void test_invalid_keys(void) {
  printf("Running test_invalid_keys...\n");
  uint8_t mask = 0;
  char err[128] = {0};

  TEST_ASSERT(!ai_server_parse_key_combination("FOOBAR", &mask, err, sizeof(err)), "FOOBAR is rejected");
  TEST_ASSERT(!ai_server_parse_key_combination("SHIFT+INVALID", &mask, err, sizeof(err)), "SHIFT+INVALID is rejected");
  TEST_ASSERT(!ai_server_parse_key_combination("", &mask, err, sizeof(err)), "Empty string is rejected");
  TEST_ASSERT(!ai_server_parse_key_combination("   ", &mask, err, sizeof(err)), "Whitespace only is rejected");
}

static void test_audio_recording(void) {
  printf("Running test_audio_recording (Float32 and PCM WAV formats)...\n");
  const char *test_fn = "test_rec_direct.wav";
  remove(test_fn);

  // 1. Test 48kHz IEEE Float32 Recording
  ai_server_set_audio_format(48000, 2, 32, 1);
  TEST_ASSERT(ai_server_rec_start(test_fn) == 1, "ai_server_rec_start succeeds");
  TEST_ASSERT(ai_is_recording == true, "ai_is_recording flag is true");
  TEST_ASSERT(ai_wav_file != NULL, "ai_wav_file handle is open");

  uint8_t dummy_audio[1024];
  memset(dummy_audio, 0x55, sizeof(dummy_audio));
  ai_server_record_audio(dummy_audio, sizeof(dummy_audio));
  TEST_ASSERT(wav_data_size == sizeof(dummy_audio), "wav_data_size updated to 1024");

  uint32_t final_size = 0;
  TEST_ASSERT(ai_server_rec_stop(&final_size) == 1, "ai_server_rec_stop succeeds");
  TEST_ASSERT(final_size == 1024, "final_size reported as 1024");
  TEST_ASSERT(ai_is_recording == false, "ai_is_recording flag reset to false");

  FILE *f = fopen(test_fn, "rb");
  TEST_ASSERT(f != NULL, "WAV file was created on disk");
  if (f) {
    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    TEST_ASSERT(memcmp(hdr, "RIFF", 4) == 0, "WAV header starts with 'RIFF'");
    TEST_ASSERT(memcmp(hdr + 8, "WAVE", 4) == 0, "WAV format is 'WAVE'");
    TEST_ASSERT(hdr[20] == 3, "WAV format tag is 3 (IEEE Float)");
    uint32_t srate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    TEST_ASSERT(srate == 48000, "WAV sample rate is 48000 Hz");
    uint16_t bits = hdr[34] | (hdr[35] << 8);
    TEST_ASSERT(bits == 32, "WAV bit depth is 32-bit");
    fclose(f);
    remove(test_fn);
  }

  // 2. Test 44.1kHz 16-bit PCM Recording
  ai_server_set_audio_format(44100, 2, 16, 0);
  ai_server_rec_start(test_fn);
  ai_server_record_audio(dummy_audio, sizeof(dummy_audio));
  ai_server_rec_stop(&final_size);
  f = fopen(test_fn, "rb");
  if (f) {
    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    TEST_ASSERT(hdr[20] == 1, "WAV format tag is 1 (PCM)");
    uint32_t srate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    TEST_ASSERT(srate == 44100, "WAV sample rate is 44100 Hz");
    uint16_t bits = hdr[34] | (hdr[35] << 8);
    TEST_ASSERT(bits == 16, "WAV bit depth is 16-bit");
    fclose(f);
    remove(test_fn);
  }
}

static void test_live_tcp_server(void) {
  printf("Running test_live_tcp_server...\n");

  if (!SDL_Init(SDL_INIT_EVENTS)) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return;
  }

  // Start AI server
  if (!ai_server_init()) {
    printf("ai_server_init failed!\n");
    return;
  }

  // Give server thread a moment to bind & listen
  SDL_Delay(200);

  // Connect TCP client
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT(sock != INVALID_SOCKET, "Created client test socket");

  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(AI_SERVER_HOST);
  saddr.sin_port = htons(AI_SERVER_PORT);

  int conn = connect(sock, (struct sockaddr *)&saddr, sizeof(saddr));
  TEST_ASSERT(conn == 0, "Connected to 127.0.0.1:9123");

  if (conn == 0) {
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

    char buf[256];

    // Test PING -> PONG
    const char *ping_cmd = "PING\n";
    send(sock, ping_cmd, (int)strlen(ping_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "PONG") != NULL, "TCP: PING -> PONG");

    // Test KEY SHIFT+PLAY -> OK KEY 0x18
    const char *key_cmd = "KEY SHIFT+PLAY\n";
    send(sock, key_cmd, (int)strlen(key_cmd), 0);

    SDL_Event ev;
    int key_event_handled = 0;
    for (int i = 0; i < 60; i++) {
      while (SDL_PollEvent(&ev)) {
        if (ev.type == AI_INPUT_EVENT) {
          ai_server_handle_event(NULL, &ev);
          key_event_handled = 1;
        }
      }
      if (key_event_handled) break;
      SDL_Delay(5);
    }
    TEST_ASSERT(key_event_handled == 1, "SDL event loop handled AI_INPUT_EVENT for key");

    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK KEY 0x18") != NULL, "TCP: KEY SHIFT+PLAY -> OK KEY 0x18");

    // Test SETTLE -> OK SETTLE 30
    const char *settle_cmd = "SETTLE 30\n";
    send(sock, settle_cmd, (int)strlen(settle_cmd), 0);
    for (int i = 0; i < 50; i++) {
      while (SDL_PollEvent(&ev)) {
        if (ev.type == AI_INPUT_EVENT) {
          ai_server_handle_event(NULL, &ev);
        }
      }
      SDL_Delay(5);
    }
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK SETTLE 30") != NULL, "TCP: SETTLE -> OK SETTLE 30");

    // Test KEY INVALID -> ERROR
    const char *bad_cmd = "KEY BADKEY\n";
    send(sock, bad_cmd, (int)strlen(bad_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "ERROR") != NULL, "TCP: KEY BADKEY -> ERROR");

    // Test SCREENSHOT -> OK SCREENSHOT 230400\n + 230,400 bytes
    const char *screenshot_cmd = "SCREENSHOT\n";
    send(sock, screenshot_cmd, (int)strlen(screenshot_cmd), 0);

    int event_handled = 0;
    for (int i = 0; i < 300; i++) {
      while (SDL_PollEvent(&ev)) {
        if (ev.type == AI_SCREENSHOT_EVENT) {
          ai_server_handle_screenshot(NULL);
          event_handled = 1;
          break;
        }
      }
      if (event_handled) break;
      SDL_Delay(10);
    }
    TEST_ASSERT(event_handled == 1, "SDL event loop caught AI_SCREENSHOT_EVENT");

    // Read header line "OK SCREENSHOT 230400\n"
    memset(buf, 0, sizeof(buf));
    int line_idx = 0;
    while (line_idx < (int)sizeof(buf) - 1) {
      char c;
      int r = recv(sock, &c, 1, 0);
      if (r <= 0) break;
      buf[line_idx++] = c;
      if (c == '\n') break;
    }
    buf[line_idx] = '\0';
    TEST_ASSERT(strstr(buf, "OK SCREENSHOT 230400") != NULL, "TCP: SCREENSHOT header is OK SCREENSHOT 230400");

    char *pixel_data = malloc(AI_SCREENSHOT_BUFFER_SIZE);
    size_t total_recv = 0;
    while (total_recv < AI_SCREENSHOT_BUFFER_SIZE) {
      int n = recv(sock, pixel_data + total_recv, (int)(AI_SCREENSHOT_BUFFER_SIZE - total_recv), 0);
      if (n <= 0) break;
      total_recv += n;
    }
    TEST_ASSERT(total_recv == AI_SCREENSHOT_BUFFER_SIZE, "TCP: SCREENSHOT received 230,400 raw RGB bytes");
    TEST_ASSERT((uint8_t)pixel_data[0] == 0x42, "TCP: Pixel data content verified");
    free(pixel_data);

    // Test REC_START over TCP
    const char *rec_start_cmd = "REC_START test_tcp_rec.wav\n";
    send(sock, rec_start_cmd, (int)strlen(rec_start_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK REC_START test_tcp_rec.wav") != NULL, "TCP: REC_START -> OK REC_START");

    // Simulate audio playback feeding 2048 bytes
    uint8_t mock_audio[2048];
    memset(mock_audio, 0x33, sizeof(mock_audio));
    ai_server_record_audio(mock_audio, sizeof(mock_audio));

    // Test REC_STOP over TCP
    const char *rec_stop_cmd = "REC_STOP\n";
    send(sock, rec_stop_cmd, (int)strlen(rec_stop_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK REC_STOP 2048") != NULL, "TCP: REC_STOP -> OK REC_STOP 2048");

    // Test waveform playback state detection
    uint8_t osc_data[64];
    memset(osc_data, 128, sizeof(osc_data));
    ai_screen_on_waveform(osc_data, sizeof(osc_data));

    // Test GET_STATE command over TCP (single-line JSON response)
    const char *state_cmd = "GET_STATE\n";
    send(sock, state_cmd, (int)strlen(state_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK STATE") != NULL, "TCP: GET_STATE -> OK STATE");
    TEST_ASSERT(strstr(buf, "\"screen\":") != NULL, "TCP: GET_STATE JSON contains screen field");
    TEST_ASSERT(strstr(buf, "\"play_state\":\"PLAYING\"") != NULL, "TCP: Waveform stream triggers PLAYING state");

    // Test GET_CURSOR command over TCP (single-line response)
    const char *cursor_cmd = "GET_CURSOR\n";
    send(sock, cursor_cmd, (int)strlen(cursor_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK CURSOR") != NULL, "TCP: GET_CURSOR -> OK CURSOR");

    // Test GET_TEXT_SCREEN command over TCP (multi-line response)
    const char *text_screen_cmd = "GET_TEXT_SCREEN\n";
    send(sock, text_screen_cmd, (int)strlen(text_screen_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK TEXT_SCREEN 30") != NULL, "TCP: GET_TEXT_SCREEN -> OK TEXT_SCREEN 30");

    // Sleep a tiny moment to drain socket before next test
    SDL_Delay(50);
    char drain[2048];
    while (recv(sock, drain, sizeof(drain), 0) > 0 && strlen(drain) > 0) {
      if (strlen(drain) < sizeof(drain)) break;
    }

    // Test LOGS command over TCP (multi-line response)
    const char *logs_cmd = "LOGS 2\n";
    send(sock, logs_cmd, (int)strlen(logs_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK LOGS") != NULL, "TCP: LOGS -> OK LOGS");

    // Check file on disk
    FILE *f_tcp = fopen("test_tcp_rec.wav", "rb");
    TEST_ASSERT(f_tcp != NULL, "TCP: Recorded WAV file exists on disk");
    if (f_tcp) {
      fseek(f_tcp, 0, SEEK_END);
      long s = ftell(f_tcp);
      TEST_ASSERT(s == 2048 + 44, "TCP: Recorded WAV file size is exactly 2092 bytes");
      fclose(f_tcp);
      remove("test_tcp_rec.wav");
    }
  }

  closesocket(sock);
  ai_server_shutdown();
}

static void test_logger(void) {
  printf("Running test_logger...\n");
  const char *log_test_fn = "logs/test_run.log";
  remove(log_test_fn);

  TEST_ASSERT(ai_logger_init(log_test_fn) == 1, "ai_logger_init succeeds");

  ai_log("TEST", "Test log message %d", 123);
  ai_log("PORT", "Serial test message");
  ai_log("KEY", "Keystroke test message");

  char recent[4096];
  int count = ai_logger_get_recent(recent, sizeof(recent), 10);
  TEST_ASSERT(count >= 3, "ai_logger_get_recent retrieved logged lines");
  TEST_ASSERT(strstr(recent, "Test log message 123") != NULL, "Ring buffer contains test log");
  TEST_ASSERT(strstr(recent, "Serial test message") != NULL, "Ring buffer contains serial test log");

  ai_logger_shutdown();

  FILE *f = fopen(log_test_fn, "r");
  TEST_ASSERT(f != NULL, "Log file exists on disk");
  if (f) {
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
      if (strstr(line, "Test log message 123") != NULL) {
        found = 1;
        break;
      }
    }
    TEST_ASSERT(found == 1, "Log file contains formatted output");
    fclose(f);
    remove(log_test_fn);
  }
}

static void feed_text_row(int row, int start_col, const char *str) {
  for (int i = 0; str[i] != '\0'; i++) {
    ai_screen_on_draw_char(str[i], (start_col + i) * 8, row * 8, 255, 255, 255, 0, 0, 0);
  }
}

static void test_virtual_screen_phrase(void) {
  printf("Running test_virtual_screen_phrase...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw header: "PHRASE 01"
  feed_text_row(0, 0, "PHRASE 01  BPM 130.0");

  // Draw step row 4: "00 C-4 FF 01 VOL 80 --- -- --- --"
  feed_text_row(4, 0, "00 C-4 FF 01 VOL 80 --- -- --- --");

  // Draw cursor highlight on "C-4" at (col 3, row 4, width 3 chars = 24px)
  ai_screen_on_draw_rect(3 * 8, 4 * 8, 24, 8, 0, 255, 255);

  char json[2048];
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"PHRASE\"") != NULL, "Screen identified as PHRASE");
  TEST_ASSERT(strstr(json, "\"input\":\"NOTE_00\"") != NULL, "Active input identified as NOTE_00");
  TEST_ASSERT(strstr(json, "\"value\":\"C-4\"") != NULL, "Current value is C-4");
  TEST_ASSERT(strstr(json, "\"cursor_col\":3") != NULL, "Cursor col is 3");
  TEST_ASSERT(strstr(json, "\"cursor_row\":4") != NULL, "Cursor row is 4");

  char grid[4096];
  ai_screen_get_text_grid(grid, sizeof(grid), 1);
  TEST_ASSERT(strstr(grid, "[C-4]") != NULL, "Text grid contains [C-4] marked cursor");

  // Move cursor to VOLUME (col 7, row 4, width 2 chars = 16px)
  ai_screen_on_draw_rect(7 * 8, 4 * 8, 16, 8, 0, 255, 255);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"VOLUME_00\"") != NULL, "Active input identified as VOLUME_00");
  TEST_ASSERT(strstr(json, "\"value\":\"FF\"") != NULL, "Current value is FF");

  // Move cursor to FX1 (col 13, row 4, width 3 chars = 24px)
  ai_screen_on_draw_rect(13 * 8, 4 * 8, 24, 8, 0, 255, 255);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"FX1_00\"") != NULL, "Active input identified as FX1_00");
  TEST_ASSERT(strstr(json, "\"value\":\"VOL\"") != NULL, "Current value is VOL");
}

static void test_virtual_screen_synth_left_label(void) {
  printf("Running test_virtual_screen_synth_left_label...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw header: "INST 00 [WAVSYN]"
  feed_text_row(0, 0, "INST 00 [WAVSYN]");

  // Draw parameter row: "  CUTOFF  80    RES  40"
  feed_text_row(4, 0, "  CUTOFF  80    RES  40");

  // Draw cursor on "80" at (col 10, row 4)
  ai_screen_on_draw_rect(10 * 8, 4 * 8, 16, 8, 255, 255, 0);

  char json[2048];
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"INSTRUMENT\"") != NULL, "Screen identified as INSTRUMENT");
  TEST_ASSERT(strstr(json, "\"input\":\"CUTOFF\"") != NULL, "Left-label heuristic resolved CUTOFF");
  TEST_ASSERT(strstr(json, "\"value\":\"80\"") != NULL, "Current value is 80");

  // Draw cursor on "40" at (col 21, row 4)
  ai_screen_on_draw_rect(21 * 8, 4 * 8, 16, 8, 255, 255, 0);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"RES\"") != NULL, "Left-label heuristic resolved RES");
  TEST_ASSERT(strstr(json, "\"value\":\"40\"") != NULL, "Current value is 40");
}

static void draw_corner_cursor(int col, int row, int width_chars) {
  int x = col * 8;
  int y = row * 8;
  int w = width_chars * 8;
  int h = 8;
  ai_screen_on_draw_rect(x, y, 2, 2, 0, 255, 255);
  ai_screen_on_draw_rect(x + w - 2, y, 2, 2, 0, 255, 255);
  ai_screen_on_draw_rect(x, y + h - 2, 2, 2, 0, 255, 255);
  ai_screen_on_draw_rect(x + w - 2, y + h - 2, 2, 2, 0, 255, 255);
}

static void test_virtual_screen_keyboard(void) {
  printf("Running test_virtual_screen_keyboard (character picker & name editing)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Name picker modal
  feed_text_row(0, 0, "NAME INSTRUMENT");
  feed_text_row(3, 2, "SYNTH_LEAD");
  feed_text_row(12, 4, "A B C D E F G H I J");
  feed_text_row(18, 4, "SPACE  OK  CANCEL");

  char json[2048];

  // 1. Test Name Buffer row
  draw_corner_cursor(2, 3, 10);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"KEYBOARD\"") != NULL, "Modal identified as KEYBOARD");
  TEST_ASSERT(strstr(json, "\"input\":\"NAME_BUFFER\"") != NULL, "Input is NAME_BUFFER");
  TEST_ASSERT(strstr(json, "\"value\":\"SYNTH_LEAD\"") != NULL, "Buffer value is SYNTH_LEAD");

  // 2. Test Letter in Character Picker Grid ('D' at col 10, row 12)
  draw_corner_cursor(10, 12, 1);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"PICKER_CHAR\"") != NULL, "Input is PICKER_CHAR");
  TEST_ASSERT(strstr(json, "\"value\":\"D\"") != NULL, "Focused character is D");

  // 3. Test SPACE button
  draw_corner_cursor(4, 18, 5);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"SPACE_BTN\"") != NULL, "Input is SPACE_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"SPACE\"") != NULL, "Button value is SPACE");

  // 4. Test OK button
  draw_corner_cursor(11, 18, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"OK_BTN\"") != NULL, "Input is OK_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"OK\"") != NULL, "Button value is OK");

  // 5. Test CANCEL button
  draw_corner_cursor(15, 18, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"CANCEL_BTN\"") != NULL, "Input is CANCEL_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"CANCEL\"") != NULL, "Button value is CANCEL");
}

static int table_step_to_row(int step) {
  if (step < 4) return 4 + step;
  if (step < 8) return 10 + (step - 4);
  if (step < 12) return 15 + (step - 8);
  return 20 + (step - 12);
}

static void test_virtual_screen_table(void) {
  printf("Running test_virtual_screen_table (iterating across all 16 rows and all fields)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw header: "TABLE 00"
  feed_text_row(0, 0, "TABLE 00");
  feed_text_row(2, 0, "  N   V  FX1   FX2   FX3");

  // Draw 16 rows: 00..0F on rows aligned to 4-step hardware blocks
  const char *fx_cmds[16] = {"VOL", "PAN", "PIT", "TIC", "HOP", "KIL", "CUT", "RES",
                             "FIL", "AMP", "DEL", "REV", "CHO", "MOD", "DEG", "OFF"};
  char row_texts[16][64];

  for (int step = 0; step < 16; step++) {
    int row = table_step_to_row(step);
    snprintf(row_texts[step], sizeof(row_texts[step]),
             "%02X +%02X %02X %s%02X %s%02X %s%02X",
             step, step, 0x80 + step,
             fx_cmds[step % 16], (step * 7) & 0xFF,
             fx_cmds[(step + 3) % 16], (step * 11) & 0xFF,
             fx_cmds[(step + 6) % 16], (step * 13) & 0xFF);
    feed_text_row(row, 0, row_texts[step]);
  }

  char json[2048];

  // Test 1: Header table number field
  ai_screen_on_draw_rect(6 * 8, 0, 16, 8, 0, 255, 255);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"TABLE\"") != NULL, "Screen identified as TABLE");
  TEST_ASSERT(strstr(json, "\"input\":\"TABLE_NUM\"") != NULL, "Header input identified as TABLE_NUM");
  TEST_ASSERT(strstr(json, "\"value\":\"00\"") != NULL, "Header value identified as 00");

  // Test 2: Iterate across all 16 steps (00 to 0F) and all columns
  int all_table_fields_passed = 1;

  for (int step = 0; step < 16; step++) {
    int row = table_step_to_row(step);
    char expected_step[4];
    snprintf(expected_step, sizeof(expected_step), "%02X", step);

    // 1. STEP column (col 0, width 2 chars)
    draw_corner_cursor(0, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    char exp_input[32];
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"STEP_%s\"", expected_step);
    char exp_val[32];
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", expected_step);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] STEP_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 2. NOTE column (col 3, width 3 chars)
    draw_corner_cursor(3, row, 3);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"NOTE_%s\"", expected_step);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"+%s\"", expected_step);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] NOTE_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 3. VOLUME column (col 7, width 2 chars)
    draw_corner_cursor(7, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"VOLUME_%s\"", expected_step);
    char exp_vol[8];
    snprintf(exp_vol, sizeof(exp_vol), "%02X", 0x80 + step);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", exp_vol);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] VOLUME_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 4. FX1 column (col 10, width 3 chars)
    draw_corner_cursor(10, row, 3);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX1_%s\"", expected_step);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", fx_cmds[step % 16]);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX1_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 5. FX1_VAL column (col 13, width 2 chars)
    draw_corner_cursor(13, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX1_VAL_%s\"", expected_step);
    char exp_fx1v[8];
    snprintf(exp_fx1v, sizeof(exp_fx1v), "%02X", (step * 7) & 0xFF);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", exp_fx1v);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX1_VAL_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 6. FX2 column (col 16, width 3 chars)
    draw_corner_cursor(16, row, 3);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX2_%s\"", expected_step);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", fx_cmds[(step + 3) % 16]);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX2_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 7. FX2_VAL column (col 19, width 2 chars)
    draw_corner_cursor(19, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX2_VAL_%s\"", expected_step);
    char exp_fx2v[8];
    snprintf(exp_fx2v, sizeof(exp_fx2v), "%02X", (step * 11) & 0xFF);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", exp_fx2v);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX2_VAL_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 8. FX3 column (col 22, width 3 chars)
    draw_corner_cursor(22, row, 3);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX3_%s\"", expected_step);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", fx_cmds[(step + 6) % 16]);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX3_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }

    // 9. FX3_VAL column (col 25, width 2 chars)
    draw_corner_cursor(25, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"FX3_VAL_%s\"", expected_step);
    char exp_fx3v[8];
    snprintf(exp_fx3v, sizeof(exp_fx3v), "%02X", (step * 13) & 0xFF);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", exp_fx3v);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] FX3_VAL_%s check failed! json=%s\n", expected_step, json);
      all_table_fields_passed = 0;
    }
  }

  TEST_ASSERT(all_table_fields_passed == 1, "All 16 steps x 9 fields (144 fields) on TABLE screen accurately resolved");
}

static void test_virtual_screen_inst_mods(void) {
  printf("Running test_virtual_screen_inst_mods (iterating across all 4 MOD slots and parameters)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw header
  feed_text_row(0, 0, "INST 00 MODS");

  // Draw MOD 1 (LFO)
  feed_text_row(3, 0, "MOD 1  LFO");
  feed_text_row(4, 0, "  DEST CUTOFF");
  feed_text_row(5, 0, "  AMT  80");
  feed_text_row(6, 0, "  SHP  TRI   FRQ 08");

  // Draw MOD 2 (AHD Envelope)
  feed_text_row(8, 0, "MOD 2  AHD");
  feed_text_row(9, 0, "  DEST PITCH");
  feed_text_row(10, 0, "  AMT  40");
  feed_text_row(11, 0, "  ATK  02   HLD 04   DEC 10");

  // Draw MOD 3 (LFO modulating MOD RATE)
  feed_text_row(13, 0, "MOD 3  LFO");
  feed_text_row(14, 0, "  DEST MOD RATE");
  feed_text_row(15, 0, "  AMT  20");
  feed_text_row(16, 0, "  SHP  SIN   FRQ 16T");

  // Draw MOD 4 (Tracking)
  feed_text_row(18, 0, "MOD 4  TRACK");
  feed_text_row(19, 0, "  DEST PAN");
  feed_text_row(20, 0, "  AMT  60");
  feed_text_row(21, 0, "  SRC  NOTE  LOW C-2  HIGH C-6");

  char json[2048];

  // 1. Header Instrument Number
  draw_corner_cursor(5, 0, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"INST_MODS\"") != NULL, "Screen identified as INST_MODS");
  TEST_ASSERT(strstr(json, "\"input\":\"INST_NUM\"") != NULL, "Header input identified as INST_NUM");
  TEST_ASSERT(strstr(json, "\"value\":\"00\"") != NULL, "Header value identified as 00");

  // 2. MOD 1 Slot & Parameters
  draw_corner_cursor(7, 3, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD1_TYPE\"") != NULL && strstr(json, "\"value\":\"LFO\"") != NULL, "MOD1_TYPE = LFO");

  draw_corner_cursor(7, 4, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD1_DEST\"") != NULL && strstr(json, "\"value\":\"CUTOFF\"") != NULL, "MOD1_DEST = CUTOFF");

  draw_corner_cursor(7, 5, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD1_AMT\"") != NULL && strstr(json, "\"value\":\"80\"") != NULL, "MOD1_AMT = 80");

  draw_corner_cursor(7, 6, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD1_SHP\"") != NULL && strstr(json, "\"value\":\"TRI\"") != NULL, "MOD1_SHP = TRI");

  draw_corner_cursor(17, 6, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD1_FRQ\"") != NULL && strstr(json, "\"value\":\"08\"") != NULL, "MOD1_FRQ = 08");

  // 3. MOD 2 Slot & Parameters
  draw_corner_cursor(7, 8, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_TYPE\"") != NULL && strstr(json, "\"value\":\"AHD\"") != NULL, "MOD2_TYPE = AHD");

  draw_corner_cursor(7, 9, 5);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_DEST\"") != NULL && strstr(json, "\"value\":\"PITCH\"") != NULL, "MOD2_DEST = PITCH");

  draw_corner_cursor(7, 10, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_AMT\"") != NULL && strstr(json, "\"value\":\"40\"") != NULL, "MOD2_AMT = 40");

  draw_corner_cursor(7, 11, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_ATK\"") != NULL && strstr(json, "\"value\":\"02\"") != NULL, "MOD2_ATK = 02");

  draw_corner_cursor(17, 11, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_HLD\"") != NULL && strstr(json, "\"value\":\"04\"") != NULL, "MOD2_HLD = 04");

  draw_corner_cursor(26, 11, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD2_DEC\"") != NULL && strstr(json, "\"value\":\"10\"") != NULL, "MOD2_DEC = 10");

  // 4. MOD 3 Slot & Parameters (with multi-word DEST 'MOD RATE')
  draw_corner_cursor(7, 13, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD3_TYPE\"") != NULL && strstr(json, "\"value\":\"LFO\"") != NULL, "MOD3_TYPE = LFO");

  draw_corner_cursor(7, 14, 8);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD3_DEST\"") != NULL && strstr(json, "\"value\":\"MOD RATE\"") != NULL, "MOD3_DEST = MOD RATE");

  draw_corner_cursor(7, 15, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD3_AMT\"") != NULL && strstr(json, "\"value\":\"20\"") != NULL, "MOD3_AMT = 20");

  draw_corner_cursor(7, 16, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD3_SHP\"") != NULL && strstr(json, "\"value\":\"SIN\"") != NULL, "MOD3_SHP = SIN");

  draw_corner_cursor(17, 16, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD3_FRQ\"") != NULL && strstr(json, "\"value\":\"16T\"") != NULL, "MOD3_FRQ = 16T");

  // 5. MOD 4 Slot & Parameters
  draw_corner_cursor(7, 18, 5);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_TYPE\"") != NULL && strstr(json, "\"value\":\"TRACK\"") != NULL, "MOD4_TYPE = TRACK");

  draw_corner_cursor(7, 19, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_DEST\"") != NULL && strstr(json, "\"value\":\"PAN\"") != NULL, "MOD4_DEST = PAN");

  draw_corner_cursor(7, 20, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_AMT\"") != NULL && strstr(json, "\"value\":\"60\"") != NULL, "MOD4_AMT = 60");

  draw_corner_cursor(7, 21, 4);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_SRC\"") != NULL && strstr(json, "\"value\":\"NOTE\"") != NULL, "MOD4_SRC = NOTE");

  draw_corner_cursor(17, 21, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_LOW\"") != NULL && strstr(json, "\"value\":\"C-2\"") != NULL, "MOD4_LOW = C-2");

  draw_corner_cursor(26, 21, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"MOD4_HIGH\"") != NULL && strstr(json, "\"value\":\"C-6\"") != NULL, "MOD4_HIGH = C-6");
}

static void test_virtual_screen_groove(void) {
  printf("Running test_virtual_screen_groove (iterating across all 16 rows and fields)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw header: "GROOVE 00"
  feed_text_row(0, 0, "GROOVE 00");
  feed_text_row(2, 0, "  TICKS");

  // Draw 16 rows: 00..0F on rows aligned to 4-step hardware blocks
  const uint8_t sample_ticks[16] = {6, 7, 5, 6, 8, 4, 6, 6, 12, 12, 6, 6, 0, 0, 0, 0};
  char row_texts[16][32];

  for (int step = 0; step < 16; step++) {
    int row = table_step_to_row(step);
    snprintf(row_texts[step], sizeof(row_texts[step]), "%02X %02X", step, sample_ticks[step]);
    feed_text_row(row, 0, row_texts[step]);
  }

  char json[2048];

  // Test 1: Header Groove Number field
  draw_corner_cursor(7, 0, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"GROOVE\"") != NULL, "Screen identified as GROOVE");
  TEST_ASSERT(strstr(json, "\"input\":\"GROOVE_NUM\"") != NULL, "Header input identified as GROOVE_NUM");
  TEST_ASSERT(strstr(json, "\"value\":\"00\"") != NULL, "Header value identified as 00");

  // Test 2: Iterate across all 16 steps (00 to 0F) and both columns
  int all_groove_fields_passed = 1;

  for (int step = 0; step < 16; step++) {
    int row = table_step_to_row(step);
    char expected_step[4];
    snprintf(expected_step, sizeof(expected_step), "%02X", step);

    // 1. STEP column (col 0, width 2 chars)
    draw_corner_cursor(0, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    char exp_input[32];
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"STEP_%s\"", expected_step);
    char exp_val[32];
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", expected_step);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] GROOVE STEP_%s check failed! json=%s\n", expected_step, json);
      all_groove_fields_passed = 0;
    }

    // 2. TICKS column (col 3, width 2 chars)
    draw_corner_cursor(3, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"TICKS_%s\"", expected_step);
    char exp_ticks[8];
    snprintf(exp_ticks, sizeof(exp_ticks), "%02X", sample_ticks[step]);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", exp_ticks);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] GROOVE TICKS_%s check failed! json=%s\n", expected_step, json);
      all_groove_fields_passed = 0;
    }
  }

  TEST_ASSERT(all_groove_fields_passed == 1, "All 16 steps x 2 fields (32 fields) on GROOVE screen accurately resolved");
}

static void test_virtual_screen_scale(void) {
  printf("Running test_virtual_screen_scale (iterating across all 12 note intervals and header fields)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Header: "SCALE 00  KEY C" and "NAME MAJOR"
  feed_text_row(0, 0, "SCALE 00  KEY C");
  feed_text_row(1, 0, "NAME MAJOR");
  feed_text_row(2, 0, "NOTE EN  OFFSET");

  // 12 Semitones: C through B
  const char *note_names[12] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
  const char *en_states[12] = {"ON ", "---", "ON ", "---", "ON ", "ON ", "---", "ON ", "---", "ON ", "---", "ON "};
  const char *offsets[12] = {"+00.00", "+00.00", "+00.00", "+00.00", "+00.00", "+00.00",
                             "+00.00", "+00.00", "+00.00", "+00.00", "+00.00", "+00.00"};
  char row_texts[12][32];

  for (int interval = 0; interval < 12; interval++) {
    int row = 3 + interval;
    snprintf(row_texts[interval], sizeof(row_texts[interval]), "%s  %s %s",
             note_names[interval], en_states[interval], offsets[interval]);
    feed_text_row(row, 0, row_texts[interval]);
  }

  char json[2048];

  // Test 1: Header Fields (SCALE_NUM, KEY, NAME)
  draw_corner_cursor(6, 0, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"SCALE\"") != NULL, "Screen identified as SCALE");
  TEST_ASSERT(strstr(json, "\"input\":\"SCALE_NUM\"") != NULL, "Header input identified as SCALE_NUM");
  TEST_ASSERT(strstr(json, "\"value\":\"00\"") != NULL, "Header value identified as 00");

  draw_corner_cursor(14, 0, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"KEY\"") != NULL, "Header input identified as KEY");
  TEST_ASSERT(strstr(json, "\"value\":\"C\"") != NULL, "Header key value identified as C");

  draw_corner_cursor(5, 1, 5);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"NAME\"") != NULL, "Header input identified as NAME");
  TEST_ASSERT(strstr(json, "\"value\":\"MAJOR\"") != NULL, "Header name value identified as MAJOR");

  // Test 2: Iterate across all 12 note intervals (00 to 0B)
  int all_scale_fields_passed = 1;

  for (int interval = 0; interval < 12; interval++) {
    int row = 3 + interval;
    char expected_int[4];
    snprintf(expected_int, sizeof(expected_int), "%02X", interval);

    char clean_note[4] = {0};
    snprintf(clean_note, sizeof(clean_note), "%s", note_names[interval]);
    if (clean_note[1] == ' ') clean_note[1] = '\0';

    char clean_en[4] = {0};
    snprintf(clean_en, sizeof(clean_en), "%s", en_states[interval]);
    if (clean_en[2] == ' ') clean_en[2] = '\0';

    // 1. NOTE column (col 0..2)
    draw_corner_cursor(0, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    char exp_input[32];
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"NOTE_%s\"", expected_int);
    char exp_val[32];
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", clean_note);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] SCALE NOTE_%s check failed! json=%s\n", expected_int, json);
      all_scale_fields_passed = 0;
    }

    // 2. ENABLE column (col 4..6)
    draw_corner_cursor(4, row, 3);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"ENABLE_%s\"", expected_int);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", clean_en);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] SCALE ENABLE_%s check failed! json=%s\n", expected_int, json);
      all_scale_fields_passed = 0;
    }

    // 3. OFFSET column (col 8..14)
    draw_corner_cursor(8, row, 6);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"OFFSET_%s\"", expected_int);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", offsets[interval]);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] SCALE OFFSET_%s check failed! json=%s\n", expected_int, json);
      all_scale_fields_passed = 0;
    }
  }

  TEST_ASSERT(all_scale_fields_passed == 1, "All 12 intervals x 3 fields (36 fields) on SCALE screen accurately resolved");
}

static void test_virtual_screen_inst_pool(void) {
  printf("Running test_virtual_screen_inst_pool (iterating across 16 instrument slots and fields)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Header: "INSTRUMENT POOL" and column labels
  feed_text_row(0, 0, "INSTRUMENT POOL");
  feed_text_row(2, 0, "NO  TYPE     NAME");

  const char *inst_types[16] = {
      "WAVSYN ", "MACRO  ", "SAMPLER", "FMSYN  ", "MIDI   ", "HYPER  ", "WAVSYN ", "SAMPLER",
      "MACRO  ", "FMSYN  ", "SAMPLER", "MIDI   ", "WAVSYN ", "HYPER  ", "SAMPLER", "MACRO  "};
  const char *inst_names[16] = {
      "KICK 01 ", "ACID BS ", "SNARE909", "EPRHODES", "EXTSYNTH", "HYPERPAD", "SUB BASS", "HIHAT CL",
      "CHORD 01", "FM BELLS", "VOX CHNT", "MIDI OUT", "LEAD SAW", "WARM PAD", "PERC 01 ", "PLUCK 01"};
  char row_texts[16][40];

  for (int slot = 0; slot < 16; slot++) {
    int row = 3 + slot;
    snprintf(row_texts[slot], sizeof(row_texts[slot]), "%02X  %s  %s",
             slot, inst_types[slot], inst_names[slot]);
    feed_text_row(row, 0, row_texts[slot]);
  }

  char json[2048];

  // Test: Iterate across 16 slots and all 3 columns (INST, TYPE, NAME)
  int all_pool_fields_passed = 1;

  for (int slot = 0; slot < 16; slot++) {
    int row = 3 + slot;
    char expected_slot[4];
    snprintf(expected_slot, sizeof(expected_slot), "%02X", slot);

    char clean_type[16] = {0};
    snprintf(clean_type, sizeof(clean_type), "%s", inst_types[slot]);
    int t_len = (int)strlen(clean_type);
    while (t_len > 0 && clean_type[t_len - 1] == ' ') clean_type[--t_len] = '\0';

    char clean_name[16] = {0};
    snprintf(clean_name, sizeof(clean_name), "%s", inst_names[slot]);
    int n_len = (int)strlen(clean_name);
    while (n_len > 0 && clean_name[n_len - 1] == ' ') clean_name[--n_len] = '\0';

    // 1. INST Slot column (col 0..2)
    draw_corner_cursor(0, row, 2);
    ai_screen_get_state_json(json, sizeof(json));
    TEST_ASSERT(strstr(json, "\"screen\":\"INST_POOL\"") != NULL, "Screen identified as INST_POOL");
    char exp_input[32];
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"INST_%s\"", expected_slot);
    char exp_val[32];
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", expected_slot);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] INST_POOL INST_%s check failed! json=%s\n", expected_slot, json);
      all_pool_fields_passed = 0;
    }

    // 2. TYPE column (col 4..11)
    draw_corner_cursor(4, row, 7);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"TYPE_%s\"", expected_slot);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", clean_type);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] INST_POOL TYPE_%s check failed! json=%s\n", expected_slot, json);
      all_pool_fields_passed = 0;
    }

    // 3. NAME column (col 13..21)
    draw_corner_cursor(13, row, 8);
    ai_screen_get_state_json(json, sizeof(json));
    snprintf(exp_input, sizeof(exp_input), "\"input\":\"NAME_%s\"", expected_slot);
    snprintf(exp_val, sizeof(exp_val), "\"value\":\"%s\"", clean_name);
    if (!strstr(json, exp_input) || !strstr(json, exp_val)) {
      printf("  [FAIL] INST_POOL NAME_%s check failed! json=%s\n", expected_slot, json);
      all_pool_fields_passed = 0;
    }
  }

  TEST_ASSERT(all_pool_fields_passed == 1, "All 16 slots x 3 fields (48 fields) on INST_POOL screen accurately resolved");
}

static void test_virtual_screen_file_browser(void) {
  printf("Running test_virtual_screen_file_browser (directory traversal & file picker)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Header: "DIRECTORY: /SONGS/DEMOS"
  feed_text_row(0, 0, "DIRECTORY: /SONGS/DEMOS");
  feed_text_row(2, 0, "  /..");
  feed_text_row(3, 0, "  /PRESETS");
  feed_text_row(4, 0, "  /TEMPLATES");
  feed_text_row(5, 0, "  BEAT_01.M8S");
  feed_text_row(6, 0, "  SYNTH_LEAD.M8I");
  feed_text_row(7, 0, "  808_KICK.WAV");
  feed_text_row(8, 0, "  README.TXT");
  feed_text_row(10, 0, "  LOAD    CANCEL");

  char json[2048];

  // 1. Test Header Path
  draw_corner_cursor(11, 0, 12);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"FILE_BROWSER\"") != NULL, "Screen identified as FILE_BROWSER");
  TEST_ASSERT(strstr(json, "\"input\":\"CURRENT_PATH\"") != NULL, "Header path input is CURRENT_PATH");
  TEST_ASSERT(strstr(json, "\"value\":\"/SONGS/DEMOS\"") != NULL, "Header path value is /SONGS/DEMOS");

  // 2. Test Parent Directory /..
  draw_corner_cursor(2, 2, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"PARENT_DIR\"") != NULL, "Input is PARENT_DIR");
  TEST_ASSERT(strstr(json, "\"value\":\"/..\"") != NULL, "Value is /..");

  // 3. Test Subdirectory /PRESETS
  draw_corner_cursor(2, 3, 8);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"DIRECTORY_ITEM\"") != NULL, "Input is DIRECTORY_ITEM");
  TEST_ASSERT(strstr(json, "\"value\":\"/PRESETS\"") != NULL, "Value is /PRESETS");

  // 4. Test Subdirectory /TEMPLATES
  draw_corner_cursor(2, 4, 10);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"DIRECTORY_ITEM\"") != NULL, "Input is DIRECTORY_ITEM");
  TEST_ASSERT(strstr(json, "\"value\":\"/TEMPLATES\"") != NULL, "Value is /TEMPLATES");

  // 5. Test Song File BEAT_01.M8S
  draw_corner_cursor(2, 5, 11);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"SONG_FILE\"") != NULL, "Input is SONG_FILE");
  TEST_ASSERT(strstr(json, "\"value\":\"BEAT_01.M8S\"") != NULL, "Value is BEAT_01.M8S");

  // 6. Test Instrument File SYNTH_LEAD.M8I
  draw_corner_cursor(2, 6, 14);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"INSTRUMENT_FILE\"") != NULL, "Input is INSTRUMENT_FILE");
  TEST_ASSERT(strstr(json, "\"value\":\"SYNTH_LEAD.M8I\"") != NULL, "Value is SYNTH_LEAD.M8I");

  // 7. Test Sample File 808_KICK.WAV
  draw_corner_cursor(2, 7, 12);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"SAMPLE_FILE\"") != NULL, "Input is SAMPLE_FILE");
  TEST_ASSERT(strstr(json, "\"value\":\"808_KICK.WAV\"") != NULL, "Value is 808_KICK.WAV");

  // 8. Test Generic File README.TXT
  draw_corner_cursor(2, 8, 10);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"FILE_ITEM\"") != NULL, "Input is FILE_ITEM");
  TEST_ASSERT(strstr(json, "\"value\":\"README.TXT\"") != NULL, "Value is README.TXT");

  // 9. Test Action Buttons: LOAD and CANCEL
  draw_corner_cursor(2, 10, 4);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"LOAD_BTN\"") != NULL, "Button is LOAD_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"LOAD\"") != NULL, "Value is LOAD");

  draw_corner_cursor(10, 10, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"CANCEL_BTN\"") != NULL, "Button is CANCEL_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"CANCEL\"") != NULL, "Value is CANCEL");

  // 10. Test Sample Browser Context (SAMPLER > SAMPLE: "SELECT SAMPLE" / "DIRECTORY: /SAMPLES/DRUMS")
  ai_screen_reset();
  feed_text_row(0, 0, "SELECT SAMPLE");
  feed_text_row(1, 0, "DIRECTORY: /SAMPLES/DRUMS");
  feed_text_row(2, 0, "  /..");
  feed_text_row(3, 0, "  SNARE_909.WAV");

  draw_corner_cursor(2, 3, 13);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"FILE_BROWSER\"") != NULL, "Sample picker identified as FILE_BROWSER");
  TEST_ASSERT(strstr(json, "\"input\":\"SAMPLE_FILE\"") != NULL, "Input is SAMPLE_FILE");
  TEST_ASSERT(strstr(json, "\"value\":\"SNARE_909.WAV\"") != NULL, "Value is SNARE_909.WAV");

  // 11. Test Instrument Preset Browser Context (INSTRUMENT > LOAD: "LOAD INSTRUMENT")
  ai_screen_reset();
  feed_text_row(0, 0, "LOAD INSTRUMENT");
  feed_text_row(1, 0, "DIRECTORY: /PRESETS/SYNTHS");
  feed_text_row(2, 0, "  /..");
  feed_text_row(3, 0, "  ACID_BASS.M8I");

  draw_corner_cursor(2, 3, 13);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"FILE_BROWSER\"") != NULL, "Instrument preset loader identified as FILE_BROWSER");
  TEST_ASSERT(strstr(json, "\"input\":\"INSTRUMENT_FILE\"") != NULL, "Input is INSTRUMENT_FILE");
  TEST_ASSERT(strstr(json, "\"value\":\"ACID_BASS.M8I\"") != NULL, "Value is ACID_BASS.M8I");

  // 12. Test Empty Gap Row Snapping (as seen in M8 file browser when cursor lands on row 8/9 with item on row 7/8)
  ai_screen_reset();
  feed_text_row(0, 0, "LOAD PROJECT");
  feed_text_row(5, 0, "/..");
  feed_text_row(6, 0, "/INSTRUMENTS");
  feed_text_row(7, 0, "/SAMPLES");
  // Row 8 is blank gap row
  feed_text_row(9, 0, "DEMO2.M8S");

  // Cursor placed at col 0, row 8 (the empty gap row), width 9
  draw_corner_cursor(0, 8, 9);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"FILE_BROWSER\"") != NULL, "Screen is FILE_BROWSER");
  TEST_ASSERT(strstr(json, "\"input\":\"DIRECTORY_ITEM\"") != NULL, "Empty gap row snaps to DIRECTORY_ITEM");
  TEST_ASSERT(strstr(json, "\"value\":\"/SAMPLES\"") != NULL, "Empty gap row resolves value to /SAMPLES");
}

static void test_virtual_screen_confirm_dialog(void) {
  printf("Running test_virtual_screen_confirm_dialog (modal confirmation & alert prompts)...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Prompt: "LOSE CHANGES TO CURRENT SONG?" on row 12, "  OK  CANCEL" on row 15
  feed_text_row(12, 0, "LOSE CHANGES TO CURRENT SONG?");
  feed_text_row(15, 0, "  OK  CANCEL");

  char json[2048];

  // 1. Test OK button (col 2, row 15)
  draw_corner_cursor(2, 15, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"CONFIRM_DIALOG\"") != NULL, "Screen identified as CONFIRM_DIALOG");
  TEST_ASSERT(strstr(json, "\"header\":\"LOSE CHANGES TO CURRENT SONG?\"") != NULL, "Header extracted prompt message");
  TEST_ASSERT(strstr(json, "\"input\":\"OK_BTN\"") != NULL, "Input is OK_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"OK\"") != NULL, "Value is OK");

  // 2. Test CANCEL button (col 6, row 15)
  draw_corner_cursor(6, 15, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"CANCEL_BTN\"") != NULL, "Input is CANCEL_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"CANCEL\"") != NULL, "Value is CANCEL");

  // 3. Test Overwrite Prompt with YES / NO buttons
  ai_screen_reset();
  feed_text_row(10, 0, "OVERWRITE EXISTING FILE?");
  feed_text_row(14, 0, "  YES  NO");

  draw_corner_cursor(2, 14, 3);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"CONFIRM_DIALOG\"") != NULL, "Overwrite prompt identified as CONFIRM_DIALOG");
  TEST_ASSERT(strstr(json, "\"input\":\"YES_BTN\"") != NULL, "Input is YES_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"YES\"") != NULL, "Value is YES");

  draw_corner_cursor(7, 14, 2);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"NO_BTN\"") != NULL, "Input is NO_BTN");
  TEST_ASSERT(strstr(json, "\"value\":\"NO\"") != NULL, "Value is NO");
}

static void test_virtual_screen_project(void) {
  printf("Running test_virtual_screen_project (Project screen fields & full-word token focus)...\n");
  ai_screen_init();
  ai_screen_reset();

  feed_text_row(0, 0, "PROJECT");
  feed_text_row(7, 0, "TEMPO        140.00 <>");
  feed_text_row(16, 0, "NAME         DEMO02-------");
  feed_text_row(17, 0, "PROJECT      LOAD SAVE NEW");
  feed_text_row(18, 0, "EXPORT/SHARE RENDER BUNDLE");
  feed_text_row(20, 0, "CLEAR UNUSED PHRASES INST/TBL");
  feed_text_row(21, 0, "INST. POOL   VIEW INST.POOL");

  char json[2048];

  // 1. Test RENDER (col 13, row 18, width 6)
  draw_corner_cursor(13, 18, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"PROJECT\"") != NULL, "Screen is PROJECT (not KEYBOARD)");
  TEST_ASSERT(strstr(json, "\"input\":\"EXPORT_RENDER\"") != NULL, "Input is EXPORT_RENDER");
  TEST_ASSERT(strstr(json, "\"value\":\"RENDER\"") != NULL, "Value is full word RENDER");
  TEST_ASSERT(strstr(json, "\"cursor_width\":6") != NULL, "Cursor width is 6 for full word");

  // 2. Test BUNDLE (col 20, row 18, width 6)
  draw_corner_cursor(20, 18, 6);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"EXPORT_BUNDLE\"") != NULL, "Input is EXPORT_BUNDLE");
  TEST_ASSERT(strstr(json, "\"value\":\"BUNDLE\"") != NULL, "Value is BUNDLE");

  // 3. Test LOAD (col 13, row 17, width 4)
  draw_corner_cursor(13, 17, 4);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"PROJECT_LOAD\"") != NULL, "Input is PROJECT_LOAD");
  TEST_ASSERT(strstr(json, "\"value\":\"LOAD\"") != NULL, "Value is LOAD");

  // 4. Test CLEAR UNUSED PHRASES (col 13, row 20, width 7)
  draw_corner_cursor(13, 20, 7);
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"input\":\"CLEAR_PHRASES\"") != NULL, "Input is CLEAR_PHRASES");
  TEST_ASSERT(strstr(json, "\"value\":\"PHRASES\"") != NULL, "Value is PHRASES");
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  SDL_Init(SDL_INIT_EVENTS);

  printf("=== M8C AI Server Unit & Integration Tests ===\n");
  test_single_keys();
  test_key_combinations();
  test_invalid_keys();
  test_logger();
  test_virtual_screen_phrase();
  test_virtual_screen_table();
  test_virtual_screen_inst_mods();
  test_virtual_screen_groove();
  test_virtual_screen_scale();
  test_virtual_screen_inst_pool();
  test_virtual_screen_project();
  test_virtual_screen_file_browser();
  test_virtual_screen_confirm_dialog();
  test_virtual_screen_synth_left_label();
  test_virtual_screen_keyboard();
  test_audio_recording();
  test_live_tcp_server();

  SDL_Quit();

  printf("\nTest Summary: %d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
