from __future__ import annotations

import threading
import time
from typing import Optional

from attitude_state import AttitudeState
from protocol import parse_attitude_line


class SerialReader(threading.Thread):
    def __init__(self, port: str, baud: int, state: AttitudeState, stop_event: threading.Event) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.state = state
        self.stop_event = stop_event

    def run(self) -> None:
        try:
            import serial
        except ImportError:
            self.state.set_status("Disconnected", "Missing pyserial. Run pip install -r mpu_viewer/requirements.txt")
            return

        while not self.stop_event.is_set():
            try:
                with serial.Serial(self.port, self.baud, timeout=1) as ser:
                    self.state.set_status("Connected", f"{self.port} @ {self.baud}")
                    while not self.stop_event.is_set():
                        raw: bytes = ser.readline()
                        if not raw:
                            continue

                        line = raw.decode("utf-8", errors="ignore").strip()
                        attitude = parse_attitude_line(line)
                        if attitude is None:
                            if line == "STA,CAL":
                                self.state.set_status("Calibrating", "Keep the MPU6050 still")
                                continue
                            if line == "STA,OFF":
                                self.state.set_status("MPU Offline", "Check MPU6050 wiring and power")
                                continue
                            if line.startswith("ATT,"):
                                self.state.set_status("Data Error", line)
                            continue

                        self.state.update(attitude, source="serial", line=line)
            except Exception as exc:
                self.state.set_status("Disconnected", str(exc))
                self.stop_event.wait(1.0)


def start_serial_reader(
    port: Optional[str],
    baud: int,
    state: AttitudeState,
    stop_event: threading.Event,
) -> Optional[SerialReader]:
    if not port:
        state.set_status("Disconnected", "No serial port selected")
        return None

    reader = SerialReader(port=port, baud=baud, state=state, stop_event=stop_event)
    reader.start()
    return reader
