/*
 * SSDT-FCHSLEEP: keep the AMD FCH's EHCI (USB 2) controller powered through S3/S4 under macOS, so it doesn't wake the
 * machine the moment it goes to sleep.
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
 * This _PTS sets EHCD for the length of the firmware's _PTS when macOS enters S3 or S4, so FPTS leaves EHCI powered,
 * then puts it back. macOS has already put the controller in D3 by then (with the flag clear, so _PS3 didn't
 * power-gate it), and it stays armed: a key press on a USB 2 port wakes the machine. On wake FWAK only powers it on
 * again if it was off. S4 too: macOS's standby ("sleep params ... type 5", straight away with StandbyNoDelay) prepares
 * S4, and the Dell's second sleep of a boot was such a standby (EHC1 all ones again, woken by EHC1 at once).
 * The xHCI is left to the firmware: the first version kept it powered too (XHCD as well), and the Dell then woke at once
 * with "Wake reason: ?" instead, with EHC1 quiet (log 1793784140); the xHCI's power-off never woke it before.
 * Other sleep states and other OSes run the firmware's _PTS as is. Firmware without the flag is left alone (CondRefOf).
 *
 * Needs the OpenCore ACPI patch renaming only the _PTS definition (DSDT: 5F50545301 -> 5A50545301, "_PTS" with its
 * argument count, Count 1), and SSDT-FCHXHCI (EHCD clear at runtime).
 * Compile: iasl SSDT-FCHSLEEP.dsl
 */
DefinitionBlock ("", "SSDT", 2, "FCHXHC", "FCHSLEEP", 0x00001000)
{
    External (\ZPTS, MethodObj)
    External (\EHCD, FieldUnitObj)

    Method (\_PTS, 1, NotSerialized)  // _PTS: Prepare To Sleep
    {
        If ((_OSI ("Darwin") && ((Arg0 == 0x03) || (Arg0 == 0x04))))
        {
            Local0 = Zero
            If (CondRefOf (\EHCD))
            {
                Local0 = \EHCD
                \EHCD = One
            }

            \ZPTS (Arg0)
            If (CondRefOf (\EHCD))
            {
                \EHCD = Local0
            }
        }
        Else
        {
            \ZPTS (Arg0)
        }
    }
}
