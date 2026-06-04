from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Attitude:
    roll: float
    pitch: float
    yaw: float


def parse_attitude_line(line: str) -> Attitude | None:
    text = line.strip()
    if not text:
        return None

    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 4 or parts[0] != "ATT":
        return None

    try:
        roll = float(parts[1])
        pitch = float(parts[2])
        yaw = float(parts[3])
    except ValueError:
        return None

    return Attitude(roll=roll, pitch=pitch, yaw=yaw)
