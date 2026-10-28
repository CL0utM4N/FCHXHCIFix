New: **`SSDT-XOSI.aml`** in the zip, and a guide to checking your firmware.

- **The HPET is off under macOS on these laptops.** AMD's reference firmware enables it only for Windows Vista or later,
  and macOS answers only `_OSI("Darwin")`. Symptoms: a dead or flaky keyboard and USB, or a boot that only continues once
  a USB device is plugged in (ASUS VivoBook 15 D509BA, Acer Aspire E5-523). `SSDT-XOSI` plus the `_OSI` → `XOSI` rename
  make the firmware see Windows 10 (Install step 5).
- With the firmware in Windows 8+ mode, add `SSDT-FCHXHCI.aml` too (Install step 6), which stops the firmware
  power-gating the USB 3 controller in a way macOS can't undo.
- *Check your firmware* in the README: what to look for in your DSDT to know whether you need steps 5 and 6.
- Still required: the `AppleUSBXHCI::reset()` kernel patch (Install step 4, `Limit` 2048).
- The kext itself is unchanged apart from the version.
