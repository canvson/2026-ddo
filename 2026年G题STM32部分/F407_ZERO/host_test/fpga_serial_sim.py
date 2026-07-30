#!/usr/bin/env python3
"""Deterministic PC-side FPGA serial simulator for the passive 2026 G project."""

from __future__ import annotations

import argparse
import math
import struct
import sys
import time
from dataclasses import dataclass
from typing import Iterable


HEAD = b"\xA5\x5A"
MAX_PAYLOAD = 128

PKT_STATUS = 0x04
PKT_FEATURE = 0x10
CMD_PING = 0x85

MODE_UA = 0
MODE_UB = 1
MODE_UB_J = 2

STATUS_WAIT = 0
STATUS_VALID = 1
STATUS_HOLD = 2
STATUS_OVER_RANGE = 3
STATUS_LINK_OR_ALGO_ERROR = 4

FLAG_INTERFERENCE_SUPPRESSED = 0x01
FLAG_PHASE_VALID = 0x02


def crc16_modbus(data: bytes) -> int:
    """Return CRC16/MODBUS (poly A001, init FFFF)."""
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def build_frame(packet_type: int, sequence: int, payload: bytes = b"") -> bytes:
    """Build one complete frame; CRC includes both header bytes."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload is too large")
    prefix = HEAD + struct.pack("<BBH", packet_type, sequence, len(payload)) + payload
    return prefix + struct.pack("<H", crc16_modbus(prefix))


@dataclass(frozen=True)
class Frame:
    packet_type: int
    sequence: int
    payload: bytes


class FrameParser:
    """Incremental parser that resynchronizes on the A5 5A header."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.crc_errors = 0
        self.length_errors = 0

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []

        while True:
            start = self._buffer.find(HEAD)
            if start < 0:
                self._buffer[:] = self._buffer[-1:] if self._buffer[-1:] == HEAD[:1] else b""
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < 6:
                break

            payload_length = self._buffer[4] | (self._buffer[5] << 8)
            if payload_length > MAX_PAYLOAD:
                self.length_errors += 1
                del self._buffer[0]
                continue

            frame_length = payload_length + 8
            if len(self._buffer) < frame_length:
                break

            expected_crc = self._buffer[frame_length - 2] | (
                self._buffer[frame_length - 1] << 8
            )
            actual_crc = crc16_modbus(bytes(self._buffer[: frame_length - 2]))
            if expected_crc != actual_crc:
                self.crc_errors += 1
                del self._buffer[0]
                continue

            frames.append(
                Frame(
                    self._buffer[2],
                    self._buffer[3],
                    bytes(self._buffer[6 : 6 + payload_length]),
                )
            )
            del self._buffer[:frame_length]

        return frames


class FpgaSimulator:
    """Passive FPGA model that continuously emits compact feature packets."""

    def __init__(self) -> None:
        self.mode = MODE_UA
        self.frame_id = 0
        self.tx_sequence = 0
        self.rx_crc_errors = 0
        self.status = STATUS_VALID

    def _next_frame(self, packet_type: int, payload: bytes) -> bytes:
        frame = build_frame(packet_type, self.tx_sequence, payload)
        self.tx_sequence = (self.tx_sequence + 1) & 0xFF
        return frame

    def _components(self) -> list[tuple[int, int, int]]:
        base = 50_000 + self.mode * 25_000
        return [
            (base, 90_000 + self.mode * 8_000, 0),
            (base * 2, 28_000 + self.mode * 4_000, 900),
            (base * 3, 12_000 + self.mode * 2_000, -450),
        ]

    def feature_payload(self) -> bytes:
        self.frame_id = (self.frame_id + 1) & 0xFFFFFFFF
        self.mode = (self.frame_id // 20) % 3
        components = self._components()
        flags = FLAG_PHASE_VALID
        if self.mode == MODE_UB_J:
            flags |= FLAG_INTERFERENCE_SUPPRESSED
        vpp_uv = sum(abs(amp) for _, amp, _ in components) * 2
        urms_uv = int(round(math.sqrt(sum((amp / math.sqrt(2)) ** 2 for _, amp, _ in components))))
        payload = struct.pack(
            "<IBBBBiiII",
            self.frame_id,
            self.mode,
            self.status,
            len(components),
            flags,
            vpp_uv,
            urms_uv,
            components[0][0],
            0,
        )
        for frequency, amp_peak_uv, phase_deg10 in components:
            payload += struct.pack("<IihH", frequency, amp_peak_uv, phase_deg10, 0)
        assert len(payload) == 60
        return payload

    def status_payload(self) -> bytes:
        return struct.pack("<IBBHHH", self.frame_id, self.status, 0, self.rx_crc_errors, 0, 0)

    def data_cycle(self) -> list[bytes]:
        return [
            self._next_frame(PKT_FEATURE, self.feature_payload()),
            self._next_frame(PKT_STATUS, self.status_payload()),
        ]

    def handle_command(self, frame: Frame) -> list[bytes]:
        if frame.packet_type == CMD_PING and not frame.payload:
            return [self._next_frame(PKT_STATUS, self.status_payload())]
        return []


def packet_names(frames: Iterable[bytes]) -> str:
    names = {
        PKT_FEATURE: "FEATURE",
        PKT_STATUS: "STATUS",
    }
    return ",".join(names.get(frame[2], f"0x{frame[2]:02X}") for frame in frames)


def run_serial(port: str, baudrate: int, interval: float, once: bool) -> int:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(
            "error: serial mode requires pyserial; install it in the project environment "
            "with 'python -m pip install pyserial'",
            file=sys.stderr,
        )
        return 2

    parser = FrameParser()
    simulator = FpgaSimulator()
    next_cycle = time.monotonic()

    try:
        with serial.Serial(port=port, baudrate=baudrate, timeout=0.05) as stream:
            print(f"passive FPGA simulator listening on {port} at {baudrate} baud")
            while True:
                received = stream.read(stream.in_waiting or 1)
                for command in parser.feed(received):
                    simulator.rx_crc_errors = parser.crc_errors
                    responses = simulator.handle_command(command)
                    if responses:
                        stream.write(b"".join(responses))
                        print(
                            f"RX type=0x{command.packet_type:02X} seq={command.sequence}; "
                            f"TX {packet_names(responses)}"
                        )

                now = time.monotonic()
                if now >= next_cycle:
                    responses = simulator.data_cycle()
                    stream.write(b"".join(responses))
                    print(f"TX {packet_names(responses)}")
                    if once:
                        return 0
                    next_cycle = now + interval
    except KeyboardInterrupt:
        print("stopped")
        return 0
    except serial.SerialException as exc:
        print(f"error: cannot use serial port {port!r}: {exc}", file=sys.stderr)
        return 2


def run_self_test() -> int:
    assert crc16_modbus(b"123456789") == 0x4B37

    ping = build_frame(CMD_PING, 7)
    assert struct.unpack_from("<H", ping, len(ping) - 2)[0] == crc16_modbus(ping[:-2])
    parser = FrameParser()
    assert parser.feed(b"\x00\xA5") == []
    parsed = parser.feed(ping[1:5]) + parser.feed(ping[5:])
    assert parsed == [Frame(CMD_PING, 7, b"")]

    bad = bytearray(build_frame(CMD_PING, 8))
    bad[-1] ^= 0x01
    recovered = parser.feed(bytes(bad) + build_frame(CMD_PING, 9))
    assert recovered == [Frame(CMD_PING, 9, b"")]
    assert parser.crc_errors == 1

    simulator = FpgaSimulator()
    responses = simulator.data_cycle()
    assert [frame[2] for frame in responses] == [PKT_FEATURE, PKT_STATUS]
    decoded = FrameParser().feed(b"".join(responses))
    feature, status = decoded
    assert len(feature.payload) == 60
    assert feature.payload[4] in (MODE_UA, MODE_UB, MODE_UB_J)
    assert feature.payload[5] in (
        STATUS_WAIT,
        STATUS_VALID,
        STATUS_HOLD,
        STATUS_OVER_RANGE,
        STATUS_LINK_OR_ALGO_ERROR,
    )
    assert feature.payload[6] == 3
    assert len(status.payload) == 12

    ping_responses = simulator.handle_command(Frame(CMD_PING, 11, b""))
    assert [frame[2] for frame in ping_responses] == [PKT_STATUS]

    crc_parser = FrameParser()
    crc_parser.feed(bytes(bad))
    simulator.rx_crc_errors = crc_parser.crc_errors
    ping_status = FrameParser().feed(simulator.handle_command(Frame(CMD_PING, 12, b""))[0])[0]
    assert struct.unpack_from("<H", ping_status.payload, 6)[0] == 1

    print("self-test passed: CRC, resync, PING, FEATURE/STATUS")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example COM5")
    parser.add_argument("--baudrate", type=int, default=921600)
    parser.add_argument("--interval", type=float, default=0.5, help="feature period in seconds")
    parser.add_argument("--once", action="store_true", help="exit after the first transmitted batch")
    parser.add_argument("--self-test", action="store_true", help="run tests without pyserial or hardware")
    args = parser.parse_args(argv)
    if args.self_test and args.port:
        parser.error("--self-test and --port cannot be used together")
    if not args.self_test and not args.port:
        parser.error("serial mode requires an explicit --port (or use --self-test)")
    if args.baudrate <= 0:
        parser.error("--baudrate must be positive")
    if args.interval <= 0:
        parser.error("--interval must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run_serial(args.port, args.baudrate, args.interval, args.once)


if __name__ == "__main__":
    raise SystemExit(main())
