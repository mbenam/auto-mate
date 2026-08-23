#!/usr/bin/env python3
"""
Interactive AI Agent Demo for m8c
Demonstrates:
- Feature A: Winsock2 TCP Server (127.0.0.1:9123)
- Feature B: Virtual Screen & Cursor Tracking (GET_STATE, GET_CURSOR, GET_TEXT_SCREEN)
- Feature C: Thread-Safe Raw RGB24 Screenshots (320x240)
- Feature D: Direct Audio to WAV Recording
- Feature E: Daemon / Headless Mode & Unified Activity Logger
"""
import socket
import subprocess
import time
import sys
import os
import wave
import json
import argparse

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
            chunk = self.sock.recv(min(16384, count - len(result)))
            if not chunk:
                break
            result.extend(chunk)
        return result

def run_demo(headless=False):
    m8c_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "Release", "m8c.exe"))
    if not os.path.exists(m8c_bin):
        m8c_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "m8c.exe"))

    mode_label = "Daemon / Headless Mode (Virtual Offscreen Display)" if headless else "Standard GUI Mode"
    print("=" * 78)
    print("       M8C AI AGENT FULL CAPABILITY DEMONSTRATION")
    print(f"       Running Mode: {mode_label}")
    print("=" * 78)
    print(f"[*] Starting m8c process from:\n    {m8c_bin}")
    
    log_file_path = "m8c_demo_session.log"
    if os.path.exists(log_file_path):
        try:
            os.remove(log_file_path)
        except OSError:
            pass

    cmd = [m8c_bin, "--log", log_file_path]
    if headless:
        cmd.append("--headless")

    proc = subprocess.Popen(cmd, cwd=os.path.dirname(m8c_bin))
    
    try:
        # Give m8c time to initialize SDL and bind the TCP server
        time.sleep(1.5)
        
        print("\n[*] Connecting to m8c AI Server at 127.0.0.1:9123...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        sock.connect(("127.0.0.1", 9123))
        reader = SocketReader(sock)
        print("[+] Connected successfully!\n")
        
        demo_steps = [
            ("Feature A/B: Healthcheck / Handshake", "PING\n"),
            ("Feature A/B: Navigate UP", "KEY UP\n"),
            ("Feature A/B: Navigate DOWN", "KEY DOWN\n"),
            ("Feature A/B: Navigate LEFT", "KEY LEFT\n"),
            ("Feature A/B: Navigate RIGHT", "KEY RIGHT\n"),
            ("Feature A/B: Press EDIT button", "KEY EDIT\n"),
            ("Feature A/B: Press OPTION button", "KEY OPTION\n"),
            ("Feature A/B: Combination: SHIFT + PLAY (Song Start)", "KEY SHIFT+PLAY\n"),
            ("Feature A/B: Combination: EDIT + OPT (Cut/Paste)", "KEY EDIT+OPT\n"),
            ("Feature A/B: Combination: Multi-button UP + LEFT + EDIT", "KEY UP+LEFT+EDIT\n"),
            ("Feature A/B: Error Handling: Non-existent key token", "KEY UNKNOWN_KEY\n"),
            ("Feature A/B: Error Handling: Unsupported command", "HELP\n"),
        ]
        
        for title, command in demo_steps:
            print(f"--- Demo Step: {title} ---")
            print(f"  [AI Agent -> m8c]  {command.strip()}")
            t0 = time.perf_counter()
            sock.sendall(command.encode("utf-8"))
            response = reader.read_line()
            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            print(f"  [m8c -> AI Agent]  {response}  ({elapsed_ms:.2f} ms)\n")
            time.sleep(0.05)

        print("--- Demo Step: Feature B - Virtual Screen State (GET_STATE / JSON) ---")
        print("  [AI Agent -> m8c]  GET_STATE")
        t0 = time.perf_counter()
        sock.sendall(b"GET_STATE\n")
        resp = reader.read_line()
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        print(f"  [m8c -> AI Agent]  {resp[:120]}... ({elapsed_ms:.2f} ms)")
        if resp.startswith("OK STATE "):
            try:
                state_obj = json.loads(resp[9:])
                print(f"     | Screen:       {state_obj.get('screen')}")
                print(f"     | Cursor:       Col {state_obj.get('cursor_col')}, Row {state_obj.get('cursor_row')}")
                print(f"     | Active Input: {state_obj.get('input')}")
                print(f"     | Value:        '{state_obj.get('value')}'")
                print(f"     | Header:       '{state_obj.get('header')}'")
                print(f"     | Cursor Line:  '{state_obj.get('cursor_text_line')}'")
            except Exception as e:
                print(f"     | JSON Parse Error: {e}")
        print()

        print("--- Demo Step: Feature B - Fast Cursor Query (GET_CURSOR) ---")
        print("  [AI Agent -> m8c]  GET_CURSOR")
        sock.sendall(b"GET_CURSOR\n")
        resp = reader.read_line()
        print(f"  [m8c -> AI Agent]  {resp}\n")

        print("--- Demo Step: Feature B - Full ASCII Screen Grid (GET_TEXT_SCREEN) ---")
        print("  [AI Agent -> m8c]  GET_TEXT_SCREEN")
        sock.sendall(b"GET_TEXT_SCREEN\n")
        hdr = reader.read_line()
        print(f"  [m8c -> AI Agent]  {hdr}")
        if "OK TEXT_SCREEN" in hdr:
            print("  +----------------------------------------+")
            for r in range(10): # Preview first 10 rows
                line = reader.read_line()
                print(f"  |{line.ljust(40)}|")
            for _ in range(20): # drain remaining rows
                reader.read_line()
            print("  |                ... [20 more rows] ...  |")
            print("  +----------------------------------------+\n")

        print("--- Demo Step: Feature C - Thread-Safe Raw Screenshot (320x240 RGB24) ---")
        print("  [AI Agent -> m8c]  SCREENSHOT")
        t0 = time.perf_counter()
        sock.sendall(b"SCREENSHOT\n")
        
        target_size = 320 * 240 * 3 # 230,400 bytes
        raw_pixels = reader.read_exact(target_size)
            
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        fps_capability = 1000.0 / elapsed_ms if elapsed_ms > 0 else 0
        print(f"  [m8c -> AI Agent]  Received {len(raw_pixels):,} bytes RGB24 in {elapsed_ms:.2f} ms (~{fps_capability:.1f} FPS capability)\n")

        print("--- Demo Step: Feature D - Direct Audio to WAV Recording ---")
        wav_file = os.path.abspath("demo_recording.wav")
        if os.path.exists(wav_file):
            try:
                os.remove(wav_file)
            except OSError:
                pass
            
        print(f"  [AI Agent -> m8c]  REC_START {wav_file}")
        sock.sendall(f"REC_START {wav_file}\n".encode("utf-8"))
        resp = reader.read_line()
        print(f"  [m8c -> AI Agent]  {resp}")
        
        print("  [*] Recording audio stream for 1.0 second...")
        time.sleep(1.0)
        
        print("  [AI Agent -> m8c]  REC_STOP")
        sock.sendall(b"REC_STOP\n")
        resp = reader.read_line()
        print(f"  [m8c -> AI Agent]  {resp}")
        
        if os.path.exists(wav_file):
            with wave.open(wav_file, "rb") as wf:
                channels = wf.getnchannels()
                width = wf.getsampwidth()
                rate = wf.getframerate()
                frames = wf.getnframes()
                duration = frames / rate if rate > 0 else 0
                print(f"  [+] Output WAV: {frames:,} frames ({duration:.2f}s) @ {rate}Hz Stereo 16-bit PCM (File size: {os.path.getsize(wav_file):,} bytes)\n")
            try:
                os.remove(wav_file)
            except OSError:
                pass

        print("--- Demo Step: Feature E - Live Activity Logger Query (LOGS) ---")
        print("  [AI Agent -> m8c]  LOGS 8")
        sock.sendall(b"LOGS 8\n")
        hdr_line = reader.read_line()
        print(f"  [m8c -> AI Agent]  {hdr_line}")
        if "OK LOGS" in hdr_line:
            parts = hdr_line.split()
            count = int(parts[2]) if len(parts) >= 3 else 0
            for i in range(count):
                entry = reader.read_line()
                print(f"     | {entry}")
        print()

        sock.close()
        print("[+] Full demo completed successfully!")
        
    finally:
        print("[*] Terminating m8c process...")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        print("[*] Clean shutdown verified.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="M8C AI Server Interactive Demo")
    parser.add_argument("--headless", "--daemon", "-d", action="store_true", help="Run m8c in headless/daemon mode (virtual display)")
    args = parser.parse_args()
    run_demo(headless=args.headless)
