"""Deprecated helper for an earlier workflow.

The current 2026 G package uses a single FPGA FEATURE packet and does not
generate FPGA command lookup tables. Keep this entry only so old notes fail
loudly instead of producing stale output.
"""

from __future__ import annotations


def main() -> None:
    raise SystemExit(
        "extract_fpga_tables.py is deprecated. "
        "Use tools/build_g_hmi_frames.py, tools/protocol_selftest.c, "
        "and stm32_ref/fpga_packet.* instead."
    )


if __name__ == "__main__":
    main()
