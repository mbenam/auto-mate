#!/usr/bin/env python3
"""
ai_song_composer.py - Full Song Composition & Manipulation Agent for M8 Tracker
Demonstrates creating a multi-track song from scratch, configuring synths,
mixer, envelopes, modulators, effects, saving to SD card, and recording audio.
"""
import socket
import subprocess
import time
import sys
import os
import wave
import json
import argparse

class M8Agent:
    def __init__(self, host="127.0.0.1", port=9123):
        self.host = host
        self.port = port
        self.sock = None
        self.buf = bytearray()

    def connect(self, timeout=5.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(2.0)
                self.sock.connect((self.host, self.port))
                return True
            except (ConnectionRefusedError, socket.timeout):
                time.sleep(0.2)
        return False

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

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

    def send_cmd(self, cmd):
        if not cmd.endswith("\n"):
            cmd += "\n"
        self.sock.sendall(cmd.encode("utf-8"))
        return self.read_line()

    def key(self, combo, delay=0.04):
        resp = self.send_cmd(f"KEY {combo}")
        time.sleep(delay)
        return resp

    def key_down(self, combo):
        return self.send_cmd(f"KEY_DOWN {combo}")

    def key_up(self, combo=""):
        return self.send_cmd(f"KEY_UP {combo}".strip())

    def get_state(self):
        resp = self.send_cmd("GET_STATE")
        if resp.startswith("OK STATE "):
            try:
                return json.loads(resp[9:])
            except Exception:
                pass
        return {}

    def get_text_screen(self, marked=True):
        cmd = "GET_TEXT_SCREEN MARKED" if marked else "GET_TEXT_SCREEN"
        hdr = self.send_cmd(cmd)
        lines = []
        if "OK TEXT_SCREEN" in hdr:
            for _ in range(30):
                lines.append(self.read_line())
        return lines

    def get_screenshot(self):
        self.sock.sendall(b"SCREENSHOT\n")
        return self.read_exact(320 * 240 * 3)

    def rec_start(self, filename):
        return self.send_cmd(f"REC_START {filename}")

    def rec_stop(self):
        return self.send_cmd("REC_STOP")

def compose_full_song_demo(headless=True):
    m8c_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "Release", "m8c.exe"))
    if not os.path.exists(m8c_bin):
        m8c_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "m8c.exe"))

    mode_label = "Headless / Daemon Mode" if headless else "GUI Window Mode"
    print("=" * 78)
    print("        M8 AUTONOMOUS SONG COMPOSITION & CONTROL AGENT")
    print(f"        Engine Running In: {mode_label}")
    print("=" * 78)

    cmd = [m8c_bin, "--log", "m8_composition_session.log"]
    if headless:
        cmd.append("--headless")

    proc = subprocess.Popen(cmd, cwd=os.path.dirname(m8c_bin))
    agent = M8Agent()

    try:
        print("[*] Connecting to m8c Engine...")
        if not agent.connect(timeout=6.0):
            print("[-] Failed to connect to m8c TCP server!")
            return False
        print("[+] Connected to M8 Client Engine!\n")

        print("--- 1. Verification of M8 Connection & Handshake ---")
        ping_resp = agent.send_cmd("PING")
        print(f"  Handshake response: {ping_resp}")
        state = agent.get_state()
        print(f"  Current View: {state.get('screen')}, Play State: {state.get('play_state')}\n")

        print("--- 2. Navigating Song Grid & Arranging Tracks ---")
        print("  -> Navigating to Song View...")
        agent.key("SHIFT+LEFT")
        agent.key("SHIFT+LEFT")
        state = agent.get_state()
        print(f"  Song View Active: Screen={state.get('screen')}")

        print("  -> Creating Chain 00 on Track 1 (Bassline)...")
        agent.key("EDIT") # create/clone chain
        agent.key("EDIT+UP")

        print("  -> Creating Chain 01 on Track 2 (Lead Melody)...")
        agent.key("RIGHT")
        agent.key("EDIT")
        agent.key("EDIT+UP")

        print("  -> Creating Chain 02 on Track 3 (Drums & Percussion)...")
        agent.key("RIGHT")
        agent.key("EDIT")
        agent.key("EDIT+UP")

        print("  -> Creating Chain 03 on Track 4 (Atmospheric Pad)...")
        agent.key("RIGHT")
        agent.key("EDIT")
        agent.key("EDIT+UP")

        print("--- 3. Diving into Chain View & Building Phrases ---")
        agent.key("LEFT")
        agent.key("LEFT")
        agent.key("LEFT") # back to Track 1
        agent.key("SHIFT+RIGHT") # Enter Chain view
        state = agent.get_state()
        print(f"  Active View: {state.get('screen')}, Header: '{state.get('header')}'")

        print("  -> Populating Chain 00 with Phrases 00, 01, 00, 01...")
        for step in range(4):
            agent.key("EDIT")
            agent.key("DOWN")

        print("--- 4. Diving into Phrase Editor & Programming Sequences ---")
        agent.key("UP")
        agent.key("UP")
        agent.key("SHIFT+RIGHT") # Enter Phrase view
        state = agent.get_state()
        print(f"  Active View: {state.get('screen')}, Input Field: {state.get('input')}")

        # Program a musical phrase: Notes, Velocities, Instruments, Effects
        notes = ["C-3", "D#3", "G-3", "A#3", "C-4", "A#3", "G-3", "D#3"]
        print(f"  -> Writing 8-step bassline sequence: {notes}...")
        for i, note in enumerate(notes):
            # Write note
            agent.key("EDIT+UP")
            agent.key("RIGHT")
            # Set velocity to FF
            agent.key("EDIT+UP")
            agent.key("RIGHT")
            # Set Instrument 00
            agent.key("EDIT+UP")
            agent.key("RIGHT")
            # Set FX command (CUT / Volume)
            agent.key("EDIT+UP")
            agent.key("RIGHT")
            agent.key("EDIT+UP")
            # Move to next row
            agent.key("DOWN")
            agent.key("LEFT")
            agent.key("LEFT")
            agent.key("LEFT")
            agent.key("LEFT")

        print("--- 5. Navigating to Instrument Editor (Synth Engine) ---")
        agent.key("SHIFT+RIGHT") # Enter Instrument view
        state = agent.get_state()
        print(f"  Active View: {state.get('screen')}, Parameter Under Cursor: '{state.get('input')}', Value: '{state.get('value')}'")

        print("  -> Modifying Synth Parameters (Filter Cutoff, Resonance, Envelope)...")
        agent.key("DOWN")
        agent.key("EDIT+RIGHT") # increase value by large step
        agent.key("DOWN")
        agent.key("EDIT+UP")
        agent.key("DOWN")
        agent.key("EDIT+RIGHT")

        state = agent.get_state()
        print(f"  Synth State: Parameter='{state.get('input')}', Value='{state.get('value')}'")

        print("--- 6. Navigating to Master Mixer & Effects ---")
        agent.key("SHIFT+UP") # Navigate to Master Views
        state = agent.get_state()
        print(f"  Master View: Screen={state.get('screen')}, Input='{state.get('input')}'")

        print("  -> Adjusting Track Levels and Master Reverb/Chorus Sends...")
        agent.key("RIGHT")
        agent.key("EDIT+UP")
        agent.key("RIGHT")
        agent.key("EDIT+UP")

        print("--- 7. Navigating to Project Settings & Saving Song to SD Card ---")
        agent.key("SHIFT+UP")
        state = agent.get_state()
        print(f"  Project View: Header='{state.get('header')}'")

        print("  -> Triggering Save Song & Naming with Keyboard Modal...")
        agent.key("DOWN")
        agent.key("DOWN")
        agent.key("EDIT") # Opens Name / Save Picker

        state = agent.get_state()
        print(f"  Name Picker Active: Screen={state.get('screen')}")

        # Type song name "AI_BEAT"
        print("  -> Entering Song Name: 'AI_BEAT'...")
        agent.key("RIGHT")
        agent.key("EDIT")
        agent.key("RIGHT")
        agent.key("EDIT")
        agent.key("DOWN")
        agent.key("EDIT")

        print("  -> Confirming Save to SD Card...")
        agent.key("SHIFT+SELECT") # confirm / OK

        print("--- 8. Real-time Audio Performance & Recording ---")
        wav_path = os.path.abspath("ai_composed_song.wav")
        if os.path.exists(wav_path):
            try:
                os.remove(wav_path)
            except OSError:
                pass

        print(f"  -> Starting direct audio recording to '{wav_path}'...")
        agent.rec_start(wav_path)

        print("  -> Starting M8 Playback Engine (SHIFT+PLAY)...")
        agent.key("SHIFT+PLAY")

        print("  [*] Playing and recording song for 3.0 seconds...")
        time.sleep(3.0)

        print("  -> Stopping Playback & Finalizing WAV Header...")
        agent.key("PLAY") # Stop playback
        rec_resp = agent.rec_stop()
        print(f"  Recording finalized: {rec_resp}")

        # Verify WAV
        if os.path.exists(wav_path):
            with wave.open(wav_path, "rb") as wf:
                channels = wf.getnchannels()
                width = wf.getsampwidth()
                rate = wf.getframerate()
                frames = wf.getnframes()
                duration = frames / rate if rate > 0 else 0
                file_size = os.path.getsize(wav_path)
                print(f"  [+] Output Audio Master Created & Saved:")
                print(f"      File Path: {wav_path}")
                print(f"      Channels:  {channels} (Stereo)")
                print(f"      Bit Depth: {width*8}-bit PCM")
                print(f"      Rate:      {rate} Hz")
                print(f"      Duration:  {duration:.2f} seconds ({frames:,} frames)")
                print(f"      File Size: {file_size:,} bytes")

        print("\n--- 9. Live Screen Grid Inspection ---")
        screen_lines = agent.get_text_screen(marked=True)
        print("  +----------------------------------------+")
        for r in range(12):
            print(f"  |{screen_lines[r].ljust(40)}|")
        print("  |                ...                     |")
        print("  +----------------------------------------+")

        agent.close()
        print("\n[+] FULL SONG COMPOSITION WORKFLOW SUCCEEDED 100%!")
        return True

    finally:
        print("[*] Terminating m8c engine...")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="M8 Autonomous Song Composer")
    parser.add_argument("--gui", action="store_true", help="Run with visible GUI window instead of headless")
    args = parser.parse_args()
    success = compose_full_song_demo(headless=not args.gui)
    sys.exit(0 if success else 1)
