# FAQ

**Which radios does NOVA support?**
The ATS Mini V3 and V4 (ESP32-S3 + SI4732, 8 MB flash). Other ESP32-S3 + SI4732 radios with the same wiring may work
but are not tested.

**Will I lose the original firmware?**
Only if you skip the backup. NOVA Installer saves the whole flash in two minutes and restores it in one.

**The screen stays dark after installing.**
Switch the radio off and on again. After an install over USB the radio does not always restart by itself; if it
still stays dark, unplug the USB cable and switch it on again.

**My radio shows "Memory not found".**
It needs the other build (OSPI or QSPI). Run the installer again and pick the other one in step 2.

**Can NOVA play internet radio, record, or use Bluetooth audio?**
No. On the ATS Mini the sound goes from the radio chip straight to the amplifier; the processor has no audio path and
no audio converter, so it cannot play or record sound. The ESP32-S3 also has no Bluetooth Classic audio.
The editor records on your computer instead.

**Does NOVA use Wi-Fi?**
Only when you ask it to update the station names (EiBi) or set the clock. The radio joins your network with its sound
off, does the job, and switches Wi-Fi off again. Wi-Fi is never on while you listen. The network is saved from the
editor, over USB.

**Why are some shortwave names wrong or missing?**
Names come from the EiBi schedule for the current time. Set the clock (FM RDS, Wi-Fi or editor) and update the schedule
twice a year. A station not in EiBi shows its band name.

**The editor does not see my radio.**
Use Chrome, Edge or Opera on a computer, a USB data cable, and close the installer or any serial monitor.
See [installing.md](installing.md#the-radio-is-not-found).

**I hear noise when the screen changes.**
Like all ATS Mini firmware, drawing on the screen can be heard on some AM and shortwave frequencies. Lower the
brightness, use a dark theme, or set a screen timeout.

**Is the Ghost Box real?**
It is a fast radio sweep with a complete log. NOVA makes no claim about what you hear; the log, the source check and
blind mode are there so you can judge for yourself.
