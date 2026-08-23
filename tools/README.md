# M8C Developer & Automation Tools

This directory contains standalone utility scripts and real-time monitoring tools for interacting with `m8c`.

---

## 1. M8 Real-Time State Monitor (`m8_state_monitor.py`)

A graphical Tkinter application that connects to `m8c` over TCP port `9123` and polls **`GET_STATE`** and **`GET_TEXT_SCREEN`** every **500ms (half a second)**.

### Features
- **Real-Time $40 \times 30$ Virtual Screen**: Renders the exact M8 text screen with bracketed cursor highlights `[ ]`.
- **State & Parameter Inspector**: Displays active screen type, header, focused input field name, current value, play engine status, and cursor coordinates.
- **Raw JSON Viewer**: Formatted JSON tree of the live `GET_STATE` response.
- **Quick Controls**: Remote buttons for navigating and triggering playback (`UP`, `DOWN`, `LEFT`, `RIGHT`, `EDIT`, `OPTION`, `SHIFT`, `PLAY`, `SHIFT+PLAY`).
- **Connection Controls**: Connect/disconnect toggle with status indicators and customizable host/port settings.

### How to Run

1. Start `m8c` in either GUI or Headless mode:
   ```powershell
   # In terminal 1:
   .\build\Release\m8c.exe --headless
   ```

2. Launch the State Monitor:
   ```powershell
   # In terminal 2:
   python tools/m8_state_monitor.py
   ```
