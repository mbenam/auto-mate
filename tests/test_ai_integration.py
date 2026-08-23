#!/usr/bin/env python3
"""
Integration test script for m8c AI Server over TCP.
Tests keystrokes, combinations, screenshots, audio recording, daemon mode, and unified logger.
"""
import socket
import subprocess
import time
import sys
import os
import wave

def run_test_suite(headless=False, log_filename="test_ai_integ.log"):
    m8c_bin = os.path.join(os.path.dirname(__file__), "..", "build", "Release", "m8c.exe")
    if not os.path.exists(m8c_bin):
        m8c_bin = os.path.join(os.path.dirname(__file__), "..", "build", "m8c.exe")
    
    m8c_bin = os.path.abspath(m8c_bin)
    if not os.path.exists(m8c_bin):
        print(f"Error: m8c binary not found at {m8c_bin}")
        return False

    mode_str = "Headless / Daemon" if headless else "GUI"
    print(f"\n==========================================")
    print(f"Starting m8c in {mode_str} Mode...")
    print(f"==========================================")

    if os.path.exists(log_filename):
        try:
            os.remove(log_filename)
        except OSError:
            pass

    cmd = [m8c_bin]
    if headless:
        cmd.extend(["--headless", "--log", log_filename])
    else:
        cmd.extend(["--log", log_filename])

    proc = subprocess.Popen(cmd, cwd=os.path.dirname(m8c_bin))

    try:
        # Give server time to bind 127.0.0.1:9123
        time.sleep(1.5)

        print("Connecting to TCP 127.0.0.1:9123...")
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect(("127.0.0.1", 9123))
        print("Connected!")

        class SocketReader:
            def __init__(self, sock):
                self.sock = sock
                self.buf = bytearray()

            def read_line(self):
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

            def read_exact(self, count):
                result = bytearray()
                if self.buf:
                    take = min(len(self.buf), count)
                    result.extend(self.buf[:take])
                    del self.buf[:take]
                while len(result) < count:
                    chunk = self.sock.recv(min(8192, count - len(result)))
                    if not chunk:
                        break
                    result.extend(chunk)
                return result

        reader = SocketReader(s)

        test_cases = [
            ("PING\n", "PONG"),
            ("KEY UP\n", "OK KEY 0x40"),
            ("KEY DOWN\n", "OK KEY 0x20"),
            ("KEY LEFT\n", "OK KEY 0x80"),
            ("KEY RIGHT\n", "OK KEY 0x04"),
            ("KEY EDIT\n", "OK KEY 0x01"),
            ("KEY OPT\n", "OK KEY 0x02"),
            ("KEY OPTION\n", "OK KEY 0x02"),
            ("KEY SHIFT\n", "OK KEY 0x10"),
            ("KEY SELECT\n", "OK KEY 0x10"),
            ("KEY PLAY\n", "OK KEY 0x08"),
            ("KEY START\n", "OK KEY 0x08"),
            ("KEY SHIFT+PLAY\n", "OK KEY 0x18"),
            ("KEY EDIT+OPT\n", "OK KEY 0x03"),
            ("KEY UP+LEFT+EDIT\n", "OK KEY 0xC1"),
            ("KEY FOOBAR\n", "ERROR Unknown key token 'FOOBAR'"),
            ("UNKNOWN_COMMAND\n", "ERROR Unknown command"),
        ]

        passed = 0
        total = len(test_cases)

        for cmd_str, expected in test_cases:
            s.sendall(cmd_str.encode("utf-8"))
            resp = reader.read_line()
            if expected in resp:
                print(f"  [PASS] '{cmd_str.strip()}' -> '{resp}'")
                passed += 1
            else:
                print(f"  [FAIL] '{cmd_str.strip()}' -> '{resp}' (expected '{expected}')")

        # Test GET_STATE command over TCP (JSON parsing)
        print("Testing GET_STATE command over TCP...")
        s.sendall(b"GET_STATE\n")
        state_resp = reader.read_line()
        total += 1
        if state_resp.startswith("OK STATE "):
            json_str = state_resp[9:]
            import json
            try:
                state_data = json.loads(json_str)
                print(f"  [PASS] 'GET_STATE' -> Screen={state_data.get('screen')}, Input={state_data.get('input')}, Cursor=({state_data.get('cursor_col')}, {state_data.get('cursor_row')})")
                passed += 1
            except Exception as e:
                print(f"  [FAIL] 'GET_STATE' invalid JSON: {e}")
        else:
            print(f"  [FAIL] 'GET_STATE' -> '{state_resp}'")

        # Test GET_CURSOR command over TCP
        print("Testing GET_CURSOR command over TCP...")
        s.sendall(b"GET_CURSOR\n")
        cursor_resp = reader.read_line()
        total += 1
        if cursor_resp.startswith("OK CURSOR"):
            print(f"  [PASS] 'GET_CURSOR' -> '{cursor_resp}'")
            passed += 1
        else:
            print(f"  [FAIL] 'GET_CURSOR' -> '{cursor_resp}'")

        # Test GET_TEXT_SCREEN command over TCP
        print("Testing GET_TEXT_SCREEN command over TCP...")
        s.sendall(b"GET_TEXT_SCREEN\n")
        text_hdr = reader.read_line()
        total += 1
        if "OK TEXT_SCREEN 30" in text_hdr:
            screen_lines = [reader.read_line() for _ in range(30)]
            print(f"  [PASS] 'GET_TEXT_SCREEN' -> Received 30 grid lines")
            passed += 1
        else:
            print(f"  [FAIL] 'GET_TEXT_SCREEN' -> '{text_hdr}'")

        # Test LOGS command over TCP
        print("Testing LOGS command over TCP...")
        s.sendall(b"LOGS 5\n")
        hdr_line = reader.read_line()
        total += 1
        if "OK LOGS" in hdr_line:
            parts = hdr_line.split()
            count = int(parts[2]) if len(parts) >= 3 else 0
            log_lines = [reader.read_line() for _ in range(count)]
            print(f"  [PASS] 'LOGS 5' -> {hdr_line} ({len(log_lines)} log entries)")
            passed += 1
        else:
            print(f"  [FAIL] 'LOGS 5' -> '{hdr_line}'")

        # Test SCREENSHOT command (230,400 raw RGB bytes)
        print("Testing SCREENSHOT command...")
        s.sendall(b"SCREENSHOT\n")
        target_size = 320 * 240 * 3 # 230,400 bytes
        raw_pixels = reader.read_exact(target_size)

        total += 1
        if len(raw_pixels) == target_size:
            print(f"  [PASS] 'SCREENSHOT' -> Received {len(raw_pixels)} bytes (320x240 RGB24)")
            passed += 1
        else:
            print(f"  [FAIL] 'SCREENSHOT' -> Received {len(raw_pixels)} bytes, expected {target_size}")

        # Test Feature D: Direct Audio Recording
        print("Testing Audio Recording (REC_START / REC_STOP)...")
        wav_path = os.path.abspath("test_integ_audio.wav")
        if os.path.exists(wav_path):
            try:
                os.remove(wav_path)
            except OSError:
                pass

        s.sendall(f"REC_START {wav_path}\n".encode("utf-8"))
        resp = reader.read_line()
        total += 1
        if "OK REC_START" in resp:
            print(f"  [PASS] 'REC_START' -> '{resp}'")
            passed += 1
        else:
            print(f"  [FAIL] 'REC_START' -> '{resp}'")

        time.sleep(0.3)

        s.sendall(b"REC_STOP\n")
        resp = reader.read_line()
        total += 1
        if "OK REC_STOP" in resp:
            print(f"  [PASS] 'REC_STOP' -> '{resp}'")
            passed += 1
        else:
            print(f"  [FAIL] 'REC_STOP' -> '{resp}'")

        # Verify WAV header
        total += 1
        if os.path.exists(wav_path):
            with wave.open(wav_path, "rb") as wf:
                channels = wf.getnchannels()
                sampwidth = wf.getsampwidth()
                framerate = wf.getframerate()
                if channels == 2 and sampwidth == 2 and framerate == 44100:
                    print(f"  [PASS] Valid WAV header (Channels={channels}, Width={sampwidth}, Rate={framerate}Hz)")
                    passed += 1
                else:
                    print(f"  [FAIL] Invalid WAV parameters: channels={channels}, sampwidth={sampwidth}, rate={framerate}")
            try:
                os.remove(wav_path)
            except OSError:
                pass
        else:
            print("  [FAIL] Output WAV file was not found on disk")

        s.close()
        print(f"\nMode Summary ({mode_str}): {passed}/{total} passed.")
        return passed == total

    finally:
        print("Terminating m8c...")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        
        # Verify log file creation in logs/ folder on disk
        full_log_path = os.path.join(os.path.dirname(m8c_bin), "logs", log_filename)
        if not os.path.exists(full_log_path):
            full_log_path = os.path.join(os.path.dirname(m8c_bin), log_filename)
        if os.path.exists(full_log_path):
            with open(full_log_path, "r") as f:
                content = f.read()
                print(f"Log file verified ({len(content)} bytes at {full_log_path})")
            try:
                os.remove(full_log_path)
            except OSError:
                pass

def main():
    print("==========================================")
    print(" M8C AI SERVER FULL INTEGRATION TEST SUITE")
    print("==========================================")
    
    # 1. Test GUI Mode
    gui_ok = run_test_suite(headless=False, log_filename="m8c_gui_test.log")
    
    # 2. Test Headless / Daemon Mode
    daemon_ok = run_test_suite(headless=True, log_filename="m8c_daemon_test.log")

    print("\n==========================================")
    print(f"FINAL RESULT: GUI Mode: {'PASS' if gui_ok else 'FAIL'} | Daemon Mode: {'PASS' if daemon_ok else 'FAIL'}")
    print("==========================================")
    return gui_ok and daemon_ok

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
