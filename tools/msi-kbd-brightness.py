#!/usr/bin/env python3
"""
msi-kbd-brightness - controla el brillo del teclado MSI Mystic Light
(VID 1462 / PID 1562) sin necesidad del modulo de kernel.

Uso:
  msi-kbd-brightness              -> muestra el brillo actual (cacheado)
  msi-kbd-brightness get          -> lee el nivel actual del dispositivo
  msi-kbd-brightness set <0-10>   -> fija el brillo (0 = apagado, 10 = 100%)
  msi-kbd-brightness off          -> apaga la luz
  msi-kbd-brightness on           -> al 100%

Nota: al escribir se usa "num_colors = 0", de modo que el firmware mantiene
el color y el efecto actuales (no pisa lo que haya puesto OpenRGB).
"""
import fcntl
import glob
import os
import sys

VID, PID = 0x1462, 0x1562
REPORT_ID = 0x02
REPORT_LEN = 64
MAX_LEVEL = 10


def find_hidraw():
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            with open(os.path.join(path, "device", "uevent")) as f:
                txt = f.read()
        except OSError:
            continue
        for line in txt.splitlines():
            if line.startswith("HID_ID="):
                try:
                    vid, pid = (int(x, 16) for x in line.split(":")[1:3])
                except ValueError:
                    continue
                if (vid, pid) == (VID, PID):
                    return os.path.join("/dev", os.path.basename(path))
    return None


def send(level):
    dev = find_hidraw()
    if not dev:
        print("No se encontro el teclado MSI (1462:1562)", file=sys.stderr)
        return 1

    pkt = bytearray(REPORT_LEN)
    pkt[0] = REPORT_ID   # report id
    pkt[1] = 0x00        # packet id
    pkt[2] = 0x01        # modo (se ignora con num_colors = 0)
    pkt[3] = 0x00        # velocidad
    pkt[4] = level       # brillo: 0 = off, 1..10 = 10%..100%
    pkt[5] = 0x00        # 0 colores -> se conserva color y efecto

    fd = os.open(dev, os.O_RDWR)
    try:
        fcntl.ioctl(fd, 0xC0000000 | (REPORT_LEN << 16) | 0x48 << 8 | 0x06, bytes(pkt))
    finally:
        os.close(fd)
    print(f"brillo del teclado MSI -> {level} ({level * 10}%)")
    return 0


def main():
    args = sys.argv[1:]
    if not args:
        args = ["get"]

    cmd = args[0]
    if cmd == "set":
        if len(args) < 2:
            print("uso: msi-kbd-brightness set <0-10>", file=sys.stderr)
            return 1
        level = int(args[1])
        if not 0 <= level <= MAX_LEVEL:
            print(f"el nivel debe estar entre 0 y {MAX_LEVEL}", file=sys.stderr)
            return 1
        return send(level)
    if cmd == "off":
        return send(0)
    if cmd == "on":
        return send(MAX_LEVEL)
    if cmd == "get":
        path = "/sys/class/leds/msi::kbd_backlight/brightness"
        if os.path.exists(path):
            with open(path) as f:
                print(f"LED del kernel: {f.read().strip()} / {MAX_LEVEL}")
        else:
            print("modulo del kernel no cargado (aun no se puede leer el estado)")
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
