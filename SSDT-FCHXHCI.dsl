/*
 * SSDT-FCHXHCI: keep the AMD FCH USB controllers (xHCI 1022:7914, EHCI) out of the firmware's D3cold power-gating under macOS.
 *
 * AMD's reference firmware for these APUs has a flag, XHCD, that arms D3cold for the xHCI. When it's set, XHC0._PS3
 * cuts the controller's power through an SMI and _PS0 has to reload the controller's firmware through another SMI;
 * macOS never gets the controller back (AppleUSBXHCIPCI "reset failed" 0xe00002c9, all USB 3 dead). The firmware's
 * start-up code (FINI, called from \_SB.PCI0._INI) leaves XHCD set only when the OS answers _OSI for Windows 8 or later,
 * which happens under macOS when an SSDT-XOSI / _OSI rename makes the firmware see Windows (for an I2C trackpad,
 * brightness keys, ...). Without such a rename XHCD is already clear and this SSDT changes nothing.
 *
 * The FCH's EHCI (USB 2) controller has the same flag, EHCD, and the same SMI power-off in EHC1._PS3; FINI leaves it set
 * in Windows 8+ mode too. On the ASUS VivoBook 15 D509BA EHCI happened to survive it; on a Dell Inspiron 3475 AIO the USB
 * 2 ports were dead. So EHCD is cleared as well.
 *
 * FXHC clears XHCD and EHCD again under macOS. Its _INI runs after \_SB.PCI0._INI because ACPI runs a parent's _INI
 * before its children's. Firmware without these flags is left alone (CondRefOf).
 *
 * Compile: iasl SSDT-FCHXHCI.dsl
 */
DefinitionBlock ("", "SSDT", 2, "FCHXHC", "FCHXHCI", 0x00001000)
{
    External (\XHCD, FieldUnitObj)
    External (\EHCD, FieldUnitObj)
    External (\_SB.PCI0, DeviceObj)

    Scope (\_SB.PCI0)
    {
        Device (FXHC)
        {
            Name (_HID, "FXHC0000")  // _HID: Hardware ID
            Method (_INI, 0, NotSerialized)  // _INI: Initialize
            {
                If (CondRefOf (\XHCD))
                {
                    \XHCD = Zero
                }

                If (CondRefOf (\EHCD))
                {
                    \EHCD = Zero
                }
            }

            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0B)  // present, enabled, not shown in UI (_INI only runs when present)
                }

                Return (Zero)
            }
        }
    }
}
