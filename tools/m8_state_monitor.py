#!/usr/bin/env python3
"""
m8_state_monitor.py - Real-Time Tkinter State Inspector for M8C
Polls GET_STATE and GET_TEXT_SCREEN from m8c every 500ms over TCP.
"""
import tkinter as tk
from tkinter import ttk, messagebox
import socket
import json
import time
import threading

class M8StateMonitorApp:
    def __init__(self, root):
        self.root = root
        self.root.title("M8C Real-Time State Monitor")
        self.root.geometry("980x680")
        self.root.minsize(800, 560)

        # Connection settings
        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="9123")
        self.poll_interval_ms = 500  # Poll every half a second
        self.is_connected = False
        self.sock = None
        self.buf = bytearray()
        self.sock_lock = threading.Lock()
        self.poll_active = False

        # State storage
        self.current_state = {}
        self.current_screen_lines = []

        self._setup_style()
        self._build_ui()

        # Handle window close cleanly
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        # Auto-connect attempt on launch
        self.root.after(300, self.toggle_connection)

    def _setup_style(self):
        self.root.configure(bg="#18181c")
        self.style = ttk.Style()
        try:
            self.style.theme_use("clam")
        except tk.TclError:
            pass

        self.style.configure(".", background="#18181c", foreground="#e0e0e0")
        self.style.configure("TFrame", background="#18181c")
        self.style.configure("TLabel", background="#18181c", foreground="#e0e0e0", font=("Segoe UI", 10))
        self.style.configure("Header.TLabel", font=("Segoe UI", 12, "bold"), foreground="#00d8ff")
        self.style.configure("Badge.TLabel", font=("Segoe UI", 10, "bold"), padding=4)
        self.style.configure("TButton", font=("Segoe UI", 9, "bold"), padding=4)
        self.style.configure("TEntry", fieldbackground="#24242a", foreground="#ffffff")

    def _build_ui(self):
        # 1. Top Connection Bar
        conn_frame = ttk.Frame(self.root, padding="10 8 10 8")
        conn_frame.pack(fill=tk.X, side=tk.TOP)

        ttk.Label(conn_frame, text="Host:").pack(side=tk.LEFT, padx=(0, 4))
        self.host_entry = ttk.Entry(conn_frame, textvariable=self.host_var, width=14)
        self.host_entry.pack(side=tk.LEFT, padx=(0, 10))

        ttk.Label(conn_frame, text="Port:").pack(side=tk.LEFT, padx=(0, 4))
        self.port_entry = ttk.Entry(conn_frame, textvariable=self.port_var, width=6)
        self.port_entry.pack(side=tk.LEFT, padx=(0, 15))

        self.btn_connect = tk.Button(
            conn_frame,
            text="Connect",
            command=self.toggle_connection,
            bg="#2a7a38",
            fg="#ffffff",
            activebackground="#349a46",
            activeforeground="#ffffff",
            font=("Segoe UI", 9, "bold"),
            relief=tk.FLAT,
            padx=12,
            pady=2,
            cursor="hand2"
        )
        self.btn_connect.pack(side=tk.LEFT, padx=(0, 15))

        self.lbl_status = tk.Label(
            conn_frame,
            text="● Disconnected",
            bg="#18181c",
            fg="#ff4444",
            font=("Segoe UI", 10, "bold")
        )
        self.lbl_status.pack(side=tk.LEFT, padx=(0, 15))

        self.lbl_poll_info = tk.Label(
            conn_frame,
            text="Polling interval: 500ms (0.5s)",
            bg="#18181c",
            fg="#888888",
            font=("Segoe UI", 9)
        )
        self.lbl_poll_info.pack(side=tk.RIGHT, padx=5)

        # Separator line
        sep = tk.Frame(self.root, height=1, bg="#33333d")
        sep.pack(fill=tk.X, padx=10, pady=2)

        # 2. Main Content Split View
        main_pane = ttk.Frame(self.root, padding=10)
        main_pane.pack(fill=tk.BOTH, expand=True)

        # Left Column: Virtual Screen Text Grid (40x30)
        left_frame = ttk.Frame(main_pane)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 8))

        screen_header_frame = ttk.Frame(left_frame)
        screen_header_frame.pack(fill=tk.X, pady=(0, 6))

        ttk.Label(screen_header_frame, text="M8 Virtual Screen (40x30 Grid)", style="Header.TLabel").pack(side=tk.LEFT)
        
        self.lbl_play_state = tk.Label(
            screen_header_frame,
            text="STOPPED",
            bg="#2a2a30",
            fg="#aaaaaa",
            font=("Segoe UI", 9, "bold"),
            padx=8,
            pady=2
        )
        self.lbl_play_state.pack(side=tk.RIGHT)

        # Text Widget for Displaying Screen Grid
        self.txt_screen = tk.Text(
            left_frame,
            bg="#0d0d11",
            fg="#00e5ff",
            insertbackground="#ffffff",
            font=("Consolas", 11),
            wrap=tk.NONE,
            relief=tk.FLAT,
            padx=10,
            pady=10,
            cursor="arrow"
        )
        self.txt_screen.pack(fill=tk.BOTH, expand=True)
        self.txt_screen.tag_configure("header", foreground="#ffea00", font=("Consolas", 11, "bold"))
        self.txt_screen.tag_configure("cursor", background="#005577", foreground="#ffffff", font=("Consolas", 11, "bold"))
        self.txt_screen.tag_configure("normal", foreground="#00e5ff")
        self.txt_screen.tag_configure("muted", foreground="#555566")
        self.txt_screen.config(state=tk.DISABLED)

        # Right Column: Structured JSON State & Metadata Inspector
        right_frame = ttk.Frame(main_pane, width=360)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, padx=(8, 0))
        right_frame.pack_propagate(False)

        ttk.Label(right_frame, text="State & Parameter Inspector", style="Header.TLabel").pack(anchor=tk.W, pady=(0, 6))

        # Inspector Card Table
        card = tk.Frame(right_frame, bg="#212128", padx=12, pady=10, relief=tk.FLAT)
        card.pack(fill=tk.X, pady=(0, 10))

        fields = [
            ("Active Screen:", "lbl_val_screen", "#00d8ff"),
            ("Header / Mode:", "lbl_val_header", "#ffea00"),
            ("Cursor Grid:", "lbl_val_cursor", "#00ff99"),
            ("Focused Field:", "lbl_val_input", "#ff8800"),
            ("Current Value:", "lbl_val_value", "#ffffff"),
            ("Play Engine:", "lbl_val_play", "#00d8ff"),
            ("Last Query:", "lbl_val_time", "#888888"),
        ]

        self.val_labels = {}
        for r, (title, attr_name, fg_color) in enumerate(fields):
            tk.Label(
                card,
                text=title,
                bg="#212128",
                fg="#a0a0b0",
                font=("Segoe UI", 9, "bold"),
                anchor=tk.W
            ).grid(row=r, column=0, sticky=tk.W, pady=3)

            val_lbl = tk.Label(
                card,
                text="--",
                bg="#212128",
                fg=fg_color,
                font=("Segoe UI", 10, "bold" if r < 5 else "normal"),
                anchor=tk.W
            )
            val_lbl.grid(row=r, column=1, sticky=tk.W, padx=(10, 0), pady=3)
            self.val_labels[attr_name] = val_lbl

        card.columnconfigure(1, weight=1)

        # Cursor Line Preview Card
        ttk.Label(right_frame, text="Cursor Row Content:", style="TLabel").pack(anchor=tk.W, pady=(4, 2))
        self.lbl_cursor_line = tk.Label(
            right_frame,
            text="--",
            bg="#212128",
            fg="#00ff99",
            font=("Consolas", 10),
            padx=8,
            pady=6,
            anchor=tk.W,
            relief=tk.FLAT
        )
        self.lbl_cursor_line.pack(fill=tk.X, pady=(0, 10))

        # Raw JSON State Tree / Display
        ttk.Label(right_frame, text="Raw GET_STATE JSON:", style="TLabel").pack(anchor=tk.W, pady=(4, 2))
        self.txt_json = tk.Text(
            right_frame,
            bg="#0d0d11",
            fg="#cccccc",
            font=("Consolas", 9),
            height=10,
            relief=tk.FLAT,
            padx=6,
            pady=6
        )
        self.txt_json.pack(fill=tk.BOTH, expand=True)
        self.txt_json.config(state=tk.DISABLED)

        # 3. Bottom Quick Navigation Bar
        bottom_frame = ttk.Frame(self.root, padding="10 6 10 8")
        bottom_frame.pack(fill=tk.X, side=tk.BOTTOM)

        ttk.Label(bottom_frame, text="Quick Remote Controls:").pack(side=tk.LEFT, padx=(0, 10))

        controls = [
            ("▲ UP", "UP"),
            ("▼ DOWN", "DOWN"),
            ("◀ LEFT", "LEFT"),
            ("▶ RIGHT", "RIGHT"),
            ("EDIT (X)", "EDIT"),
            ("OPT (Z)", "OPTION"),
            ("SHIFT", "SHIFT"),
            ("PLAY / STOP", "PLAY"),
            ("SHIFT+PLAY", "SHIFT+PLAY")
        ]

        for label, cmd_combo in controls:
            btn = tk.Button(
                bottom_frame,
                text=label,
                command=lambda c=cmd_combo: self.send_key(c),
                bg="#2a2a34",
                fg="#ffffff",
                activebackground="#3e3e4c",
                activeforeground="#ffffff",
                font=("Segoe UI", 8, "bold"),
                relief=tk.FLAT,
                padx=6,
                pady=2,
                cursor="hand2"
            )
            btn.pack(side=tk.LEFT, padx=3)

    def toggle_connection(self):
        if self.is_connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        host = self.host_var.get().strip()
        try:
            port = int(self.port_var.get().strip())
        except ValueError:
            messagebox.showerror("Invalid Port", "Port must be an integer (e.g. 9123)")
            return

        self.lbl_status.config(text="● Connecting...", fg="#ffaa00")
        self.root.update_idletasks()

        threading.Thread(target=self._async_connect, args=(host, port), daemon=True).start()

    def _async_connect(self, host, port):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect((host, port))
            with self.sock_lock:
                self.sock = s
            self.root.after(0, self._on_connect_success)
        except Exception as e:
            self.root.after(0, lambda err=e: self._on_connect_failure(err))

    def _on_connect_success(self):
        self.is_connected = True
        self.lbl_status.config(text="● Connected (127.0.0.1:9123)", fg="#00ff66")
        self.btn_connect.config(text="Disconnect", bg="#992222", activebackground="#bb3333")
        self.host_entry.config(state=tk.DISABLED)
        self.port_entry.config(state=tk.DISABLED)

        # Start periodic polling loop (every 500ms)
        self.poll_active = True
        self._schedule_poll()

    def _on_connect_failure(self, error):
        self.is_connected = False
        self.lbl_status.config(text=f"● Disconnected ({error})", fg="#ff4444")
        self.btn_connect.config(text="Connect", bg="#2a7a38", activebackground="#349a46")
        self.host_entry.config(state=tk.NORMAL)
        self.port_entry.config(state=tk.NORMAL)

    def disconnect(self):
        self.poll_active = False
        with self.sock_lock:
            if self.sock:
                try:
                    self.sock.close()
                except OSError:
                    pass
                self.sock = None
        self.is_connected = False
        self.lbl_status.config(text="● Disconnected", fg="#ff4444")
        self.btn_connect.config(text="Connect", bg="#2a7a38", activebackground="#349a46")
        self.host_entry.config(state=tk.NORMAL)
        self.port_entry.config(state=tk.NORMAL)
        self.lbl_play_state.config(text="STOPPED", bg="#2a2a30", fg="#aaaaaa")

    def _send_cmd_locked(self, cmd_str):
        with self.sock_lock:
            if not self.sock:
                return None
            try:
                if not cmd_str.endswith("\n"):
                    cmd_str += "\n"
                self.sock.sendall(cmd_str.encode("utf-8"))
                
                # Read line response using persistent buffer helper
                line = self._recv_line_locked()
                return line
            except Exception:
                return None

    def _recv_line_locked(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            self.buf.extend(chunk)
        idx = self.buf.index(b"\n")
        line = self.buf[:idx].decode("utf-8", errors="replace").strip("\r\n")
        del self.buf[:idx + 1]
        return line

    def _schedule_poll(self):
        if not self.poll_active or not self.is_connected:
            return
        threading.Thread(target=self._async_query_state, daemon=True).start()
        # Schedule next iteration in exactly 500ms
        self.root.after(self.poll_interval_ms, self._schedule_poll)

    def _async_query_state(self):
        with self.sock_lock:
            if not self.sock:
                self.root.after(0, self.disconnect)
                return

            try:
                # 1. Fetch GET_STATE
                self.sock.sendall(b"GET_STATE\n")
                state_resp = self._recv_line_locked()
                if not state_resp:
                    self.root.after(0, self.disconnect)
                    return

                state_data = {}
                if state_resp.startswith("OK STATE "):
                    try:
                        state_data = json.loads(state_resp[9:])
                    except Exception:
                        pass

                # 2. Fetch GET_TEXT_SCREEN MARKED
                self.sock.sendall(b"GET_TEXT_SCREEN MARKED\n")
                hdr = self._recv_line_locked()
                screen_lines = []
                if hdr and "OK TEXT_SCREEN" in hdr:
                    for _ in range(30):
                        line_str = self._recv_line_locked()
                        if line_str is not None:
                            screen_lines.append(line_str)

                # Update GUI on main thread
                self.root.after(0, lambda s=state_data, lines=screen_lines: self._update_ui_state(s, lines))
            except Exception:
                self.root.after(0, self.disconnect)

    def _update_ui_state(self, state, screen_lines):
        self.current_state = state
        self.current_screen_lines = screen_lines

        # Update Value Labels
        self.val_labels["lbl_val_screen"].config(text=state.get("screen", "UNKNOWN"))
        self.val_labels["lbl_val_header"].config(text=state.get("header", "--"))
        col = state.get("cursor_col", -1)
        row = state.get("cursor_row", -1)
        self.val_labels["lbl_val_cursor"].config(text=f"Col: {col:02d}, Row: {row:02d}" if col >= 0 else "None (-1, -1)")
        self.val_labels["lbl_val_input"].config(text=state.get("input", "NO_CURSOR"))
        self.val_labels["lbl_val_value"].config(text=state.get("value", "--") if state.get("value") else "(empty)")
        
        play_state = state.get("play_state", "STOPPED")
        self.val_labels["lbl_val_play"].config(text=play_state)
        if play_state == "PLAYING":
            self.lbl_play_state.config(text="▶ PLAYING", bg="#1b4d24", fg="#00ff66")
        else:
            self.lbl_play_state.config(text="⏹ STOPPED", bg="#2a2a30", fg="#888888")

        self.val_labels["lbl_val_time"].config(text=time.strftime("%H:%M:%S") + f".{int(time.time()*1000)%1000:03d}")

        # Update Cursor Line Preview
        cursor_line = state.get("cursor_text_line", "")
        self.lbl_cursor_line.config(text=cursor_line if cursor_line else "(no active cursor row)")

        # Update Raw JSON viewer
        self.txt_json.config(state=tk.NORMAL)
        self.txt_json.delete("1.0", tk.END)
        self.txt_json.insert(tk.END, json.dumps(state, indent=2))
        self.txt_json.config(state=tk.DISABLED)

        # Update Virtual Screen Grid with precise bracket token highlighting
        if screen_lines:
            self.txt_screen.config(state=tk.NORMAL)
            self.txt_screen.delete("1.0", tk.END)
            for idx, line in enumerate(screen_lines):
                if idx == 0:
                    self.txt_screen.insert(tk.END, line + "\n", "header")
                elif "[" in line and "]" in line:
                    p1 = line.index("[")
                    p2 = line.index("]")
                    pre = line[:p1]
                    mid = line[p1:p2+1]
                    post = line[p2+1:]
                    
                    self.txt_screen.insert(tk.END, pre, "normal")
                    self.txt_screen.insert(tk.END, mid, "cursor")
                    self.txt_screen.insert(tk.END, post + "\n", "normal")
                elif "--" in line and not line.strip().replace("-", "").isalnum():
                    self.txt_screen.insert(tk.END, line + "\n", "muted")
                else:
                    self.txt_screen.insert(tk.END, line + "\n", "normal")

            self.txt_screen.config(state=tk.DISABLED)

    def send_key(self, combo):
        if not self.is_connected:
            return
        threading.Thread(target=lambda: self._send_cmd_locked(f"KEY {combo}"), daemon=True).start()

    def on_close(self):
        self.disconnect()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = M8StateMonitorApp(root)
    root.mainloop()

if __name__ == "__main__":
    main()
