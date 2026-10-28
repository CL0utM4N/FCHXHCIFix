USB 3 on these machines needs the kext **and** an OpenCore kernel patch. The README's Install section now lists both,
and this release adds an SSDT for one remaining case.

- **Install step 4 (required): the `AppleUSBXHCI::reset()` timeout patch,** with every `config.plist` key. macOS waits
  100 ms for the controller to reset; it needs ~105 ms, so without the patch macOS switches the controller off and USB 3
  stays dead whatever the kext does.
- **Monterey:** the patch needs `Limit` **2048**. Its bytes sit 571 bytes into `reset()` on Sequoia but 1078 on
  Monterey 12.5.1, so the 1024 used before skipped Monterey's copy (the installer then waits for its root device). There
  is one match in each version.
- **New: `SSDT-FCHXHCI.aml`** in the zip, for configs that make the firmware see Windows 8 or later (an `SSDT-XOSI` with
  an `_OSI` → `XOSI` rename). In that mode AMD's reference firmware arms a D3cold path for the controller that macOS
  can't come back from; the SSDT clears that flag (`XHCD`) under macOS. It does nothing on firmware without the flag.
  Not needed without such a rename.
- The kext itself is unchanged apart from the version.
- Tested on an ASUS VivoBook 15 D509BA (A9-9425): installed Ventura 13.7.8, Sonoma 14 and Sequoia 15.8.1, and the
  Monterey 12.5.1 installer.
