Sleep: these machines now stay in S3 and wake from USB keyboards on both the USB 2 and the USB 3 ports.

Under macOS, Carrizo/Bristol Ridge/Stoney machines woke ~1 s after every S3 entry, and USB 3 keyboards couldn't wake
them. With this release and [CarrizoRed](https://github.com/CL0utM4N/CarrizoRed) 0.2.0 they sleep properly
(see *Sleep* in the README).

- **New: `SSDT-FCHSLEEP.aml`** (Install step 7, with a one-entry `_PTS` → `ZPTS` OpenCore patch). AMD's reference
  firmware powers the USB 2 controller (EHCI) off in `_PTS` while it is armed for wake, which wakes the machine at
  once. The SSDT keeps EHCI powered through `_PTS` and leaves the USB 3 controller to the firmware's own sleep
  hand-over, which is what lets USB 3 keyboards wake the machine.
- **New: the webcam's port on the internal USB 2 hub is switched off during sleep** and back on after the wake (the
  webcam loses power in S3, which the hub took as a reason to wake). `-fchcamwake` leaves it on.
- **Remove `SSDT-GPRW`** (or any `_PRW` override for `EHC1`/`XHC0`) if you added one to stop instant wakes: it isn't
  needed with this, and it stops USB keyboards waking the machine.
- The rest of the sleep fixes (CPU cache flush before S3, wake source filtering, the FCH's USB wake quirks) are in
  CarrizoRed 0.2.0. Without CarrizoRed, sleep on these machines isn't expected to work.
- Still required for USB 3: the `AppleUSBXHCI::reset()` kernel patch (Install step 4, `Limit` 2048).
- Tested: Dell Inspiron 3475 AIO (A6-9225, Tahoe) and ASUS VivoBook 15 D509BA (A9-9425): real S3, no wakes of its own,
  wake from USB 2 and USB 3 keyboards, every port working after the wake.
