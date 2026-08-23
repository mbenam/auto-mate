# M8C AI Automation & Developer Guide

Welcome to the **M8C AI Automation & Developer Guide**. This document provides an extensive reference, architectural overview, TCP protocol specification, screen navigation model, and end-to-end Python examples for controlling Dirtywave M8 tracker hardware headlessly via `m8c`.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [CLI Options & Execution Modes](#2-cli-options--execution-modes)
3. [TCP Protocol Specification](#3-tcp-protocol-specification)
   - [Wire Protocol](#wire-protocol)
   - [Command Reference Table](#command-reference-table)
   - [Detailed Command Specifications](#detailed-command-specifications)
4. [Virtual Screen & State Tracking](#4-virtual-screen--state-tracking)
   - [Grid Layout & Coordinates](#grid-layout--coordinates)
   - [Screen Identification Heuristics](#screen-identification-heuristics)
   - [State JSON Schema](#state-json-schema)
5. [Keyboard & Key Combo Simulation](#5-keyboard--key-combo-simulation)
   - [Key Bitmasks & Names](#key-bitmasks--names)
   - [Press, Hold, and Release Mechanisms](#press-hold-and-release-mechanisms)
6. [Screenshots & Computer Vision Pipeline](#6-screenshots--computer-vision-pipeline)
7. [Direct Audio to WAV Recording](#7-direct-audio-to-wav-recording)
8. [Extensive Python Code Examples](#8-extensive-python-code-examples)
   - [Example 1: Core `M8Client` Wrapper Class](#example-1-core-m8client-wrapper-class)
   - [Example 2: Text Screen & JSON State Monitor](#example-2-text-screen--json-state-monitor)
   - [Example 3: Autonomous Beat & Note Programming](#example-3-autonomous-beat--note-programming)
   - [Example 4: Synthesizer Engine & Parameter Automation](#example-4-synthesizer-engine--parameter-automation)
   - [Example 5: Computer Vision & Screenshot Processing](#example-5-computer-vision--screenshot-processing)
   - [Example 6: Direct WAV Recording & Waveform Verification](#example-6-direct-wav-recording--waveform-verification)
   - [Example 7: Project Creation & SD Card Save Automation](#example-7-project-creation--sd-card-save-automation)
9. [Troubleshooting & Best Practices](#9-troubleshooting--best-practices)

---

## 1. Architecture Overview

```
+-------------------------------------------------------------------------+
|                                 m8c                                     |
|                                                                         |
|  +--------------------+     SLIP Packets     +-----------------------+  |
|  |   M8 USB Device    | <==================> |  M8 Protocol Parser   |  |
|  |  (Teensy / M8 HW)  |                      |  (src/command.c)      |  |
|  +--------------------+                      +-----------+-----------+  |
|                                                          |              |
|                                       Draw Char / Rect   |              |
|                                                          v              |
|  +--------------------+                      +-----------------------+  |
|  |    Audio Engine    |                      |     Virtual Screen    |  |
|  | (USB -> WAV File)  |                      |   30x40 ASCII Grid    |  |
|  +---------+----------+                      |  (src/ai_screen.c)    |  |
|            |                                 +-----------+-----------+  |
|            | Direct PCM Frames                           |              |
|            v                                             | State Query  |
|  +-------------------------------------------------------+-----------+  |
|  |                    Multi-Threaded AI TCP Server                   |  |
|  |                    (127.0.0.1:9123, src/ai_server.c)              |  |
|  +-----------------------------------+-------------------------------+  |
+--------------------------------------|----------------------------------+
                                       | TCP Socket (Line-based JSON/Text)
                                       v
                        +-------------------------------+
                        |       AI Agent / Python       |
                        |      (LLM / Vision / Audio)   |
                        +-------------------------------+
```

The system operates across three synchronized layers:

1. **SDL Main Loop Thread**: Receives and processes SLIP packets (`0xFD` draw character, `0xFE` draw rectangle) from the M8 over USB serial, updating the intermediate display texture and virtual text matrix.
2. **Virtual Screen Buffer (`src/ai_screen.c`)**: Maintains a single source of truth for the $40 \text{ columns} \times 30 \text{ rows}$ ASCII grid, active cursor location, focused parameter name, current value, screen type, and play state.
3. **AI TCP Server Thread (`src/ai_server.c`)**: Listens on `127.0.0.1:9123`, providing instantaneous state queries, keystroke injections, thread-safe raw screenshot streaming (320x240 RGB24), and WAV audio recording control.

---

## 2. CLI Options & Execution Modes

`m8c` can be launched in either GUI mode (interactive window) or Headless Daemon mode (background process for AI agents):

```bash
# Normal GUI window with AI TCP server enabled
m8c.exe

# Headless Daemon mode (no window, virtual offscreen rendering, port 9123 active)
m8c.exe --headless

# Enable USB audio streaming from M8
m8c.exe --audio

# Full Headless AI Server with USB Audio capture and session logging
m8c.exe --headless --audio --log session.log
```

### CLI Flag Reference

| Flag | Short | Description |
| :--- | :--- | :--- |
| `--headless`, `--daemon` | `-d` | Runs without creating an OS window. Ideal for cloud servers and automated agents. |
| `--audio` | `-a` | Automatically identifies and binds the M8 USB Digital Audio Interface. |
| `--no-audio` | | Disables audio stream initialization. |
| `--log <filename>` | `-l` | Writes structured session logs. Automatically saved into the `logs/` directory. |
| `--dev <device>` | | Selects a specific serial port (e.g. `COM3` or `/dev/ttyACM0`). |
| `--config <file>` | | Specifies an alternative `config.ini` filepath. |
| `--list` | | Lists all connected M8 / Teensy serial devices and exits. |

---

## 3. TCP Protocol Specification

### Wire Protocol
- **Transport**: TCP over IPv4.
- **Default Endpoint**: `127.0.0.1:9123`
- **Encoding**: UTF-8, line-terminated (`\n`).
- **Response Format**: Every command returns a line starting with `OK` or `ERROR`, with additional binary or multi-line payloads where specified.

---

### Command Reference Table

| Command | Payload / Arguments | Response Format | Description |
| :--- | :--- | :--- | :--- |
| **`PING`** | None | `PONG\n` | Heartbeat and liveness check. |
| **`GET_STATE`** | None | `OK STATE <json_object>\n` | Returns full screen state, cursor position, active field, value, and play state. |
| **`GET_CURSOR`** | None | `OK CURSOR <col> <row> <input_name>\n` | Returns current cursor grid coordinates and resolved input field name. |
| **`GET_TEXT_SCREEN`** | `[MARKED]` | `OK TEXT_SCREEN 30\n` + 30 lines | Dumps the full $40 \times 30$ ASCII screen grid. `MARKED` adds `[ ]` brackets at cursor. |
| **`KEY`** | `<combo>` | `OK KEY <hex_mask> <duration>ms\n` | Injects a standard keystroke pulse (default: 30ms hold). |
| **`KEY_DOWN`** | `<combo>` | `OK KEY_DOWN <hex_mask>\n` | Holds down specified keys continuously until released. |
| **`KEY_UP`** | `[combo]` | `OK KEY_UP <hex_mask>\n` | Releases specified keys (or all keys if no argument given). |
| **`KEY_PRESS`** | `<combo> [ms]` | `OK KEY_PRESS <hex_mask> <ms>\n` | Pulses keys with a custom duration in milliseconds. |
| **`SCREENSHOT`** | None | `OK SCREENSHOT 230400\n` + binary bytes | Captures 230,400 raw RGB24 bytes (320x240 resolution). |
| **`REC_START`** | `<filepath.wav>` | `OK REC_START <filepath>\n` | Intercepts USB audio stream and begins writing 44.1kHz 16-bit stereo WAV. |
| **`REC_STOP`** | None | `OK REC_STOP <byte_count>\n` | Finalizes WAV header with exact byte length and closes file. |
| **`LOGS`** | `[count]` | `OK LOGS <n>\n` + $n$ log lines | Returns recent in-memory log entries from circular buffer. |

---

### Detailed Command Specifications

#### 1. `GET_STATE`
Returns a compact single-line JSON object representing the entire screen state:
```json
{
  "screen": "PHRASE",
  "cursor_col": 3,
  "cursor_row": 4,
  "input": "NOTE",
  "value": "C-4",
  "header": "PHRASE 00",
  "play_state": "PLAYING",
  "cursor_text_line": "0 C-4 80 00 ---00 ---00 ---00"
}
```

#### 2. `KEY <combo>`
Simulates simultaneous button presses. Combinations can be separated by `+`, `,`, or `|`.
- Valid keys: `UP`, `DOWN`, `LEFT`, `RIGHT`, `EDIT`, `OPTION` (or `OPT`), `SHIFT` (or `SELECT`), `PLAY` (or `START`).
- Examples: `KEY UP`, `KEY SHIFT+PLAY`, `KEY EDIT+UP`, `KEY OPTION+LEFT`.

#### 3. `SCREENSHOT`
Transmits an exact 230,400-byte raw RGB frame buffer directly from SDL's intermediate render target:
$$\text{Payload Size} = 320 \times 240 \times 3 \text{ bytes (RGB24)} = 230,400 \text{ bytes}$$

#### 4. `REC_START <filepath>` / `REC_STOP`
Captures raw PCM audio passing through the SDL stream:
- **Sample Rate**: 44,100 Hz
- **Channels**: 2 (Stereo)
- **Bit Depth**: 16-bit Signed Integer (Little-Endian)
- **Header**: Standard 44-byte RIFF/WAVE header finalized on `REC_STOP`.

---

## 4. Virtual Screen & State Tracking

### Grid Layout & Coordinates
The M8 screen displays text on a $40 \text{ column} \times 30 \text{ row}$ character grid:
- **Columns**: $0 \le x < 40$ ($x=0$ is leftmost column).
- **Rows**: $0 \le y < 30$ ($y=0$ is topmost header line).

### Screen Identification Heuristics
The virtual screen parser identifies the active view dynamically:
1. **Header Matching**: Matches screen titles (`SONG`, `CHAIN`, `PHRASE`, `MIXER`, `TABLE`, `GROOVE`, `SCALE`, `PROJECT`, `SYSTEM`).
2. **Modal Detection**: Identifies modal overlays (`KEYBOARD`, `FILE_BROWSER`, `INSTRUMENT POOL`, `CONFIRM_DIALOG`).
3. **Left-Label Scanning**: On parameter-heavy synth pages (`WAVSYNTH`, `MACROSYNTH`, `SAMPLER`, `EFFECTS`), scans leftward from the cursor position to find the nearest non-empty alphanumeric label (e.g. `CUTOFF`, `RES`, `VOLUME`, `PITCH`).

---

## 5. Keyboard & Key Combo Simulation

### Key Bitmasks & Names

| Button | Hex Bitmask | Physical Default | Alternate Binding |
| :--- | :--- | :--- | :--- |
| **`EDIT`** | `0x01` | `X` (`SDL_SCANCODE_X`) | Left Ctrl |
| **`OPTION`** | `0x02` | `Z` (`SDL_SCANCODE_Z`) | Left Alt |
| **`RIGHT`** | `0x04` | Right Arrow | Keypad 6 |
| **`PLAY`** | `0x08` | Space Bar | `S` |
| **`SHIFT`** | `0x10` | Left Shift | `A` |
| **`DOWN`** | `0x20` | Down Arrow | Keypad 2 |
| **`UP`** | `0x40` | Up Arrow | Keypad 8 |
| **`LEFT`** | `0x80` | Left Arrow | Keypad 4 |

---

## 6. Screenshots & Computer Vision Pipeline

Screenshots use a thread-safe mutex and condition variable (`screenshot_ready_event`). When `SCREENSHOT` is received by the TCP server:
1. Pushes `AI_SCREENSHOT_EVENT` to SDL's event queue.
2. The SDL rendering thread captures the raw $320 \times 240$ texture into `shared_pixel_buffer` via `SDL_RenderReadPixels`.
3. Signals the condition variable to wake the TCP thread.
4. The TCP server streams the 230,400 RGB24 bytes across the socket.

---

## 7. Direct Audio to WAV Recording

Audio recording hooks directly into `audio_cb_out` in [`src/backends/audio_sdl.c`](file:///c:/dev/m8c/src/backends/audio_sdl.c). Whenever `ai_is_recording` is true:
- Raw audio buffers from the M8 USB input stream are written directly to disk.
- Zero latency and zero conversion loss (identical to direct USB stream).
- When `REC_STOP` is called, the file seeks to byte 0 and writes the exact file length into the RIFF and data chunk headers.

---

## 8. Extensive Python Code Examples

### Example 1: Core `M8Client` Wrapper Class

```python
#!/usr/bin/env python3
"""
m8_client.py - Reusable Python Client for M8C AI Server
"""
import socket
import time
import json
import os

class M8Client:
    def __init__(self, host="127.0.0.1", port=9123, timeout=5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.buf = bytearray()

    def connect(self, retries=15, retry_delay=0.3):
        """Connects to the M8C AI Server over TCP."""
        for attempt in range(retries):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(self.timeout)
                self.sock.connect((self.host, self.port))
                return True
            except (ConnectionRefusedError, socket.timeout):
                time.sleep(retry_delay)
        raise ConnectionError(f"Could not connect to m8c on {self.host}:{self.port}")

    def close(self):
        """Closes the TCP connection."""
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _read_line(self):
        """Reads a single newline-terminated line from the server."""
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            self.buf.extend(chunk)
        if b"\n" in self.buf:
            idx = self.buf.index(b"\n")
            line = self.buf[:idx].decode("utf-8", errors="replace").strip()
            del self.buf[:idx + 1]
            return line
        line = self.buf.decode("utf-8", errors="replace").strip()
        self.buf.clear()
        return line

    def send_command(self, cmd):
        """Sends a raw command string and returns the response."""
        if not cmd.endswith("\n"):
            cmd += "\n"
        self.sock.sendall(cmd.encode("utf-8"))
        return self._read_line()

    def ping(self):
        """Checks connection liveness."""
        return self.send_command("PING") == "PONG"

    def key(self, combo, delay=0.08):
        """Sends a keystroke pulse and waits a short duration."""
        resp = self.send_command(f"KEY {combo}")
        time.sleep(delay)
        return resp

    def key_down(self, combo):
        """Holds keys down continuously."""
        return self.send_command(f"KEY_DOWN {combo}")

    def key_up(self, combo=""):
        """Releases held keys."""
        return self.send_command(f"KEY_UP {combo}".strip())

    def get_state(self):
        """Queries full state JSON dictionary."""
        resp = self.send_command("GET_STATE")
        if resp.startswith("OK STATE "):
            return json.loads(resp[9:])
        return {}

    def get_cursor(self):
        """Returns (col, row, input_name)."""
        resp = self.send_command("GET_CURSOR")
        if resp.startswith("OK CURSOR "):
            parts = resp.split(" ", 3)
            return int(parts[2]), int(parts[3]), parts[4] if len(parts) > 4 else ""
        return -1, -1, ""

    def get_text_screen(self, marked=True):
        """Returns list of 30 strings representing ASCII screen grid."""
        cmd = "GET_TEXT_SCREEN MARKED" if marked else "GET_TEXT_SCREEN"
        header = self.send_command(cmd)
        lines = []
        if "OK TEXT_SCREEN" in header:
            for _ in range(30):
                lines.append(self._read_line())
        return lines

    def get_screenshot_bytes(self):
        """Returns 230,400 raw RGB24 bytes."""
        header = self.send_command("SCREENSHOT")
        if not header.startswith("OK SCREENSHOT "):
            raise RuntimeError(f"Screenshot error: {header}")
        byte_count = int(header.split(" ")[2])
        data = bytearray()
        while len(data) < byte_count:
            chunk = self.sock.recv(min(4096, byte_count - len(data)))
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    def rec_start(self, wav_filepath):
        """Starts recording audio to WAV file."""
        abs_path = os.path.abspath(wav_filepath)
        return self.send_command(f"REC_START {abs_path}")

    def rec_stop(self):
        """Stops audio recording and finalizes WAV file."""
        return self.send_command("REC_STOP")
```

---

### Example 2: Text Screen & JSON State Monitor

```python
#!/usr/bin/env python3
"""
monitor_m8.py - Real-time terminal dashboard of M8 Screen & State
"""
import time
import os
from m8_client import M8Client

def main():
    client = M8Client()
    client.connect()
    print("Connected to M8C AI Server. Press Ctrl+C to exit.\n")

    try:
        while True:
            state = client.get_state()
            screen_lines = client.get_text_screen(marked=True)

            os.system("cls" if os.name == "nt" else "clear")
            print("=" * 44)
            print(f" SCREEN: {state.get('screen')} | PLAY: {state.get('play_state')}")
            print(f" INPUT:  {state.get('input')} = '{state.get('value')}'")
            print(f" CURSOR: ({state.get('cursor_col')}, {state.get('cursor_row')})")
            print("=" * 44)
            print("+" + "-" * 40 + "+")
            for line in screen_lines[:24]:
                print(f"|{line.ljust(40)}|")
            print("+" + "-" * 40 + "+")
            time.sleep(0.15)
    except KeyboardInterrupt:
        print("\nExiting monitor.")
    finally:
        client.close()

if __name__ == "__main__":
    main()
```

---

### Example 3: Autonomous Beat & Note Programming

```python
#!/usr/bin/env python3
"""
program_beat.py - Automatically navigates to Song view and programs a 4-step bassline
"""
from m8_client import M8Client

def escape_to_song_view(client):
    """Safely dismisses modals and returns to root Song view."""
    for _ in range(5):
        client.key("OPTION")
        client.key("SHIFT+LEFT")
        state = client.get_state()
        if state.get("screen") == "SONG":
            return True
    return False

def program_phrase():
    client = M8Client()
    client.connect()

    # 1. Reach root Song view
    escape_to_song_view(client)

    # 2. Reset cursor to Track 1, Row 0
    for _ in range(15):
        client.key("UP")
    for _ in range(10):
        client.key("LEFT")

    # 3. Create Chain 00 on Track 1
    client.key("EDIT")
    client.key("EDIT+UP")

    # 4. Enter Chain 00 (SHIFT+RIGHT) and create Phrase 00
    client.key("SHIFT+RIGHT")
    client.key("EDIT")
    client.key("EDIT+UP")

    # 5. Enter Phrase 00 (SHIFT+RIGHT) and program 4 notes
    client.key("SHIFT+RIGHT")
    notes = ["C-4", "D#4", "G-4", "A#4"]
    print("[*] Programming 4-step sequence in Phrase 00...")

    for step_idx in range(4):
        client.key("EDIT+UP") # Set Note
        for _ in range(step_idx * 3): # Transpose
            client.key("EDIT+UP")
        client.key("RIGHT") # Velocity column
        client.key("EDIT+UP") # Set Vel FF
        client.key("RIGHT") # Instrument column
        client.key("EDIT+UP") # Set Inst 00
        client.key("DOWN") # Next step row
        client.key("LEFT")
        client.key("LEFT")

    # 6. Return to Song View
    client.key("SHIFT+LEFT") # Back to Chain
    client.key("SHIFT+LEFT") # Back to Song
    print("[+] Sequence successfully programmed on Track 1!")

    client.close()

if __name__ == "__main__":
    program_phrase()
```

---

### Example 4: Synthesizer Engine & Parameter Automation

```python
#!/usr/bin/env python3
"""
configure_synth.py - Configures Instrument 00 as WAVSYNTH with custom cutoff & resonance
"""
from m8_client import M8Client

def configure_synth_instrument():
    client = M8Client()
    client.connect()

    # Enter Instrument view from Phrase (SHIFT+RIGHT on Instrument column)
    print("[*] Entering Instrument View...")
    client.key("SHIFT+RIGHT")

    # Reset cursor to TYPE field at top-left
    for _ in range(10):
        client.key("UP")
    for _ in range(10):
        client.key("LEFT")

    # Change TYPE to WAVSYNTH
    print("[*] Setting Instrument Type to WAVSYNTH...")
    client.key("EDIT+UP")

    # If confirmation modal appears, confirm with OK
    state = client.get_state()
    screen = client.get_text_screen()
    for line in screen:
        if "OK" in line and "CANCEL" in line:
            client.key("LEFT")
            client.key("EDIT")
            break

    # Adjust Cutoff & Resonance
    client.key("DOWN") # Move to Cutoff
    for _ in range(8):
        client.key("EDIT+RIGHT") # Increase Cutoff

    client.key("DOWN") # Move to Resonance
    for _ in range(4):
        client.key("EDIT+RIGHT") # Increase Resonance

    print("[+] Instrument 00 successfully configured as WAVSYNTH!")
    client.close()

if __name__ == "__main__":
    configure_synth_instrument()
```

---

### Example 5: Computer Vision & Screenshot Processing

```python
#!/usr/bin/env python3
"""
vision_capture.py - Captures raw screenshot from M8C and converts to PIL Image & NumPy array
"""
from m8_client import M8Client
import io

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

def capture_screenshot(output_png="m8_screenshot.png"):
    client = M8Client()
    client.connect()

    print("[*] Requesting 320x240 RGB24 raw frame buffer...")
    raw_rgb = client.get_screenshot_bytes()
    print(f"[+] Received {len(raw_rgb):,} bytes.")

    if HAS_PIL:
        # Convert raw RGB24 bytes directly into a PIL Image
        img = Image.frombytes("RGB", (320, 240), raw_rgb)
        img.save(output_png)
        print(f"[+] Saved screenshot to '{output_png}'.")
    else:
        print("[!] PIL not installed. Saving raw binary bytes.")
        with open("m8_raw_320x240.rgb", "wb") as f:
            f.write(raw_rgb)

    client.close()

if __name__ == "__main__":
    capture_screenshot()
```

---

### Example 6: Direct WAV Recording & Waveform Verification

```python
#!/usr/bin/env python3
"""
record_live_audio.py - Records 5 seconds of live audio over USB and verifies PCM data
"""
import time
import os
import wave
import struct
from m8_client import M8Client

def record_and_verify(duration=4.0, output_wav="m8_live_recording.wav"):
    client = M8Client()
    client.connect()

    wav_path = os.path.abspath(output_wav)
    print(f"[*] Starting direct USB audio recording to '{wav_path}'...")
    client.rec_start(wav_path)

    print("[*] Triggering Song Playback (SHIFT+PLAY)...")
    client.key("SHIFT+PLAY")

    # Record for specified duration
    for s in range(int(duration)):
        time.sleep(1.0)
        state = client.get_state()
        print(f"    [Time {s+1}s] Play State: {state.get('play_state')}")

    print("[*] Stopping playback and finalizing WAV...")
    client.key("PLAY")
    client.rec_stop()

    # Analyze audio samples
    if os.path.exists(wav_path):
        with wave.open(wav_path, "rb") as wf:
            channels = wf.getnchannels()
            width = wf.getsampwidth()
            rate = wf.getframerate()
            frames = wf.getnframes()
            raw_bytes = wf.readframes(frames)
            total_samples = len(raw_bytes) // 2
            samples = struct.unpack(f"<{total_samples}h", raw_bytes) if total_samples > 0 else []
            nonzero = sum(1 for s in samples if s != 0)

            print("\n" + "=" * 50)
            print("         AUDIO RECORDING REPORT")
            print("=" * 50)
            print(f" File:       {output_wav}")
            print(f" Duration:   {frames / rate:.2f} seconds")
            print(f" Format:     {rate} Hz, {channels} Channels, {width*8}-bit PCM")
            print(f" Min/Max:    [{min(samples) if samples else 0}, {max(samples) if samples else 0}]")
            print(f" Non-Zero:   {nonzero:,} / {total_samples:,} ({nonzero*100.0/max(1,total_samples):.1f}%)")
            print("=" * 50 + "\n")

    client.close()

if __name__ == "__main__":
    record_and_verify()
```

---

### Example 7: Project Creation & SD Card Save Automation

```python
#!/usr/bin/env python3
"""
save_project_to_sd.py - Initializes brand-new project and commits to /Songs/ on SD Card
"""
import time
from m8_client import M8Client

def save_new_project_to_sd(project_name="AI_TRACK"):
    client = M8Client()
    client.connect()

    # 1. Navigate to Project Screen (SHIFT+UP from Song view)
    for _ in range(5):
        client.key("OPTION")
        client.key("SHIFT+LEFT")
    client.key("SHIFT+UP")

    # 2. Reset to top-left of Project
    for _ in range(20):
        client.key("UP")
    for _ in range(10):
        client.key("LEFT")

    # 3. Navigate down to PROJECT row (6 steps down from top)
    for _ in range(6):
        client.key("DOWN")

    # 4. Highlight SAVE (5 steps right from LOAD)
    for _ in range(5):
        client.key("RIGHT")

    # 5. Execute SAVE to write .m8s project to MicroSD card
    print("[*] Executing SAVE to SD Card...")
    client.key("EDIT")
    time.sleep(1.0)

    # Check for confirmation prompt (LOSE CHANGES / OVERWRITE)
    screen = client.get_text_screen()
    for line in screen:
        if "OK" in line and "CANCEL" in line:
            client.key("LEFT")
            client.key("EDIT")
            break

    print("[+] Project saved to /Songs/ on SD Card!")
    client.close()

if __name__ == "__main__":
    save_new_project_to_sd()
```

---

## 9. Troubleshooting & Best Practices

1. **Modal Menus Blocking Playback**:
   - If the M8 is in a modal file browser or instrument pool, pressing `SHIFT+PLAY` will not start song playback.
   - **Fix**: Use `escape_to_song_view()` (repeated `OPTION` + `SHIFT+LEFT`) to ensure `GET_STATE` returns `screen == "SONG"` before starting playback.

2. **Silent Audio Recordings**:
   - Ensure `m8c` was launched with the `--audio` flag so the M8 USB audio interface is bound by SDL.
   - Verify that the active track has a chain assigned, the phrase has notes, and the instrument type is set to an internal engine (`WAVSYNTH`, `MACROSYNTH`, `SAMPLER`) with volume/cutoff up.

3. **Log File Location**:
   - All session log files specified with `--log <filename>` are automatically placed into the `logs/` directory.

4. **Port Binding**:
   - If port `9123` is in use by a previous background instance, terminate remaining `m8c.exe` processes via Task Manager or `taskkill /F /IM m8c.exe`.
