from __future__ import annotations

import argparse
import struct
from pathlib import Path


PROGRAM_SCRIPT = """int a_wave=0
int b_wave=0
int a_freq=1000
int b_freq=1000
int a_amp=1000
int b_amp=1000
int a_duty=50
int b_duty=50
int b_phase=0
int phase_send=0
int chk=0
int tmp=0
int step=0
int c_white=65535
int c_panel=6471
int c_blue=11263
int c_cyan=7671
int c_pink=63519
int c_black=0
baud=115200
dim=100
recmod=0
bkcmd=0
page 0
"""


def iter_entries(data: bytes):
    count = struct.unpack_from("<I", data, 0)[0]
    pos = 4
    for index in range(count):
        if pos + 28 > len(data):
            break
        raw_name = data[pos : pos + 16].rstrip(b"\0")
        name = raw_name.decode("ascii", "ignore")
        off, size, flag = struct.unpack_from("<III", data, pos + 16)
        yield index, name, off, size, flag
        pos += 28


def patch_file(path: Path, dry_run: bool = False) -> bool:
    data = bytearray(path.read_bytes())
    program = PROGRAM_SCRIPT.replace("\n", "\r\n").encode("gbk")

    for _index, name, off, size, _flag in iter_entries(data):
        if name != "Program.s":
            continue
        if len(program) > size:
            raise RuntimeError(f"Program.s payload is {len(program)} bytes, chunk only has {size} bytes")

        old = bytes(data[off : off + size])
        new = program + (b" " * (size - len(program)))
        changed = old != new
        if changed and not dry_run:
            data[off : off + size] = new
            path.write_bytes(data)
        return changed

    raise RuntimeError("Program.s entry not found")


def main() -> int:
    parser = argparse.ArgumentParser(description="Patch USART HMI Program.s init block.")
    parser.add_argument("hmi", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    changed = patch_file(args.hmi, args.dry_run)
    print(("would change" if args.dry_run else "changed") if changed else "already ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
