/*
 * SSDT-FCHSLEEP: keep the AMD FCH's USB 2 controller (EHCI) powered through S3 under macOS, so it doesn't wake the
 * machine the moment it goes to sleep, and leave the USB 3 controller (xHCI) to the firmware, so USB 3 keyboards can
 * wake it.
 *
 * AMD's reference firmware for these APUs prepares sleep in _PTS -> SPTS -> FPTS. For S3 and S4, FPTS powers the FCH's
 * EHCI off through FDDC (device 0x12, D3) whenever EHCD is clear, which is how SSDT-FCHXHCI leaves it under macOS (so
 * _PS3 doesn't power-gate the controller at runtime). EHC1 is still armed as a wake source (GPE 0x18) when that
 * happens, and losing power wakes the machine at once into a dark wake, every sleep (Dell Inspiron 3475 on Tahoe:
 * "Wake reason: EHC1" 0.16 s after every S3 entry; EHC1's config space reads all ones straight before S3). Turning
 * EHC1's wake off (_PRW, e.g. SSDT-GPRW) stops it too, but also stops keyboards on the USB 2 ports waking the machine
 * and USB 2 devices being seen when they're plugged in.
 *
 * This _PTS sets EHCD for the length of the firmware's _PTS when macOS enters S3 or S4, so FPTS leaves EHCI powered,
 * then puts it back. S4 too: macOS's standby ("sleep params ... type 5") prepares S4. macOS has already put EHCI in
 * D2/D3 by then (with the flag clear, so _PS3 didn't power-gate it), and it stays armed: a key press on a USB 2 port
 * wakes the machine. On wake FWAK only powers it on again if it was off. Other sleep states and other OSes run the
 * firmware's _PTS as is. Firmware without the flag is left alone (CondRefOf).
 *
 * The xHCI goes through FPTS as the firmware intends: FDDC (0x17, 3) -> U3D3 saves the controller's state (X0_S) and
 * hands it to the FCH for Sx (SMI 0x67); on wake FWAK -> U3D0 reloads its firmware and restores it. An earlier version
 * set XHCD here too and skipped that: macOS armed the xHCI's ports for wake properly (U3, wake enables, PME_En), but no
 * wake ever reached the FCH, so USB 3 keyboards never woke the machine (Dell, Tahoe, log 1793877366). Without XHCD set
 * the Dell wakes from both (log 1793879112).
 *
 * The other wake problems of these machines are fixed in CarrizoRed at S3 entry (wake GPE filter, the FCH's shared
 * USB wake lines, EHCI in D2, the internal hub's wake enables); see its docs/SLEEP.md.
 *
 * Needs the OpenCore ACPI patch renaming only the _PTS definition (DSDT: 5F50545301 -> 5A50545301, "_PTS" with its
 * argument count, Count 1), and SSDT-FCHXHCI when SSDT-XOSI is used (EHCD clear at runtime).
 * Compile: iasl SSDT-FCHSLEEP.dsl
 */
DefinitionBlock ("", "SSDT", 2, "FCHXHC", "FCHSLEEP", 0x00001100)
{
    External (\ZPTS, MethodObj)
    External (\EHCD, FieldUnitObj)

    Method (\_PTS, 1, NotSerialized)  // _PTS: Prepare To Sleep
    {
        If ((_OSI ("Darwin") && ((Arg0 == 0x03) || (Arg0 == 0x04))))
        {
            Local1 = Zero
            If (CondRefOf (\EHCD))
            {
                Local1 = \EHCD
                \EHCD = One
            }

            \ZPTS (Arg0)
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
