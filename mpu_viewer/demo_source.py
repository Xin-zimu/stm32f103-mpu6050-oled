from __future__ import annotations

import math
import threading
import time

from attitude_state import AttitudeState
from protocol import Attitude


class DemoSource(threading.Thread):
    def __init__(self, state: AttitudeState, stop_event: threading.Event) -> None:
        super().__init__(daemon=True)
        self.state = state
        self.stop_event = stop_event

    def run(self) -> None:
        started = time.time()

        while not self.stop_event.is_set():
            t = time.time() - started
            attitude = Attitude(
                roll=32.0 * math.sin(t * 0.9),
                pitch=22.0 * math.sin(t * 0.6 + 0.8),
                yaw=((t * 28.0) % 360.0) - 180.0,
            )
            line = (
                f"ATT,{round(attitude.roll * 10)},"
                f"{round(attitude.pitch * 10)},"
                f"{round(attitude.yaw * 10)}"
            )
            self.state.update(attitude, source="demo", line=line)
            self.stop_event.wait(0.05)
