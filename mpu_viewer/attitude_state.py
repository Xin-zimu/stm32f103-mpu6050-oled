from __future__ import annotations

import threading
import time
from dataclasses import asdict, dataclass

from protocol import Attitude


@dataclass
class Snapshot:
    roll: float = 0.0
    pitch: float = 0.0
    yaw: float = 0.0
    status: str = "Disconnected"
    source: str = "idle"
    last_line: str = ""
    message: str = ""
    frames: int = 0
    updated_at: float = 0.0


class AttitudeState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = Snapshot(updated_at=time.time())

    def set_status(self, status: str, message: str = "") -> None:
        with self._lock:
            self._snapshot.status = status
            self._snapshot.message = message
            self._snapshot.updated_at = time.time()

    def update(self, attitude: Attitude, source: str, line: str = "") -> None:
        with self._lock:
            self._snapshot.roll = attitude.roll
            self._snapshot.pitch = attitude.pitch
            self._snapshot.yaw = attitude.yaw
            self._snapshot.status = "Connected" if source == "serial" else "Demo"
            self._snapshot.source = source
            self._snapshot.last_line = line
            self._snapshot.frames += 1
            self._snapshot.updated_at = time.time()

    def to_dict(self) -> dict[str, object]:
        with self._lock:
            data = asdict(self._snapshot)

        data["age_ms"] = int((time.time() - float(data["updated_at"])) * 1000)
        return data
