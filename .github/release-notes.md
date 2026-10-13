First release.

Fixes painfully slow USB in macOS on AMD Carrizo, Bristol Ridge and Stoney Ridge machines, whose FCH xHCI
(`1022:7914`) interrupts never reach macOS. See the [README](https://github.com/CL0utM4N/FCHXHCIFix#readme) for the
affected machines, installation and how it works.

- Matches `1022:7914` only, in its own `IOMatchCategory` next to AppleUSBXHCIPCI. No dependencies.
- Boot-args: `-fchxhcioff` (disable), `fchxhcimode=1` (diagnosis only).
- Tested on an ASUS VivoBook 15 D509BA (A9-9425) with macOS Sequoia 15.4 and 15.8.1. Reports from other machines are
  welcome.
