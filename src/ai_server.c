// ai_server.c - AI Agent Local TCP Server for m8c
#include "ai_server.h"
#include "ai_logger.h"
#include "ai_screen.h"
#include "input.h"
#include "render.h"
#include "backends/m8.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Uint32 AI_INPUT_EVENT = 0;
Uint32 AI_SCREENSHOT_EVENT = 0;
void *shared_pixel_buffer = NULL;

// Feature D: Audio Recording to WAV
bool ai_is_recording = false;
FILE *ai_wav_file = NULL;
uint32_t wav_data_size = 0;
static SDL_Mutex *audio_rec_mutex = NULL;

#ifdef _WIN32
static HANDLE screenshot_ready_event = NULL;
#else
static SDL_Semaphore *screenshot_ready_event = NULL;
#endif
static SDL_Mutex *screenshot_mutex = NULL;

static SDL_Thread *server_thread = NULL;
static volatile int server_running = 0;
static SOCKET listen_sock = INVALID_SOCKET;

#pragma pack(push, 1)
typedef struct {
  char riff_header[4];       // "RIFF"
  uint32_t wav_size;         // Total file size - 8 = 36 + wav_data_size
  char wave_header[4];       // "WAVE"
  char fmt_header[4];        // "fmt "
  uint32_t fmt_chunk_size;   // 16 for PCM
  uint16_t audio_format;     // 1 for PCM
  uint16_t num_channels;     // 2 (Stereo)
  uint32_t sample_rate;      // 44100
  uint32_t byte_rate;        // sample_rate * num_channels * bits_per_sample / 8 = 176400
  uint16_t sample_alignment; // num_channels * bits_per_sample / 8 = 4
  uint16_t bit_depth;        // 16
  char data_header[4];       // "data"
  uint32_t data_bytes;       // wav_data_size
} wav_header_s;
#pragma pack(pop)

static void write_wav_header(FILE *f, uint32_t data_len) {
  wav_header_s hdr;
  memcpy(hdr.riff_header, "RIFF", 4);
  hdr.wav_size = 36 + data_len;
  memcpy(hdr.wave_header, "WAVE", 4);
  memcpy(hdr.fmt_header, "fmt ", 4);
  hdr.fmt_chunk_size = 16;
  hdr.audio_format = 1; // PCM
  hdr.num_channels = 2; // Stereo
  hdr.sample_rate = 44100;
  hdr.byte_rate = 44100 * 2 * 2;
  hdr.sample_alignment = 2 * 2;
  hdr.bit_depth = 16;
  memcpy(hdr.data_header, "data", 4);
  hdr.data_bytes = data_len;

  fwrite(&hdr, 1, sizeof(hdr), f);
}

int ai_server_rec_start(const char *filename) {
  if (!filename || strlen(filename) == 0) {
    filename = "m8c_record.wav";
  }

  if (audio_rec_mutex) SDL_LockMutex(audio_rec_mutex);

  // If already recording, close active file first
  if (ai_is_recording && ai_wav_file) {
    fseek(ai_wav_file, 0, SEEK_SET);
    write_wav_header(ai_wav_file, wav_data_size);
    fclose(ai_wav_file);
    ai_wav_file = NULL;
    ai_is_recording = false;
  }

  ai_wav_file = fopen(filename, "wb+");
  if (!ai_wav_file) {
    ai_log("AUDIO", "Failed to open WAV file '%s' for recording", filename);
    if (audio_rec_mutex) SDL_UnlockMutex(audio_rec_mutex);
    return 0;
  }

  wav_data_size = 0;
  write_wav_header(ai_wav_file, 0); // Write dummy 44-byte WAV header
  ai_is_recording = true;

  if (audio_rec_mutex) SDL_UnlockMutex(audio_rec_mutex);
  ai_log("AUDIO", "Started audio recording to '%s'", filename);
  return 1;
}

int ai_server_rec_stop(uint32_t *out_size) {
  if (audio_rec_mutex) SDL_LockMutex(audio_rec_mutex);

  if (!ai_is_recording || !ai_wav_file) {
    if (audio_rec_mutex) SDL_UnlockMutex(audio_rec_mutex);
    return 0;
  }

  ai_is_recording = false;
  fseek(ai_wav_file, 0, SEEK_SET);
  write_wav_header(ai_wav_file, wav_data_size);
  fclose(ai_wav_file);
  ai_wav_file = NULL;

  uint32_t final_size = wav_data_size;
  if (out_size) {
    *out_size = final_size;
  }
  wav_data_size = 0;

  if (audio_rec_mutex) SDL_UnlockMutex(audio_rec_mutex);
  ai_log("AUDIO", "Stopped audio recording (total %u bytes)", final_size);
  return 1;
}

void ai_server_record_audio(const void *audio_buffer, size_t byte_count) {
  if (ai_is_recording && ai_wav_file != NULL && audio_buffer && byte_count > 0) {
    if (audio_rec_mutex) SDL_LockMutex(audio_rec_mutex);
    if (ai_is_recording && ai_wav_file != NULL) {
      fwrite(audio_buffer, 1, byte_count, ai_wav_file);
      wav_data_size += (uint32_t)byte_count;
    }
    if (audio_rec_mutex) SDL_UnlockMutex(audio_rec_mutex);
  }
}

static void str_trim_and_upper(char *str) {
  // Trim leading whitespace
  char *start = str;
  while (isspace((unsigned char)*start)) {
    start++;
  }
  if (start != str) {
    memmove(str, start, strlen(start) + 1);
  }

  // Trim trailing whitespace
  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) {
    str[--len] = '\0';
  }

  // Uppercase
  for (size_t i = 0; i < len; i++) {
    str[i] = (char)toupper((unsigned char)str[i]);
  }
}

static int parse_single_key(const char *name, uint8_t *mask) {
  if (strcmp(name, "UP") == 0) {
    *mask |= key_up;
    return 1;
  }
  if (strcmp(name, "DOWN") == 0) {
    *mask |= key_down;
    return 1;
  }
  if (strcmp(name, "LEFT") == 0) {
    *mask |= key_left;
    return 1;
  }
  if (strcmp(name, "RIGHT") == 0) {
    *mask |= key_right;
    return 1;
  }
  if (strcmp(name, "EDIT") == 0) {
    *mask |= key_edit;
    return 1;
  }
  if (strcmp(name, "OPT") == 0 || strcmp(name, "OPTION") == 0) {
    *mask |= key_opt;
    return 1;
  }
  if (strcmp(name, "SHIFT") == 0 || strcmp(name, "SELECT") == 0) {
    *mask |= key_select;
    return 1;
  }
  if (strcmp(name, "PLAY") == 0 || strcmp(name, "START") == 0) {
    *mask |= key_start;
    return 1;
  }
  return 0;
}

int ai_server_parse_key_combination(const char *input, uint8_t *out_mask, char *err_buf, size_t err_buf_len) {
  *out_mask = 0;
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", input);

  // Delimiters: +, |, comma, space
  const char *delimiters = "+|, \t\r\n";
  char *token = strtok(buf, delimiters);
  int count = 0;

  while (token != NULL) {
    char key_name[64];
    snprintf(key_name, sizeof(key_name), "%s", token);
    str_trim_and_upper(key_name);

    if (strlen(key_name) > 0) {
      uint8_t single_mask = 0;
      if (!parse_single_key(key_name, &single_mask)) {
        if (err_buf && err_buf_len > 0) {
          snprintf(err_buf, err_buf_len, "Unknown key token '%s'", key_name);
        }
        return 0;
      }
      *out_mask |= single_mask;
      count++;
    }
    token = strtok(NULL, delimiters);
  }

  if (count == 0) {
    if (err_buf && err_buf_len > 0) {
      snprintf(err_buf, err_buf_len, "No keys specified");
    }
    return 0;
  }

  return 1;
}

static void handle_client_command(SOCKET client_sock, const char *cmd_line) {
  char line[512];
  snprintf(line, sizeof(line), "%s", cmd_line);
  str_trim_and_upper(line);

  if (strlen(line) == 0) {
    return;
  }

  ai_log("AI_TCP", "Command: '%s'", line);

  if (strcmp(line, "PING") == 0) {
    const char *resp = "PONG\n";
    send(client_sock, resp, (int)strlen(resp), 0);
    return;
  }

  if (strcmp(line, "SCREENSHOT") == 0) {
    if (screenshot_mutex) {
      SDL_LockMutex(screenshot_mutex);
    }

    SDL_Event event;
    SDL_zero(event);
    event.type = AI_SCREENSHOT_EVENT;
    if (!SDL_PushEvent(&event)) {
      const char *resp = "ERROR Failed to push screenshot SDL event\n";
      send(client_sock, resp, (int)strlen(resp), 0);
      if (screenshot_mutex) SDL_UnlockMutex(screenshot_mutex);
      ai_log("VISION", "Error: failed to push screenshot event");
      return;
    }

#ifdef _WIN32
    DWORD wait_res = WaitForSingleObject(screenshot_ready_event, 2000);
    int ok = (wait_res == WAIT_OBJECT_0);
#else
    int ok = SDL_WaitSemaphoreTimeout(screenshot_ready_event, 2000);
#endif

    if (ok && shared_pixel_buffer) {
      size_t total_sent = 0;
      while (total_sent < AI_SCREENSHOT_BUFFER_SIZE) {
        int n = send(client_sock, (const char *)shared_pixel_buffer + total_sent,
                     (int)(AI_SCREENSHOT_BUFFER_SIZE - total_sent), 0);
        if (n <= 0) break;
        total_sent += n;
      }
      ai_log("VISION", "Sent %zu bytes (320x240 RGB24) to client", total_sent);
    } else {
      const char *resp = "ERROR Screenshot capture timed out\n";
      send(client_sock, resp, (int)strlen(resp), 0);
      ai_log("VISION", "Screenshot capture timed out");
    }

    if (screenshot_mutex) {
      SDL_UnlockMutex(screenshot_mutex);
    }
    return;
  }

  if (strcmp(line, "REC_START") == 0 || strncmp(line, "REC_START ", 10) == 0 || strncmp(line, "REC_START\t", 10) == 0) {
    const char *raw_fn = (strlen(cmd_line) > 10) ? (cmd_line + 10) : "m8c_record.wav";
    char filename[256];
    snprintf(filename, sizeof(filename), "%s", raw_fn);
    // Trim whitespace
    char *p = filename;
    while (isspace((unsigned char)*p)) p++;
    if (p != filename) memmove(filename, p, strlen(p) + 1);
    size_t flen = strlen(filename);
    while (flen > 0 && isspace((unsigned char)filename[flen - 1])) filename[--flen] = '\0';
    if (strlen(filename) == 0) {
      snprintf(filename, sizeof(filename), "m8c_record.wav");
    }

    if (ai_server_rec_start(filename)) {
      char resp[300];
      snprintf(resp, sizeof(resp), "OK REC_START %s\n", filename);
      send(client_sock, resp, (int)strlen(resp), 0);
    } else {
      const char *resp = "ERROR Failed to open WAV file for recording\n";
      send(client_sock, resp, (int)strlen(resp), 0);
    }
    return;
  }

  if (strcmp(line, "REC_STOP") == 0) {
    uint32_t recorded_bytes = 0;
    if (ai_server_rec_stop(&recorded_bytes)) {
      char resp[128];
      snprintf(resp, sizeof(resp), "OK REC_STOP %u\n", recorded_bytes);
      send(client_sock, resp, (int)strlen(resp), 0);
    } else {
      const char *resp = "ERROR Not currently recording\n";
      send(client_sock, resp, (int)strlen(resp), 0);
    }
    return;
  }

  if (strcmp(line, "LOGS") == 0 || strncmp(line, "LOGS ", 5) == 0 || strncmp(line, "GET_LOGS", 8) == 0) {
    int max_lines = 20;
    const char *num_str = NULL;
    if (strncmp(line, "LOGS ", 5) == 0) num_str = cmd_line + 5;
    else if (strncmp(line, "GET_LOGS ", 9) == 0) num_str = cmd_line + 9;
    if (num_str) {
      int parsed = atoi(num_str);
      if (parsed > 0) max_lines = parsed;
    }

    static char logs_buf[65536];
    int count = ai_logger_get_recent(logs_buf, sizeof(logs_buf), max_lines);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "OK LOGS %d\n", count);
    send(client_sock, hdr, (int)strlen(hdr), 0);
    if (count > 0) {
      send(client_sock, logs_buf, (int)strlen(logs_buf), 0);
    }
    return;
  }

  if (strcmp(line, "GET_STATE") == 0 || strcmp(line, "STATE") == 0) {
    char json_buf[2048];
    ai_screen_get_state_json(json_buf, sizeof(json_buf));
    char resp[2300];
    snprintf(resp, sizeof(resp), "OK STATE %s\n", json_buf);
    send(client_sock, resp, (int)strlen(resp), 0);
    ai_log("AI_TCP", "Returned GET_STATE");
    return;
  }

  if (strcmp(line, "GET_TEXT_SCREEN") == 0 || strcmp(line, "TEXT_SCREEN") == 0 ||
      strcmp(line, "GET_TEXT_SCREEN MARKED") == 0 || strcmp(line, "TEXT_SCREEN MARKED") == 0) {
    int marked = (strstr(line, "MARKED") != NULL) ? 1 : 0;
    static char grid_buf[4096];
    int len = ai_screen_get_text_grid(grid_buf, sizeof(grid_buf), marked);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "OK TEXT_SCREEN 30\n");
    send(client_sock, hdr, (int)strlen(hdr), 0);
    if (len > 0) {
      send(client_sock, grid_buf, (int)strlen(grid_buf), 0);
    }
    ai_log("AI_TCP", "Returned GET_TEXT_SCREEN (30 lines, marked=%d)", marked);
    return;
  }

  if (strcmp(line, "GET_CURSOR") == 0 || strcmp(line, "CURSOR") == 0) {
    int col = -1, row = -1;
    char input_name[32] = {0};
    ai_screen_get_cursor(&col, &row, input_name, sizeof(input_name));
    char resp[128];
    snprintf(resp, sizeof(resp), "OK CURSOR %d %d %s\n", col, row, input_name);
    send(client_sock, resp, (int)strlen(resp), 0);
    ai_log("AI_TCP", "Returned GET_CURSOR (%d, %d, %s)", col, row, input_name);
    return;
  }

  if (strncmp(line, "KEY_DOWN ", 9) == 0 || strncmp(line, "KEY_DOWN\t", 9) == 0) {
    const char *keys_part = cmd_line + 9;
    uint8_t bitmask = 0;
    char err_msg[128] = {0};
    if (!ai_server_parse_key_combination(keys_part, &bitmask, err_msg, sizeof(err_msg))) {
      char resp[256];
      snprintf(resp, sizeof(resp), "ERROR %s\n", err_msg);
      send(client_sock, resp, (int)strlen(resp), 0);
      return;
    }
    SDL_Event event;
    SDL_zero(event);
    event.type = AI_INPUT_EVENT;
    event.user.code = (Sint32)((1 << 24) | (uint32_t)bitmask);
    SDL_PushEvent(&event);
    ai_log("KEY", "Held keys down: '%s' -> 0x%02X", keys_part, bitmask);
    char resp[128];
    snprintf(resp, sizeof(resp), "OK KEY_DOWN 0x%02X\n", bitmask);
    send(client_sock, resp, (int)strlen(resp), 0);
    return;
  }

  if (strcmp(line, "KEY_UP") == 0 || strncmp(line, "KEY_UP ", 7) == 0 || strncmp(line, "KEY_UP\t", 7) == 0) {
    uint8_t bitmask = 0;
    if (strlen(line) > 7) {
      const char *keys_part = cmd_line + 7;
      char err_msg[128] = {0};
      ai_server_parse_key_combination(keys_part, &bitmask, err_msg, sizeof(err_msg));
    }
    SDL_Event event;
    SDL_zero(event);
    event.type = AI_INPUT_EVENT;
    event.user.code = (Sint32)((2 << 24) | (uint32_t)bitmask);
    SDL_PushEvent(&event);
    ai_log("KEY", "Released keys (mask 0x%02X)", bitmask);
    char resp[128];
    snprintf(resp, sizeof(resp), "OK KEY_UP 0x%02X\n", bitmask);
    send(client_sock, resp, (int)strlen(resp), 0);
    return;
  }

  if (strncmp(line, "KEY ", 4) == 0 || strncmp(line, "KEY\t", 4) == 0 ||
      strncmp(line, "KEY_PRESS ", 10) == 0 || strncmp(line, "KEY_PRESS\t", 10) == 0) {
    const char *keys_part = (strncmp(line, "KEY_PRESS", 9) == 0) ? (cmd_line + 10) : (cmd_line + 4);
    char keys_copy[256];
    snprintf(keys_copy, sizeof(keys_copy), "%s", keys_part);
    
    // Check if there is an optional duration at the end
    int duration_ms = 30;
    char *last_space = strrchr(keys_copy, ' ');
    if (last_space && isdigit((unsigned char)*(last_space + 1))) {
      int parsed_dur = atoi(last_space + 1);
      if (parsed_dur > 0 && parsed_dur <= 5000) {
        duration_ms = parsed_dur;
        *last_space = '\0';
      }
    }

    uint8_t bitmask = 0;
    char err_msg[128] = {0};

    if (!ai_server_parse_key_combination(keys_copy, &bitmask, err_msg, sizeof(err_msg))) {
      char resp[256];
      snprintf(resp, sizeof(resp), "ERROR %s\n", err_msg);
      send(client_sock, resp, (int)strlen(resp), 0);
      ai_log("KEY", "Error parsing keys '%s': %s", keys_copy, err_msg);
      return;
    }

    // Push event to main SDL thread
    SDL_Event event;
    SDL_zero(event);
    event.type = AI_INPUT_EVENT;
    event.user.code = (Sint32)(((uint32_t)duration_ms << 8) | (uint32_t)bitmask);
    if (!SDL_PushEvent(&event)) {
      const char *resp = "ERROR Failed to push SDL event\n";
      send(client_sock, resp, (int)strlen(resp), 0);
      ai_log("KEY", "Error: failed to push SDL event");
      return;
    }

    ai_log("KEY", "Injected key command: '%s' -> 0x%02X (%dms)", keys_copy, bitmask, duration_ms);
    char resp[128];
    snprintf(resp, sizeof(resp), "OK KEY 0x%02X %dms\n", bitmask, duration_ms);
    send(client_sock, resp, (int)strlen(resp), 0);
    return;
  }

  // Unrecognized command
  char resp[256];
  snprintf(resp, sizeof(resp), "ERROR Unknown command: %s\n", line);
  send(client_sock, resp, (int)strlen(resp), 0);
  ai_log("AI_TCP", "Unknown command received: '%s'", line);
}

static void handle_client(SOCKET client_sock) {
  char recv_buf[1024];
  char line_buf[1024];
  size_t line_len = 0;

  ai_log("AI_TCP", "Client connected (socket %d)", (int)client_sock);

  while (server_running) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(client_sock, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms timeout

    int sel_ret = select((int)client_sock + 1, &read_fds, NULL, NULL, &tv);
    if (sel_ret < 0) {
      break;
    }
    if (sel_ret == 0) {
      continue;
    }

    int bytes_read = recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
    if (bytes_read <= 0) {
      break; // Client disconnected or error
    }

    for (int i = 0; i < bytes_read; i++) {
      char c = recv_buf[i];
      if (c == '\n' || c == '\r') {
        if (line_len > 0) {
          line_buf[line_len] = '\0';
          handle_client_command(client_sock, line_buf);
          line_len = 0;
        }
      } else {
        if (line_len < sizeof(line_buf) - 1) {
          line_buf[line_len++] = c;
        }
      }
    }
  }

  ai_log("AI_TCP", "Client disconnected (socket %d)", (int)client_sock);
  closesocket(client_sock);
}

static int SDLCALL ai_server_thread_fn(void *data) {
  (void)data;
  ai_log("AI_TCP", "AI Server listening on %s:%d", AI_SERVER_HOST, AI_SERVER_PORT);

  while (server_running) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_sock, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms timeout to periodically check server_running

    int sel_ret = select((int)listen_sock + 1, &read_fds, NULL, NULL, &tv);
    if (sel_ret < 0) {
      if (!server_running) break;
      continue;
    }
    if (sel_ret == 0) {
      continue;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    SOCKET client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock == INVALID_SOCKET) {
      if (!server_running) break;
      continue;
    }

    handle_client(client_sock);
  }

  ai_log("AI_TCP", "AI Server thread stopped");
  return 0;
}

int ai_server_init(void) {
  if (shared_pixel_buffer == NULL) {
    shared_pixel_buffer = calloc(1, AI_SCREENSHOT_BUFFER_SIZE);
    if (!shared_pixel_buffer) {
      ai_log("SYS", "Failed to allocate shared_pixel_buffer (%d bytes)", AI_SCREENSHOT_BUFFER_SIZE);
      return 0;
    }
  }

  if (AI_INPUT_EVENT == 0) {
    AI_INPUT_EVENT = SDL_RegisterEvents(2);
    if (AI_INPUT_EVENT == (Uint32)-1) {
      ai_log("SYS", "Failed to register custom AI SDL events");
      return 0;
    }
    AI_SCREENSHOT_EVENT = AI_INPUT_EVENT + 1;
  }

#ifdef _WIN32
  if (screenshot_ready_event == NULL) {
    screenshot_ready_event = CreateEvent(NULL, FALSE, FALSE, NULL);
  }
#else
  if (screenshot_ready_event == NULL) {
    screenshot_ready_event = SDL_CreateSemaphore(0);
  }
#endif

  if (screenshot_mutex == NULL) {
    screenshot_mutex = SDL_CreateMutex();
  }

  if (audio_rec_mutex == NULL) {
    audio_rec_mutex = SDL_CreateMutex();
  }

#ifdef _WIN32
  WSADATA wsa_data;
  int wsa_res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (wsa_res != 0) {
    ai_log("SYS", "WSAStartup failed: %d", wsa_res);
    return 0;
  }
#endif

  listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock == INVALID_SOCKET) {
    ai_log("SYS", "Failed to create AI server socket");
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
  }

  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(AI_SERVER_HOST);
  server_addr.sin_port = htons(AI_SERVER_PORT);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
    ai_log("SYS", "Failed to bind AI server socket to %s:%d", AI_SERVER_HOST, AI_SERVER_PORT);
    closesocket(listen_sock);
    listen_sock = INVALID_SOCKET;
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
  }

  if (listen(listen_sock, 4) == SOCKET_ERROR) {
    ai_log("SYS", "Failed to listen on AI server socket");
    closesocket(listen_sock);
    listen_sock = INVALID_SOCKET;
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
  }

  server_running = 1;
  server_thread = SDL_CreateThread(ai_server_thread_fn, "ai_server_thread", NULL);
  if (!server_thread) {
    ai_log("SYS", "Failed to create AI server thread: %s", SDL_GetError());
    server_running = 0;
    closesocket(listen_sock);
    listen_sock = INVALID_SOCKET;
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
  }

  return 1;
}

void ai_server_shutdown(void) {
  if (!server_running && listen_sock == INVALID_SOCKET) {
    return;
  }

  // Stop any active audio recording
  ai_server_rec_stop(NULL);

  server_running = 0;

  if (listen_sock != INVALID_SOCKET) {
    closesocket(listen_sock);
    listen_sock = INVALID_SOCKET;
  }

  if (server_thread) {
    SDL_WaitThread(server_thread, NULL);
    server_thread = NULL;
  }

#ifdef _WIN32
  if (screenshot_ready_event) {
    CloseHandle(screenshot_ready_event);
    screenshot_ready_event = NULL;
  }
  WSACleanup();
#else
  if (screenshot_ready_event) {
    SDL_DestroySemaphore(screenshot_ready_event);
    screenshot_ready_event = NULL;
  }
#endif

  if (screenshot_mutex) {
    SDL_DestroyMutex(screenshot_mutex);
    screenshot_mutex = NULL;
  }

  if (audio_rec_mutex) {
    SDL_DestroyMutex(audio_rec_mutex);
    audio_rec_mutex = NULL;
  }

  if (shared_pixel_buffer) {
    free(shared_pixel_buffer);
    shared_pixel_buffer = NULL;
  }
}

static uint8_t g_persistent_key_mask = 0;

void ai_server_handle_event(struct app_context *ctx, const SDL_Event *event) {
  if (event->type == AI_INPUT_EVENT) {
    uint32_t code = (uint32_t)event->user.code;
    uint8_t mode = (uint8_t)((code >> 24) & 0xFF);
    uint8_t bitmask = (uint8_t)(code & 0xFF);
    uint16_t duration = (uint16_t)((code >> 8) & 0xFFFF);
    if (duration == 0) duration = 30;

    if (mode == 1) {
      // Hold down
      g_persistent_key_mask |= bitmask;
      ai_log("KEY", "Hold down key mask 0x%02X (active: 0x%02X)", bitmask, g_persistent_key_mask);
      if (ctx->device_connected) {
        m8_send_msg_controller(g_persistent_key_mask);
      }
    } else if (mode == 2) {
      // Release
      if (bitmask == 0) {
        g_persistent_key_mask = 0;
      } else {
        g_persistent_key_mask &= ~bitmask;
      }
      ai_log("KEY", "Release keys (remaining active: 0x%02X)", g_persistent_key_mask);
      if (ctx->device_connected) {
        m8_send_msg_controller(g_persistent_key_mask);
      }
    } else {
      // Normal pulse with duration
      uint8_t combined = g_persistent_key_mask | bitmask;
      ai_log("KEY", "Pulse keystroke bitmask 0x%02X (%dms)", combined, duration);

      if (ctx->device_connected) {
        m8_send_msg_controller(combined);
        SDL_Delay(duration);
        m8_send_msg_controller(g_persistent_key_mask);
      }
    }
  }
}

void ai_server_handle_screenshot(struct app_context *ctx) {
  (void)ctx;
  if (shared_pixel_buffer) {
    renderer_read_pixels_rgb24(shared_pixel_buffer, AI_SCREENSHOT_WIDTH, AI_SCREENSHOT_HEIGHT);
  }
#ifdef _WIN32
  if (screenshot_ready_event) {
    SetEvent(screenshot_ready_event);
  }
#else
  if (screenshot_ready_event) {
    SDL_SignalSemaphore(screenshot_ready_event);
  }
#endif
}
