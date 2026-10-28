/*
 * SSDT-XOSI: let the firmware of AMD Carrizo / Bristol Ridge / Stoney Ridge laptops see Windows 10 under macOS.
 *
 * AMD's reference firmware for these machines sets its OS level from _OSI("Windows ...") checks (OSTP / OSFL setting
 * OSTB, TPOS or OSVR). macOS answers only _OSI("Darwin"), so the level stays at its minimum, and the HPET's _STA (which
 * needs "Windows 2006" or later) reports the HPET disabled and clears HPEN. macOS needs the HPET: without it the PS/2
 * keyboard and USB were dead on an ASUS VivoBook 15 D509BA, and an Acer Aspire E5-523 hung until a USB device was plugged
 * in. Found on both machines' DSDTs.
 *
 * Use with the OpenCore ACPI patch "_OSI" -> "XOSI" (Find 5F4F5349, Replace 584F5349, Table Signature DSDT, Count 0):
 * the firmware's OS checks then call XOSI, which claims every Windows version up to Windows 10 when macOS is running and
 * passes the question to the real _OSI on any other OS.
 *
 * Windows 8 and later also make the firmware arm the FCH xHCI's D3cold power-gating (FINI keeps XHCD set), which macOS
 * can't come back from: add SSDT-FCHXHCI.aml too, which clears it again.
 *
 * Compile: iasl SSDT-XOSI.dsl
 */
DefinitionBlock ("", "SSDT", 2, "FCHXHC", "XOSI", 0x00001000)
{
    Method (XOSI, 1, NotSerialized)
    {
        // Windows _OSI strings, oldest to newest (Microsoft "How to Identify the Windows Version in ACPI by Using _OSI").
        Local0 = Package ()
            {
                "Windows 2001",
                "Windows 2001.1",
                "Windows 2001 SP1",
                "Windows 2001 SP2",
                "Windows 2001 SP3",
                "Windows 2006",
                "Windows 2006 SP1",
                "Windows 2006 SP2",
                "Windows 2009",
                "Windows 2012",
                "Windows 2013",
                "Windows 2015"
            }
        If (_OSI ("Darwin"))
        {
            Return ((Ones != Match (Local0, MEQ, Arg0, MTR, Zero, Zero)))
        }
        Else
        {
            Return (_OSI (Arg0))
        }
    }
}
