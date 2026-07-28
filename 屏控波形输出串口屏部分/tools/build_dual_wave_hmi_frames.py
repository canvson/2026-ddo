from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass


SOF = 0xAA
EOF = 0x55
CMD_OUTPUT_CONFIG = 0x21
LEN_OUTPUT_CONFIG = 0x16


@dataclass(frozen=True)
class DualWaveConfig:
    wave_a: int = 0
    freq_a_hz: int = 1000
    amp_a_mvpp: int = 1000
    duty_a_pct10: int = 500
    wave_b: int = 0
    freq_b_hz: int = 1000
    amp_b_mvpp: int = 1000
    duty_b_pct10: int = 500
    phase_b_rel_a_deg: int = 0


def clamp(value: int, lo: int, hi: int) -> int:
    return min(max(value, lo), hi)


def build_frame(cfg: DualWaveConfig) -> bytes:
    wave_a = clamp(cfg.wave_a, 0, 2)
    wave_b = clamp(cfg.wave_b, 0, 2)
    freq_a = clamp(cfg.freq_a_hz, 1, 20_000_000)
    freq_b = clamp(cfg.freq_b_hz, 1, 20_000_000)
    amp_a = clamp(cfg.amp_a_mvpp, 0, 5000)
    amp_b = clamp(cfg.amp_b_mvpp, 0, 5000)
    duty_a = clamp(cfg.duty_a_pct10, 100, 900)
    duty_b = clamp(cfg.duty_b_pct10, 100, 900)
    phase = clamp(cfg.phase_b_rel_a_deg, -180, 180)

    data = struct.pack(
        "<BBB I H H B I H H h",
        1,
        0x03,
        wave_a,
        freq_a,
        amp_a,
        duty_a,
        wave_b,
        freq_b,
        amp_b,
        duty_b,
        phase,
    )
    assert len(data) == LEN_OUTPUT_CONFIG
    checksum = (SOF + CMD_OUTPUT_CONFIG + LEN_OUTPUT_CONFIG + sum(data)) & 0xFF
    return bytes([SOF, CMD_OUTPUT_CONFIG, LEN_OUTPUT_CONFIG]) + data + bytes([checksum, EOF])


def hex_bytes(frame: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in frame)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build HMI -> STM32 dual-wave output frames.")
    parser.add_argument("--wave-a", type=int, default=0)
    parser.add_argument("--freq-a", type=int, default=1000)
    parser.add_argument("--amp-a", type=int, default=1000)
    parser.add_argument("--duty-a", type=int, default=500, help="0.1 percent units, 500 means 50.0 percent")
    parser.add_argument("--wave-b", type=int, default=0)
    parser.add_argument("--freq-b", type=int, default=1000)
    parser.add_argument("--amp-b", type=int, default=1000)
    parser.add_argument("--duty-b", type=int, default=500, help="0.1 percent units, 500 means 50.0 percent")
    parser.add_argument("--phase", type=int, default=0, help="signed degrees, B relative to A")
    args = parser.parse_args()

    cfg = DualWaveConfig(
        wave_a=args.wave_a,
        freq_a_hz=args.freq_a,
        amp_a_mvpp=args.amp_a,
        duty_a_pct10=args.duty_a,
        wave_b=args.wave_b,
        freq_b_hz=args.freq_b,
        amp_b_mvpp=args.amp_b,
        duty_b_pct10=args.duty_b,
        phase_b_rel_a_deg=args.phase,
    )
    print(hex_bytes(build_frame(cfg)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
