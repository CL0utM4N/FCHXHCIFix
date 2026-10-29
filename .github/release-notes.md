USB 2 fixes for more firmware, `SSDT-XOSI.aml` in the zip, and on-screen status from the kext.

- **`SSDT-FCHXHCI` now also clears `EHCD`,** the USB 2 (EHCI) twin of the USB 3 D3cold flag. In Windows 8+ mode the
  firmware power-gated the EHCI controller in a way macOS can't undo (dead USB 2 ports on a Dell Inspiron 3475 AIO).
  Replace your `SSDT-FCHXHCI.aml` with the one in this zip.
- **New in the README: *USB 2 hub port marked "not connectable"*.** Some firmware marks the EHCI port that the internal
  hub hangs off as not connectable, so macOS never sees the hub or anything behind it. The README shows how to check
  your DSDT and gives a one-byte OpenCore ACPI patch (needed on the Dell Inspiron 3475).
- **New: `SSDT-XOSI.aml`** and *Check your firmware*. AMD's reference firmware turns the HPET off unless it sees
  Windows Vista or later. The symptoms are a dead or flaky keyboard and USB, or a boot that only continues once a USB
  device is plugged in. Install steps 5 and 6 cover it.
- **The kext prints its status on the verbose boot screen:** `forwarding interrupts …`, or `not forwarding: <reason>`.
  It now keeps looking for Apple's interrupt source for 5 minutes instead of 5 seconds, for slower start-ups.
- Still required: the `AppleUSBXHCI::reset()` kernel patch (Install step 4, `Limit` 2048).
- Tested: ASUS VivoBook 15 D509BA (A9-9425; Ventura 13.7.8, Sonoma 14, Sequoia 15.8.1) and Dell Inspiron 3475 AIO
  (A6-9225; Ventura 13.7.8, every USB port working).
