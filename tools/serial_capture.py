#!/usr/bin/env python3
"""Non-interactive serial log capture for MicroLink flash/verify cycles.

Reads a COM port for a fixed number of seconds and prints everything to
stdout. With --reset it first performs the standard auto-reset pulse
(DTR=high/IO0, RTS pulse on EN) so boot banners are captured from t=0 on
boards wired for auto-reset; on boards without that wiring it simply reads.

Run it with the ESP-IDF venv python (pyserial lives there via esptool):

    . 'C:\\Espressif\\tools\\Microsoft.v6.1.PowerShell_profile.ps1'
    python tools/serial_capture.py COM9 30 --reset

Never run this while idf.py monitor / esptool holds the port.
"""

import sys
import time

DEFAULT_PORT = "COM9"
DEFAULT_SECONDS = 30
BAUD = 115200


def main() -> int:
    params = [a for a in sys.argv[1:] if not a.startswith("--")]
    port = params[0] if params else DEFAULT_PORT
    seconds = int(params[1]) if len(params) > 1 else DEFAULT_SECONDS
    do_reset = "--reset" in sys.argv

    try:
        import serial
    except ImportError:
        print("pyserial not found - run with the ESP-IDF venv python "
              "(source the IDF PowerShell profile first)", file=sys.stderr)
        return 2

    # dtr/rts start de-asserted so merely opening the port cannot reset
    # the chip or hold it in a boot-mode state.
    ser = serial.Serial(port, BAUD, timeout=0.2)
    try:
        # This pyserial build rejects dtr/rts constructor kwargs; set the
        # attributes right after open so the port cannot hold the chip in
        # reset or bootloader mode.
        ser.dtr = False
        ser.rts = False
    except (AttributeError, OSError) as exc:
        print(f"warning: could not de-assert DTR/RTS: {exc}", file=sys.stderr)
    try:
        if do_reset:
            ser.setDTR(False)   # IO0 high = normal boot
            ser.setRTS(True)    # EN low = hold in reset
            time.sleep(0.15)
            ser.setRTS(False)   # release -> boot
            time.sleep(0.05)

        total = 0
        start = time.time()
        while time.time() - start < seconds:
            data = ser.read(4096)
            if data:
                total += len(data)
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
        print(f"\n--- capture done: {total} bytes in {seconds}s on {port} ---")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
