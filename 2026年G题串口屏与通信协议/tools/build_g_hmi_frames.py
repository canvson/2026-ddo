from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, List


SOF = 0xAA
EOF = 0x55

SET_PERIOD = 0x31
SET_VIEW = 0x32

VIEW_PARAM = 0
VIEW_WAVE = 1
VIEW_SPEC = 2


def checksum(cmd: int, data: Iterable[int]) -> int:
    payload = list(data)
    return (SOF + cmd + len(payload) + sum(payload)) & 0xFF


def frame(cmd: int, data: Iterable[int] = ()) -> List[int]:
    payload = list(data)
    return [SOF, cmd, len(payload), *payload, checksum(cmd, payload), EOF]


def hexs(values: Iterable[int]) -> str:
    return " ".join(f"{v:02X}" for v in values)


@dataclass(frozen=True)
class Case:
    name: str
    desc: str
    data: List[int]


CASES = [
    Case("SET_PERIOD_1", "select 1-period waveform window", frame(SET_PERIOD, [1])),
    Case("SET_PERIOD_3", "select 3-period waveform window", frame(SET_PERIOD, [3])),
    Case("SET_VIEW_PARAM", "switch to parameter page", frame(SET_VIEW, [VIEW_PARAM])),
    Case("SET_VIEW_WAVE", "switch to waveform page", frame(SET_VIEW, [VIEW_WAVE])),
    Case("SET_VIEW_SPEC", "switch to spectrum page", frame(SET_VIEW, [VIEW_SPEC])),
]


EXPECTED = {
    "SET_PERIOD_1": "AA 31 01 01 DD 55",
    "SET_PERIOD_3": "AA 31 01 03 DF 55",
    "SET_VIEW_PARAM": "AA 32 01 00 DD 55",
    "SET_VIEW_WAVE": "AA 32 01 01 DE 55",
    "SET_VIEW_SPEC": "AA 32 01 02 DF 55",
}


def self_check() -> None:
    values = {case.name: hexs(case.data) for case in CASES}
    for name, expected in EXPECTED.items():
        actual = values[name]
        if actual != expected:
            raise RuntimeError(f"{name}: expected {expected}, got {actual}")


def main() -> None:
    self_check()
    print("2026 G passive HMI -> STM32 test frames, 115200 8N1")
    print("Frame: AA CMD LEN DATA CHECK 55, CHECK=(sum before CHECK)&0xFF\n")
    for case in CASES:
        print(case.name)
        print(f"  {case.desc}")
        print(f"  HEX: {hexs(case.data)}")
        print(f"  CHECK: 0x{case.data[-2]:02X}\n")


if __name__ == "__main__":
    main()
