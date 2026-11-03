/*
 * SSDT-FCHSLEEP: keep the AMD FCH USB controllers powered through S3 under macOS, so EHCI doesn't wake the machine
 * the moment it goes to sleep.
 *
 * AMD's reference firmware for these APUs prepares sleep in _PTS -> SPTS -> FPTS. For S3 and S4, FPTS powers the FCH's
 * xHCI and EHCI off through FDDC (device 0x17 and 0x12, D3) whenever XHCD / EHCD are clear, which is how
 * SSDT-FCHXHCI leaves them under macOS (so _PS3 doesn't power-gate the controllers at runtime). EHC1 is still armed as
 * a wake source (GPE 0x18) when that happens, and losing power wakes the machine at once into a dark wake, every sleep
 * (Dell Inspiron 3475 on Tahoe: "Wake reason: EHC1" 0.16 s after every S3 entry; EHC1's config space reads all ones
 * straight before S3; no USB port or hub counter moves, also with the webcam's hub port powered off). Turning EHC1's
 * wake off (_PRW) stops it too, but also stops keyboards on the USB 2 ports waking the machine and USB 2 devices being
 * seen when they're plugged in.
 *
 * This _PTS sets XHCD and EHCD for the length of the firmware's _PTS when macOS enters S3 or S4, so FPTS leaves both
 * controllers powered, then puts them back. S4 too: macOS's standby ("sleep params ... type 5", straight away with
 * StandbyNoDelay) prepares S4, and the Dell's second sleep of a boot was such a standby (EHC1 all ones again, woken by
 * EHC1 at once) while the S3-only first version had covered the first. macOS has already put the controllers in D3
 * by then (with the flags clear, so _PS3 didn't power-gate them), and they stay armed: a key press on any USB port
 * wakes the machine. On wake FWAK only powers them on again if they were off. Other sleep states and other OSes run
 * the firmware's _PTS as is. Firmware without these flags is left alone (CondRefOf).
 *
 * The Dell then still woke at once with "Wake reason: ?", with EHC1 quiet and also with the xHCI left to the firmware
 * (GPE 0x16, the Ethernet controller's PCIe port, pending before S3). That isn't the controllers' power: macOS arms
 * every _PRW device for wake. CarrizoRed's wake filter arms only the USB controllers at S3 entry.
 *
 * Needs the OpenCore ACPI patch renaming only the _PTS definition (DSDT: 5F50545301 -> 5A50545301, "_PTS" with its
 * argument count, Count 1), and SSDT-FCHXHCI (EHCD clear at runtime).
 * Compile: iasl SSDT-FCHSLEEP.dsl
 */
DefinitionBlock ("", "SSDT", 2, "FCHXHC", "FCHSLEEP", 0x00001000)
{
    External (\ZPTS, MethodObj)
    External (\XHCD, FieldUnitObj)
    External (\EHCD, FieldUnitObj)

    Method (\_PTS, 1, NotSerialized)  // _PTS: Prepare To Sleep
    {
        If ((_OSI ("Darwin") && ((Arg0 == 0x03) || (Arg0 == 0x04))))
        {
            Local0 = Zero
            Local1 = Zero
            If (CondRefOf (\XHCD))
            {
                Local0 = \XHCD
                \XHCD = One
            }

            If (CondRefOf (\EHCD))
            {
                Local1 = \EHCD
                \EHCD = One
            }

            \ZPTS (Arg0)
            If (CondRefOf (\XHCD))
            {
                \XHCD = Local0
            }

            If (CondRefOf (\EHCD))
            {
                \EHCD = Local1
            }
        }
        Else
        {
            \ZPTS (Arg0)
        }
    }
}
