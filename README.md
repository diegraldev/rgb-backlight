# rgb-backlight

Control the RGB of USB gaming keyboards and mice from **KDE Plasma**, with a
custom kernel module. It was born from a very simple question: *«why does the
keyboard brightness slider in Plasma do nothing?»*

Spoiler: because on Linux there is **no driver at all** for these keyboards,
so the `kbd_backlight` LED that UPower looks for was never created, and the
Plasma slider had nothing to control.

With this project:

* the **MSI Mystic Light keyboard** (1462:1562) is controlled with the
  **native Plasma slider** (UPower → PowerDevil), just like screen brightness;
* the **Razer Viper Mini mouse** (1532:008a) is controlled through its own LED;
* and both show up in an **RGB** panel inside the *KDE Control Station* widget,
  with brightness, color, effect and speed.

## Screenshot

![RGB panel of the KDE Control Station widget](docs/rgb-panel.jpg)

*The **RGB** section of the KDE Control Station widget: one collapsible entry
per device (keyboard and mouse), with brightness, colors, effect and speed.*

---

## How it works

### 1. The kernel module (`rgb-backlight.ko`)

It exposes two devices of the `leds` class:

| LED | Device | Seen by UPower |
|---|---|---|
| `msi::kbd_backlight` | MSI keyboard | ✅ (the name contains `kbd_backlight`) |
| `razer::logo_backlight` | Razer mouse | ❌ (UPower only looks for keyboards) |

**MSI keyboard.** The Mystic Light firmware only applies brightness if the
packet includes a color, and it never reports its state back. To be able to
change brightness without losing the color or effect that OpenRGB set, the
module **takes control of the HID device** (the same way `hid-asus` or
`hid-razer` do in the Linux tree) and **intercepts every packet** sent to the
keyboard. The last one is memorized and re-sent with the new brightness.

The keyboard firmware turns itself on (in *static white*) when the device
finishes initializing, and the module doesn't know its state until someone
writes to it. That's what the **profile** handles (see below): it is applied
as soon as the system boots, so the firmware's white is only visible for an
instant. If OpenRGB is open, its profile applies afterwards and wins, as it
should.

**Razer mouse.** Here brightness is a standalone command, so there is nothing
to intercept: we speak the *Razer device command* protocol directly. Important
detail: **Linux does not allow sending that `feature report` from userspace**
(the mouse descriptor doesn't declare it), so doing it from the kernel module
is mandatory.

### 2. The QML plugin (`org.rgb.backlight`)

A QML type `RgbDeviceControl`, written in C++, exposing:

```qml
RgbDeviceControl {
    device: "keyboard"   // or "mouse"
    // available, brightness, color, secondaryColor, effect, speed
}
```

The **keyboard** brightness is written through D‑Bus
(`org.freedesktop.UPower.KbdBacklight`), just like PowerDevil does, to avoid
fighting the value UPower has cached. The color/effect/speed attributes are
written to `sysfs`.

### 3. The widget panel

`widget-patch/` contains the changes to the *KDE Control Station* widget
(https://github.com/EliverLara/kde-control-station): an **RGB** page with one
collapsible section per device (icon + little arrow) and inside it the
brightness, colors, effect and speed controls.

---

## Protocols (documented in the code)

### MSI Mystic Light — `feature report` 0x02, 64 bytes

```
[0] report id (0x02)
[1] packet id
[2] mode      0=off 1=static 2=breathing 3=flash 4=double flash 5=cycle 6=smooth cycle
[3] speed     0=low 1=medium 2=high
[4] brightness 0=off, 1..10 = 10%..100%
[5] number of colors (0..7)
[6..] colors (R, G, B)
```

`num_colors = 0` leaves the firmware without a color → the keyboard turns
off. That's why the last known packet must always be re-sent with the new
brightness.

### Razer Viper Mini — 90-byte *device command* (class `0x0f`)

```
[0] report id (0x00)
[1] status            [2] transaction id (0x3f for lighting)
[3..4] packets        [5] protocol     [6] data size
[7] command class     [8] command id   [9..] arguments
[89] checksum (XOR of [3..88])        [90] reserved
```

Effects (*extended matrix*, class `0x0f`, cmd `0x02`):

| Effect | id | arguments after the header |
|---|---|---|
| Off | 0x00 | `00 00 00` |
| Static | 0x01 | `00 00 01` + RGB |
| Breathing (random/single/dual) | 0x02 | `01 00 01` + RGB (or `02 00 02` + 2×RGB) |
| Spectrum | 0x03 | `00 00 00` |
| Wave | 0x04 | direction `01` + speed `28` + `00` |
| Reactive | 0x05 | `00` + speed + `01` + RGB |

Brightness: class `0x0f`, cmd `0x04`, arguments `storage(01)`, `led(04)`, value 0‑255.

> Format verified against [OpenMouse](https://github.com/OpenMouse-Project/mouse-protocol)
> (`src/razer/codec.ts`) and [OpenRazer](https://github.com/openrazer/openrazer).

---

## Installation

```bash
git clone https://github.com/diegraldev/rgb-backlight.git
cd rgb-backlight
sudo ./install.sh
```

`install.sh` takes care of everything:

1. builds and installs the module with **DKMS** (it rebuilds itself on kernel updates);
2. builds and installs the QML plugin into `/usr/lib64/qt6/qml/org/rgb/backlight/`;
3. installs the **udev** rules that let the user write the attributes and
   disables `systemd-backlight` for these LEDs;
4. enables automatic loading of the module;
5. installs the **profile** and the two services that apply it: at system boot
   and at login;
6. restarts `upower` and `powerdevil`.

Then, in the *KDE Control Station* widget → *Edit controls* → *Add new item* →
**RGB**.

### State is remembered across reboots (profile)

Booting your PC to find the keyboard in white and the mouse in some random
color is the norm: every device starts with whatever it feels like. This
project solves it by saving a **profile** with the state of each device and
restoring it at boot.

* **Saving**: done by the widget plugin, on every change you apply
  (`~/.config/rgb-backlight/<device>.state`). Writes are grouped with a
  300 ms timer (dragging a slider generates dozens of changes) and are made
  **atomically** (`QSaveFile`), so the profile is never left half-written.
  The format is key=value, readable and hand-editable:

  ```ini
  # keyboard
  colors=db0000,ffffff
  mode=1
  speed=0
  brightness=100
  ```

* **Restore at boot**: `rgb-backlight-restore.service` (a system service)
  applies the profile as soon as the system is ready, **before you log in**,
  so the keyboard and mouse are already the way you left them from the very
  start. The profile belongs to you, not to the system: the installer records
  which user owns it in `/var/lib/rgb-backlight/owner` so the service knows
  where to read it from (the kernel module cannot read files).

* **Restore at login**: `rgb-backlight-session.service` applies it again on
  login, as a safety net in case PowerDevil or the firmware touched something
  in between. Both apply the profile through the same path as the panel
  (UPower for keyboard brightness, sysfs for everything else) and write only
  what actually changed.

* **Reflecting in the widget**: the plugin reads the state straight from the
  module when the page opens, so what you see is always the real state.

It can also be handled by hand:

```bash
~/.local/share/rgb-backlight/rgb-profile.sh show      # profile + real state
~/.local/share/rgb-backlight/rgb-profile.sh save      # save the current state
~/.local/share/rgb-backlight/rgb-profile.sh restore   # apply the profile
```

### Why `systemd-backlight` gets disabled

systemd also saves LED brightness on shutdown and restores it at boot
(`systemd-backlight@leds:...service`). It writes the LED on its own and, since
the module re-sends a full packet whenever brightness changes, it ends up
turning the keyboard white before the profile gets a chance to do its job.

`install.sh` masks those two instances so the profile is the single source of
truth, storing not only brightness but also colors, effect and speed.

### If the widget gets updated from GitHub and the changes are lost

```bash
~/rgb-backlight/restore-widget.sh
systemctl --user restart plasma-plasmashell.service
```

### Other commands

```bash
~/rgb-backlight/verify.sh    # state of the module, the LEDs, UPower and Plasma
~/rgb-backlight/test.sh      # automatic test of both devices
sudo ~/rgb-backlight/uninstall.sh
```

---

## Requirements

* Kernel ≥ 5.x with `CONFIG_HID`, `CONFIG_LEDS_CLASS` and `CONFIG_DKMS` (Fedora ✓).
* `dkms`, `gcc`, `make` and the kernel headers (`kernel-devel`).
* To build the QML plugin: `qt6-qtdeclarative-devel`.
* Plasma 6 with the *KDE Control Station* widget (optional: without the
  widget, keyboard brightness still works natively in the Plasma OSD).

---

## Known limitations

* The profile is saved when you change something **from the panel**. If you
  configure the devices with OpenRGB, the profile never finds out: after a
  reboot the last state saved by the widget is applied.
* The brightness shown in the panel is the last one applied by this project;
  it is not read from the hardware (the Razer would allow it, the MSI won't).
* If the mouse is in an effect mode, the color picker stops having any visible
  effect (it's the firmware itself that ignores the color).
* Keyboard brightness goes through UPower, so **OpenRGB and the KDE panel
  step on each other**: the last one to write wins.
* This project speaks the protocol of these devices; it is not endorsed by
  MSI or Razer.

---

## License

GPL-2.0 (the kernel module) — see [LICENSE](LICENSE).
