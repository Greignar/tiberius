# tiberius
Convoy Flashlight Firmware (S2+, S8 and others with a power switch)

Features of the firmware:

No alterations to the driver, except for releasing the controller's leg, with the help of which the manufacturer made protection against changing the firmware.

After flashing or resetting, the flashlight has the following brightness modes: 0.39%, 1.56%, 6.25%, 25%, 100%. The flashlight always starts with the mode of 6.25%. A single click switches the modes in order: 6.25%, 25%, 100%, 6.25%, etc. Modes 0.39% and 1.56% are available only by double-clicking.

Fully customizable brightness in five modes with the ability to change the number of modes from one to five. The ability to quickly switch to the maximum and minimum levels of brightness. Battery charge indication. Battery discharge protection. Overheat protection (in time). SOS mode.

In the selective setting mode, the following brightness levels (steps) are available: 0%, 0.39%, 0.78%, 1.56%, 3.13%, 6.25%, 12.5%, 25%, 50%, 100%.

Protection against battery discharge: limiting the maximum brightness to 6.25% with a decrease in battery charge of less than 3.2 volts, decrease in brightness to 0.78% (with a period of 5 seconds for 1 step of reducing brightness) when charging a battery less than 3.0 volts, and turning off the flashlight when charging a battery less than 2.8 volt.

Protection against overheating and premature discharge of the battery: decrease in brightness to 25% of the maximum (with a period of 3 minutes for 1 step of reducing brightness).

All of the above brightness drops are reset by a single click.

SOS signals are sent with a one-minute period. SOS mode brightness - 50%. The relative energy consumption in SOS mode is 2.5% of the maximum consumption. In SOS mode, there is no protection against discharge or overheating.

The flashlight has two main modes: normal mode and configuration mode. In the setup mode, you can enter 9 clicks from the normal mode, and exit - turning off the flashlight.

Normal mode

Click 1: The next brightness mode (in a circle, starting with the "Click 4" mode of the selective setting mode)
Click 2: Previous brightness mode (only down)
Click 3: Maximum Brightness
Click 4: Minimum Brightness
Click 5: SOS mode
Click 6: Battery level
Click 9: Setup Mode

Setup mode

Click 1: The next brightness mode (in a circle, starting with the "Click 4" mode of the selective setting mode)
Click 2: Previous brightness mode (only down)
Click 3: Set brightness levels (for all five modes)
Click 4: Set the initial brightness level (for "Click 1" mode)
Click 9: Reset to the original state

Setting the brightness levels is carried out in two passes. The first pass is the enumeration of modes from the first to the fifth. After a click, the brightness levels are searched from the brightness of the previous mode to the ninth level of brightness. After clicking the level of brightness is maintained. This operation can be repeated for all five modes. The brightness level 0 (0%) is ignored, i.e. setting the brightness level for the first two modes to 0 you will get a flashlight with three modes. The maximum brightness mode (100%) completes the group, i.e. setting the brightness level for the third mode to 100% you will get a flashlight with three modes.

The brightness level is selected visually, the user sees the brightness level he needs and fixes it with a single click.

Data in the EEPROM is written only in the selective setting mode.

The firmware is installed by the command:
avrdude -c usbasp -p t13 -u -Uflash:w:tiberius.hex -Ulfuse:w:0x75:m -Uhfuse:w:0xFD:m
