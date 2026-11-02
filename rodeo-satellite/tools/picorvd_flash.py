#!/usr/bin/env python3
"""Flash a CH32V003 through a Pi Pico running picorvd, with no GDB: speaks
picorvd's GDB remote protocol directly (vFlashErase/vFlashWrite/vFlashDone),
reads the image back to verify, then resets the chip.

    picorvd_flash.py --port /dev/cu.usbmodemXXXX satellite.bin

Replug the Pico after moving it to another satellite: picorvd hangs if its
target goes away while it is connected.
"""
import argparse, os, sys, termios, time

def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 5
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def escape(data):
    out = bytearray()
    for b in data:
        if b in (0x23, 0x24, 0x7d, 0x2a):
            out += bytes((0x7d, b ^ 0x20))
        else:
            out.append(b)
    return bytes(out)

class Gdb:
    def __init__(self, fd): self.fd = fd

    def read_byte(self, deadline):
        while time.time() < deadline:
            b = os.read(self.fd, 1)
            if b: return b[0]
        # picorvd waits forever on a target that stopped answering, as one does
        # when the wires come off between satellites; replugging clears it.
        sys.exit("no reply from picorvd: unplug the Pico and plug it back in, "
                 "with the satellite wired")

    def cmd(self, body, timeout=10.0):
        if isinstance(body, str): body = body.encode()
        os.write(self.fd, b"$" + body + b"#%02x" % (sum(body) & 0xff))
        deadline = time.time() + timeout
        while self.read_byte(deadline) != ord("$"):
            pass
        reply = bytearray()
        while (c := self.read_byte(deadline)) != ord("#"):
            reply.append(c)
        self.read_byte(deadline); self.read_byte(deadline)
        os.write(self.fd, b"+")
        return bytes(reply)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--port", required=True)
    args = ap.parse_args()
    image = open(args.bin, "rb").read()
    g = Gdb(open_port(args.port))
    time.sleep(0.2)
    print("target:", g.cmd("?").decode(errors="replace"))

    size = (len(image) + 63) // 64 * 64
    if g.cmd(f"vFlashErase:0,{size:x}", timeout=30) != b"OK":
        sys.exit("erase failed")
    t0 = time.time()
    for off in range(0, len(image), 1024):
        r = g.cmd(b"vFlashWrite:%x:" % off + escape(image[off:off + 1024]))
        if r != b"OK":
            sys.exit(f"write at {off:#x} failed: {r!r}")
    if g.cmd("vFlashDone", timeout=30) != b"OK":
        sys.exit("flash done failed")
    print(f"wrote {len(image)} bytes in {time.time() - t0:.1f} s")

    back = bytearray()
    for off in range(0, len(image), 256):
        n = min(256, len(image) - off)
        r = g.cmd(f"m{off:x},{n:x}")
        if len(r) != 2 * n:
            sys.exit(f"read at {off:#x} failed: {r!r}")
        back += bytes.fromhex(r.decode())
    if bytes(back) != image:
        bad = next(i for i in range(len(image)) if back[i] != image[i])
        sys.exit(f"VERIFY FAILED at {bad:#x}: wrote {image[bad]:#04x}, read {back[bad]:#04x}")
    print("verified")
    g.cmd("vKill")
    print("reset; running")

if __name__ == "__main__":
    main()
