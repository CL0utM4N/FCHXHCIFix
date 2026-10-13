# FCHXHCIFix

Fixes painfully slow USB in macOS on AMD **Carrizo**, **Bristol Ridge** and **Stoney Ridge** machines (AMD A-series
6th/7th gen, E2-9000, A6-9220, A9-9425 and similar).

On these APUs the USB 3 controller built into the chip, the FCH xHCI (PCI `1022:7914`), works under macOS, but **none
of its interrupts reach macOS**. Apple's USB driver then services it only from a 2-second fallback timer, so every USB
transfer can wait up to 2 seconds:
- USB storage crawls (about 130 KB/s);
- installing macOS from a USB stick takes most of a day;
- mice and keyboards lag or drop out.

FCHXHCIFix wakes Apple's USB interrupt handler itself every ~2.5 ms. With it, USB runs at normal speed.

It's a plain IOKit kext. It needs no Lilu or other dependencies, and it only attaches to `1022:7914`.

It was found and written while bringing up **CarrizoRed**, a driver in development that makes Apple's AMD Polaris
graphics drivers work on these APUs. FCHXHCIFix doesn't depend on CarrizoRed, and it's useful on any of these machines
running macOS, whatever graphics setup they use.

## Does my machine have this controller?

Every machine with one of these APUs has it, because it's part of the chip:

| Platform | Socket | Examples |
|---|---|---|
| 6th-gen A-series (2015), **Carrizo** | FP4 (laptops) | A8-8600P, A10-8700P, FX-8800P, Pro A10-8730B, A12-8800B |
| 7th-gen A-series (2016–17), **Bristol Ridge** | FP4 (laptops) | A9-9410, A10-9600P/9620P, A12-9700P/9720P, FX-9800P/9830P |
| 7th-gen A-series desktop, **Bristol Ridge** | AM4 | A6-9500, A8-9600, A10-9700, A12-9800(E), Athlon X4 940/950/970 |
| 7th-gen E/A-series (2016–19), **Stoney Ridge** | FT4 (budget laptops, Chromebooks) | E2-9000(e), A4-9120(C), A6-9220/9225, A9-9420/9425 |

To check:
- **macOS:** `ioreg -lw0 -r -c IOPCIDevice | grep -E 'vendor-id|device-id'` and look for a device with `vendor-id <22100000>`
  and `device-id <14790000>`. Or, in System Information → USB, look for a controller on PCI device `0x7914`.
- **Linux:** `lspci -nn | grep 1022:7914` should show `00:10.0 USB controller [0c03]: … FCH USB XHCI Controller
  [1022:7914] (rev 20)`.
- **Windows:** Device Manager → Universal Serial Bus controllers → AMD USB 3.0 eXtensible Host Controller → Properties →
  Details → Hardware Ids: `PCI\VEN_1022&DEV_7914`.

Ryzen machines also show `1022:7914`, but as the **subsystem** ID of a different USB controller (e.g. Raven `1022:15e0`,
Matisse `1022:149c`). Those controllers are not affected. FCHXHCIFix doesn't match them, because it uses
`IOPCIPrimaryMatch`, which compares the device's own vendor and device ID.

## Install

1. Download `FCHXHCIFix-<version>.zip` from [Releases](../../releases) and unzip it.
2. Copy `FCHXHCIFix.kext` to `EFI/OC/Kexts`.
3. Add it to `config.plist` → Kernel → Add (`BundlePath` `FCHXHCIFix.kext`, `ExecutablePath`
   `Contents/MacOS/FCHXHCIFix`, `PlistPath` `Contents/Info.plist`, `Enabled` true). It has no dependencies and can go in
   any position.

If you install macOS from a USB stick on one of these machines, put it in the EFI from the start: the installer reads
everything over USB.

## Check that it's working

```
ioreg -lw0 -r -c IOPCIDevice | grep FCHXHCIFix-state
"FCHXHCIFix-state" = "1 sources, 482113 forwards, 482113 polls; running, mode 2 (every poll)"
```

`1 sources` means it found Apple's interrupt source for the controller. The counters should keep climbing.

| Boot-arg | Effect |
|---|---|
| `-fchxhcioff` | Disable it |
| `fchxhcimode=1` | Wake the handler only when `USBSTS.EINT` is set (for diagnosis; not enough on its own) |

## Tested

- ASUS VivoBook 15 **D509BA** (A9-9425, Stoney Ridge), macOS Sequoia: 15.4 recovery and installer, and an installed 15.8.1.

Only the D509BA has been tested so far. The controller, its revision and the APU's interrupt path are the same on all
of the machines below, so they very likely behave the same. Reports from other machines are welcome: open an issue
with your model, macOS version and the `FCHXHCIFix-state` line.

Models confirmed to have the controller, from public Linux hardware probes (`lspci` output with the controller at
`00:10.0`, [linuxhw/LsPCI](https://github.com/linuxhw/LsPCI)):

| Machine | APU graphics (so platform) |
|---|---|
| ASUS VivoBook 15 **D509BA** | Stoney (R5), A9-9425; the machine this was found on |
| Lenovo IdeaPad **330-14AST** | Stoney |
| Multilaser **PC150** notebook | Stoney |
| Fusion5 **S14** notebook | Stoney |
| ATOPNUC **MA90** mini PC | Stoney |
| Lenovo **3100** desktop | Wani (Carrizo/Bristol Ridge graphics) |
| Biostar **FX9830M** board | Wani, FX-9830P |
| OEM **AM4** desktop | Wani, Bristol Ridge AM4 APU |
| MSI **MS-7A38**, **MS-7B38**, **MS-7B84** (AM4 boards) | Bristol Ridge AM4 (APU or Athlon X4) |

Many other machines of that era use these APUs (examples, not individually checked): HP 14/15 and 240/245/255 G6/G7,
Lenovo IdeaPad 110/310/320/330 (…AST models), Acer Aspire 3/5 with A9/A6, Dell Inspiron 3000 AMD models, HP EliteBook
725/745/755 G3/G4, Lenovo ThinkPad A275/A475, and Stoney Chromebooks (HP Chromebook 14 db0, Acer Chromebook 315,
Lenovo 14e).

## Symptoms without it

- **USB storage is extremely slow.** On the D509BA a USB 3 stick at 5 Gbps read at ~130 KB/s. The installer's
  `validate_root_image` step sat for tens of minutes, and copying the installer took most of a day. `dd` from the stick
  appeared to hang.
- **Input devices lag,** and USB mice disconnect and reconnect.
- **Nothing in the logs says why.** The link is up at full speed and there are no errors.
- **The same machine is fine under Linux.**

## The controller

| | |
|---|---|
| PCI ID | `1022:7914` "FCH USB XHCI Controller", revision `0x20`, class `0c0330` (xHCI) |
| Location | `00:10.0`, inside the APU (AMD's FCH, the on-die southbridge) |
| Spec | xHCI 1.0; USB 3.x Gen 1 (5 Gbps); 8 root ports |
| Interrupts | MSI (1 of up to 8 vectors, 64-bit, not maskable) and MSI-X (8 vectors, table in BAR0 at `0x1000`) |
| Companion | the FCH EHCI `1022:7908` |
| macOS driver | Apple's generic `AppleUSBXHCIPCI` (no AMD-specific class or quirk for this device) |
| Linux driver | `xhci_hcd` with no device-specific quirk (only the generic spurious-success one); uses MSI-X |

## How it was diagnosed

With a read-only diagnostic kext that sampled the controller every millisecond:

1. **The link is healthy.** The port is in U0 100 % of the time: no U1/U2 power saving, no link recovery, no drop-offs,
   and the controller stays in D0 with no host system errors. So it isn't link power management or runtime D3.
2. **The interrupts never arrive.** `USBSTS.EINT` ("an interrupt was raised") was set in every sample and never cleared,
   which means Apple's interrupt handler never ran. The event ring was serviced once every 2 seconds, which is
   AppleUSBXHCI's fallback timer. `IMAN.IP` read 0. With MSI, the controller clears that bit itself when it sends the
   message, so the controller *does* send its interrupts.
3. **It isn't the interrupt type.** Switching the controller to MSI-X, the mode Linux uses, with
   `IOPCIDevice::configureInterrupts(kIOInterruptTypePCIMessagedX)` before Apple's driver started changed nothing.
4. **It isn't the CPU side.** The GPU's MSIs, sent to the same address (`0xFEE10000`), arrive and are handled.
5. **Apple's handler isn't rejecting them.** In Apple's code (15.8.1), for MSI the primary filter just calls a no-op
   `clearInterruptPending` and accepts every interrupt, and `primaryInterrupt` drains the event ring without further
   checks. Any interrupt that arrived would be processed.
6. **Waking the handler only when `EINT` is set isn't enough.** The controller then flags new events only every 1–2 s
   (the IO interrupter completed 1–4 batches per 2 s). Waking it continuously makes USB fast.

**Still open:** why this controller's MSI/MSI-X writes never become CPU interrupts under macOS when they do under
Linux. Point 6 also suggests the controller doesn't re-raise an interrupt until something it waits for happens (an
event handler busy / interrupt pending handshake), which only the timer path provided. Anyone with a hardware MSI trace
or AMD documentation for the FCH (BKDG for Family 15h Models 60h–6Fh / 70h–7Fh) is welcome to dig further.

## How the fix works

A plain IOKit kext. It:
1. matches the controller (`IOPCIPrimaryMatch` `0x79141022`) in its own `IOMatchCategory`, so it attaches next to
   AppleUSBXHCIPCI instead of competing with it;
2. waits for AppleUSBXHCIPCI to start, then finds that driver's `IOFilterInterruptEventSource` for the controller on its
   work loop (walking the work loop's event chain inside the gate);
3. every ~2.5 ms calls the source's `signalInterrupt()`. This runs AppleUSBXHCI's normal interrupt handler on its work
   loop, exactly as a delivered interrupt would. When there's nothing to do, the handler returns at once. It also clears
   `USBSTS.EINT` when set.

**Cost:** one work-loop wake-up every ~2.5 ms. That's negligible for CPU use, but it does keep the CPU from its deepest
idle states, which may cost some battery life until the root cause is found.

**Limitations:**
- Interrupt latency is up to ~2.5 ms instead of microseconds. Fine for storage and input. Audio interfaces and webcams
  on this controller are untested.
- It doesn't fix sleep or wake, if those have their own problems on these machines.

## Other USB problems on these machines

These aren't caused by the lost interrupts and FCHXHCIFix doesn't fix them, but they turned up on the same controller
on the D509BA:
- **The firmware's xHCI D3cold path.** With the firmware in Windows 8+ mode (`_OSI`), `XHC0._PS3` cut the controller's
  power through an SMI, and `_PS0` had to reload its firmware; macOS never got it back. An SSDT that clears the
  firmware's D3cold flag for the controller under macOS fixed it (the D509BA's DSDT calls it `XHCD`; names differ by
  vendor).
- **Reset timeout.** `AppleUSBXHCI::reset()` waits only 100 ms for `USBCMD.HCRST`; this controller needs ~105 ms. An
  OpenCore kernel patch raising it to 1000 ms fixed it.
- **USB map.** As on any machine, map the ports (e.g. with a `USBMap.kext`).

## Building

On macOS with Xcode:

```
git clone --depth 1 https://github.com/acidanthera/MacKernelSDK
make
```

The kext and a zip land in `build/`. Every push is also built by GitHub Actions (`.github/workflows/build.yml`); tagged
versions are published under Releases.

## License

BSD-3-Clause, see [LICENSE](LICENSE).
