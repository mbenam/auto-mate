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
  printf("Running test_audio_recording...\n");
  const char *test_fn = "test_rec_direct.wav";
  remove(test_fn);

  TEST_ASSERT(ai_server_rec_start(test_fn) == 1, "ai_server_rec_start succeeds");
  TEST_ASSERT(ai_is_recording == true, "ai_is_recording flag is true");
  TEST_ASSERT(ai_wav_file != NULL, "ai_wav_file handle is open");

  // Push 1024 bytes of dummy audio data
  uint8_t dummy_audio[1024];
  memset(dummy_audio, 0x55, sizeof(dummy_audio));
  ai_server_record_audio(dummy_audio, sizeof(dummy_audio));
  TEST_ASSERT(wav_data_size == sizeof(dummy_audio), "wav_data_size updated to 1024");

  uint32_t final_size = 0;
  TEST_ASSERT(ai_server_rec_stop(&final_size) == 1, "ai_server_rec_stop succeeds");
  TEST_ASSERT(final_size == 1024, "final_size reported as 1024");
  TEST_ASSERT(ai_is_recording == false, "ai_is_recording flag reset to false");
  TEST_ASSERT(ai_wav_file == NULL, "ai_wav_file closed");

  // Verify file on disk
  FILE *f = fopen(test_fn, "rb");
  TEST_ASSERT(f != NULL, "WAV file was created on disk");
  if (f) {
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    TEST_ASSERT(fsize == 1024 + 44, "WAV total file size is exactly 1068 (1024 data + 44 header)");

    fseek(f, 0, SEEK_SET);
    char hdr[44];
    fread(hdr, 1, 44, f);
    TEST_ASSERT(memcmp(hdr, "RIFF", 4) == 0, "WAV header starts with 'RIFF'");
    TEST_ASSERT(memcmp(hdr + 8, "WAVE", 4) == 0, "WAV format is 'WAVE'");
    TEST_ASSERT(memcmp(hdr + 12, "fmt ", 4) == 0, "WAV contains 'fmt ' chunk");
    TEST_ASSERT(memcmp(hdr + 36, "data", 4) == 0, "WAV contains 'data' chunk");
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
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK KEY 0x18") != NULL, "TCP: KEY SHIFT+PLAY -> OK KEY 0x18");

    // Test KEY INVALID -> ERROR
    const char *bad_cmd = "KEY BADKEY\n";
    send(sock, bad_cmd, (int)strlen(bad_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "ERROR") != NULL, "TCP: KEY BADKEY -> ERROR");

    // Test SCREENSHOT -> 230,400 bytes
    const char *screenshot_cmd = "SCREENSHOT\n";
    send(sock, screenshot_cmd, (int)strlen(screenshot_cmd), 0);

    SDL_Event ev;
    int event_handled = 0;
    for (int i = 0; i < 50; i++) {
      if (SDL_PollEvent(&ev)) {
        if (ev.type == AI_SCREENSHOT_EVENT) {
          ai_server_handle_screenshot(NULL);
          event_handled = 1;
          break;
        }
      }
      SDL_Delay(10);
    }
    TEST_ASSERT(event_handled == 1, "SDL event loop caught AI_SCREENSHOT_EVENT");

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

    // Test GET_STATE command over TCP (single-line JSON response)
    const char *state_cmd = "GET_STATE\n";
    send(sock, state_cmd, (int)strlen(state_cmd), 0);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT(strstr(buf, "OK STATE") != NULL, "TCP: GET_STATE -> OK STATE");
    TEST_ASSERT(strstr(buf, "\"screen\":") != NULL, "TCP: GET_STATE JSON contains screen field");

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

static void test_virtual_screen_keyboard(void) {
  printf("Running test_virtual_screen_keyboard...\n");
  ai_screen_init();
  ai_screen_reset();

  // Draw Name picker modal
  feed_text_row(0, 0, "NAME INSTRUMENT");
  feed_text_row(3, 2, "SYNTH_LEAD");
  feed_text_row(12, 4, "A B C D E F G H I J");
  feed_text_row(18, 4, "SPACE  OK  CANCEL");

  // Cursor on "OK" at (col 11, row 18)
  ai_screen_on_draw_rect(11 * 8, 18 * 8, 16, 8, 0, 255, 0);

  char json[2048];
  ai_screen_get_state_json(json, sizeof(json));
  TEST_ASSERT(strstr(json, "\"screen\":\"KEYBOARD\"") != NULL, "Modal identified as KEYBOARD");
  TEST_ASSERT(strstr(json, "\"value\":\"OK\"") != NULL, "Current value is OK");
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
  test_virtual_screen_synth_left_label();
  test_virtual_screen_keyboard();
  test_audio_recording();
  test_live_tcp_server();

  SDL_Quit();

  printf("\nTest Summary: %d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
