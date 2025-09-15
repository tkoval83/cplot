# EBB Command Set Documentation
Eggbot
------

Software for the Original EggBot Kit

[View the Project on GitHub evil-mad/EggBot](https://github.com/evil-mad/EggBot)

*   [View On **GitHub**](https://github.com/evil-mad/EggBot)

EBB (EiBotBoard) Command Set, v1.8.0 - v2.8.1
---------------------------------------------

This document details the serial command protocol used by the [EBB](http://www.schmalzhaus.com/EBB/) (EiBotBoard) with older firmware, firmware v1.8.0 - v2.8.1. Current documentation for firmware v3.0 and above is in separate documentation [available here](ebb.html).

The EBB is an open source USB-based motor control board, designed to drive two stepper motors. The EBB may be used on its own, or found as the control board of such machines as [The Original Egg-Bot](http://egg-bot.com/), [The WaterColorBot](http://watercolorbot.com/), or [AxiDraw](http://axidraw.com/).

* * *

Contents
--------

1.  [Serial communication and APIs](#apis)
2.  [Introduction to the EBB firmware](#introduction)
3.  [Differences between versions](#version_differences)
4.  [Updating firmware](#updating)
5.  [Addressing issues](#issues)
6.  [Additional resources](#additional_resources)
7.  [Command reference](#EBB_Command_Reference)

1.  [Syntax and conventions](#syntax)
2.  [List of commands](#commands)

9.  [Initial I/O pin configuration](#states)
10.  [Performance](#performance)
11.  [FAQ](#faq)
12.  [License](#license)

* * *

Serial communication: High-level interfaces and APIs
----------------------------------------------------

The serial protocol described in this document can be used directly, for example from a serial terminal, in order to carry out simple tasks. It can also be accessed from within any program or programming environment that is capable of communicating with a USB serial device. Using this protocol from within high-level computer languages allows one to construct and execute complex motion. All EBB applications and interfaces use this serial protocol, at their lowest levels, in order to manage the motion control.

The serial protocol specifies the fundamental primitives of how the machine operates— for example moving from position (_a1_,_b1_) to position (_a2_,_b2_) in duration Δ_t_, with the pen-lift servo motor at position _z_. By contrast, higher level programs may perform tasks such as opening up SVG files and converting them into a set of robotic movements that can be carried out through the serial protocol.

Here are some possible starting points for building higher level applications:

*   The Inkscape-based drivers [for the EggBot](https://github.com/evil-mad/EggBot), [for the WaterColorBot](https://github.com/evil-mad/wcb-ink), and [for AxiDraw](https://github.com/evil-mad/axidraw) are written in python, and use this serial protocol. The codebases from those projects are excellent resources for getting started.
*   The [Processing](http://processing.org/)\-based program [RoboPaint RT](https://github.com/evil-mad/robopaint-rt) is designed to control the WaterColorBot through a real-time interface. This program is written in Processing (Java), and serves as a good example of how to manage the EBB through Processing.
*   [RoboPaint](https://github.com/evil-mad/robopaint) is a stand-alone cross-platform program to drive the WaterColorBot (as well as EggBot and AxiDraw). RoboPaint is written in javascript and (while running) provides several APIs that can be used to control machines based on the EBB:
    *   RoboPaint, under the hood, uses the [cncserver](https://github.com/techninja/cncserver/) and its RESTful API to operate. It is a relatively low level interface, with similar functionality to the serial protocol, plus a few helpful utilities.
    *   The higher-level [RoboPaint remote print API](https://github.com/evil-mad/robopaint-mode-remote/blob/master/API.md) allows local or remote "printing" of SVG files to EBB based machines, when attached to a computer running RoboPaint.
    *   The simplified ("GET only") [Scratch API](https://github.com/techninja/cncserver/blob/master/scratch/SCRATCH.API.md) provides a method of controlling EBB based machines from the address bar of a web browser, or from simple programming languages that can retrieve data from an URL.
*   [cncserver](https://github.com/techninja/cncserver/) can be run on its own, from the command line, as a javascript-based RESTful API server to control EBB-based machines. (You can also run it by simply launching RoboPaint.)

* * *

Introduction to the EBB firmware
--------------------------------

The EBB firmware is based on the [UBW](http://www.schmalzhaus.com/UBW/Doc/FirmwareDDocumentation_v145.html) firmware. The same basic command processing framework is used, so the same type of commands are used, and the same type of errors are returned. See the [UBW command documentation](http://www.schmalzhaus.com/UBW/Doc/FirmwareDDocumentation_v145.html) for an introduction to these topics.

This command reference applies to EiBotBoard Firmware v1.8.0 - v2.8.1. (Any differences between firmware versions are noted below and in release notes.) The documentation for firmware v3.0 and above is in separate documentation [available here](ebb.html).

* * *

Differences between firmware versions
-------------------------------------

Although the EBB firmware is a continuously evolving code base, we have, since version 2.0.1, taken care to avoid compatibility changes that would affect the most common machines using the EBB: The AxiDraw, EggBot, and WaterColorBot. If you are using one of these machines, there is generally no particular benefit or compelling reason for you to update your firmware to a newer version. (Older machines with older firmware will continue to work just fine with that older firmware.)

That said, there have been [many smaller changes](EBBReleaseNotes.html) in the code between the versions that ship preloaded on the EBB and the latest versions. Most of the changes have been minor bug fixes and new, minor functions that are helpful for less common applications. If you are developing new applications with the EBB, we do encourage you to update to the newest version. If, on the other hand, you are writing new software that targets machines of various ages (for example, new EggBot software), please be aware that many of the machines out there are still using older firmware revisions.

That this documentation refers not only to different versions of the EBB firmware, but also in some cases to legacy versions of the EBB hardware. For example, the "EM" command has different behaviors on different versions of the EBB hardware.

* * *

Updating your firmware
----------------------

Instructions for updating firmware, including easy installers for Mac and Windows, can be found on the [Evil Mad Scientist Wiki](https://wiki.evilmadscientist.com/Updating_EBB_firmware).

* * *

Addressing Issues
-----------------

If you discover something that does not work as expected in the EBB firmware, please contact us by [e-mail](http://shop.evilmadscientist.com/contact), in our forum, or (preferably) file an issue on GitHub : [https://github.com/evil-mad/EggBot/issues](https://github.com/evil-mad/EggBot/issues).

Please include a clear method to reproduce the issue, the expected behavior as well as the observed behavior, and the version number of the EBB firmware version that you are using.

* * *

Additional Resources
--------------------

*   [Boot up configuration information](http://www.schmalzhaus.com/EBB/EBBConfig.html)
*   Hardware documentation for the EBB can be found on the [main EBB page](http://www.schmalzhaus.com/EBB/).
*   [EBB firmware release notes](EBBReleaseNotes.html) for versions 2.0.1 up through the newest version.

* * *

EBB Command Reference
---------------------

### Syntax and conventions

The following syntax conventions are established to assist with clarity of communication as we describe the EBB commands.

#### EBB Syntax Conventions:

*   Command format descriptions and examples are set in `code font`, with a shaded background.
*   Query response format descriptions and examples are also set in the same `code font`.
*   _Italics_ are used to represent variables and adjustable parameters.
*   Square brackets (`[ ]`) are used to enclose optional items.
*   Angle brackets (`< >`) are used to represent individual control characters, such as `<CR>` for carriage return or `<NL>` for newline, in command and query format descriptions.
*   Individual control characters may also be given by their backslash-escaped representation, as in `\r` for carriage return or `\n` for linefeed, in the context of literal command examples.
*   All unitalicized text and punctuation within a command description must be used literally.

#### Additionally, please note that:

*   All commands are composed of ASCII characters
*   All commands are case insensitive.
*   No whitespace (including spaces, tabs, or returns) is allowed within a command.
*   All commands must have a total length of 64 characters or fewer, including the terminating `<CR>` .

### The EBB Command Set  

*   [A](#A) — Analog value get
*   [AC](#AC) — Analog Configure
*   [BL](#BL) — enter BootLoader
*   [C](#C) — Configure
*   [CN](#CN) — Clear Node count
*   [CK](#CK) — Check Input
*   [CU](#CU) — Configure User Options
*   [CS](#CS) — Clear Step position
*   [EM](#EM) — Enable Motors
*   [ES](#ES) — E Stop
*   [HM](#HM) — Home or Absolute Move
*   [I](#I) — Input
*   [LM](#LM) — Low-level Move
*   [LT](#LT) — Low-level Move, Time Limited
*   [MR](#MR) — Memory Read
*   [MW](#MW) — Memory Write
*   [ND](#ND) — Node count Decrement
*   [NI](#NI) — Node count Increment
*   [O](#O) — Output
*   [PC](#PC) — Pulse Configure
*   [PD](#PD) — Pin Direction
*   [PG](#PG) — Pulse Go
*   [PI](#PI) — Pin Input
*   [PO](#PO) — Pin Output
*   [QB](#QB) — Query Button
*   [QC](#QC) — Query Current
*   [QE](#QE) — Query motor Enables and microstep resolutions
*   [QG](#QG) — Query General
*   [QL](#QL) — Query Layer
*   [QM](#QM) — Query Motors
*   [QN](#QN) — Query Node count
*   [QP](#QP) — Query Pen
*   [QR](#QR) — Query RC Servo power state
*   [QS](#QS) — Query Step position
*   [QT](#QT) — Query EBB nickname Tag
*   [RB](#RB) — Reboot
*   [R](#R) — Reset
*   [S2](#S2) — General RC Servo Output
*   [SC](#SC) — Stepper and servo mode Configure
*   [SE](#SE) — Set Engraver
*   [SL](#SL) — Set Layer
*   [SM](#SM) — Stepper Move
*   [SN](#SN) — Set Node count
*   [SP](#SP) — Set Pen State
*   [SR](#SR) — Set RC Servo power timeout
*   [ST](#ST) — Set EBB nickname Tag
*   [T](#T) — Timed Digital/Analog Read
*   [TP](#TP) — Toggle Pen
*   [V](#V) — Version Query
*   [XM](#XM) — Stepper Move, for Mixed-axis Geometries

* * *

#### "A" — Analog value get

*   Command: `A<CR>`
*   Response: `A,_channel_:_value_,_channel_:_value_` . . . `<CR><NL>`
*   Firmware versions: v2.2.3 and newer
*   Execution: Immediate
*   Description:
    
    Query all analog (ADC) input values.
    
    When one or more analog channels are enabled (see `[AC](#AC)` command below), the "A" query will cause the EBB to return a list of all enabled channels and their associated 10 bit values.
    
    The list of channels and their data will always be in sorted order from least (channel) to greatest. Only enabled channels will be present in the list.
    
    The _value_ returned for each enabled channel will range from 0 (for 0.0 V on the input) to 1023 (for 3.3 V on the input).
    
    The channel number and ADC value are both padded to 2 and 4 characters respectively in the A response.
    
*   Example Return Packet: `A,00:0713,02:0241,05:0089:09:1004<CR><NL>` if channels 0, 2, 5 and 9 are enabled.
*   NOTE 1: The EBB's analog inputs are only rated for up to 3.3 V. Be careful not to put higher voltages on them (including 5 V) or you may damage the pin.
*   NOTE 2: If you connect an ADC pin to GND (0.0 V) it will likely not read exactly 0. It will be a bit higher than that. (A typical value is 0023.) This is because of the way that the analog input reference voltages are supplied to the microcontroller.
*   Version History: For firmware versions prior to v2.2.3, a different "A" query format is used, identical to the the UBW "A" query. (See [UBW documentation](http://www.schmalzhaus.com/UBW/Doc/FirmwareDDocumentation_v145.html) for details.) These queries were rewritten for v2.2.3, with the AC command added and the A query modified to return a list that includes the channel number as well as the ADC value for each channel.

* * *

#### "AC" — Analog Configure

*   Command: `AC,_channel_,_enable_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.2.3 and newer
*   Execution: Immediate
*   Arguments:
    *   _channel_: An integer in the range of 0 to 15 (inclusive). The analog input channel number that you wish to enable or disable.
    *   _enable_: A value of either 1 or 0. A value of 1 will enable the channel. A value of 0 will disable the channel.
*   Description:
    
    Configure an analog input channel.
    
    Use this command to turn on or turn off individual analog channels. Once a channel is turned on, it will begin converting analog values to digital values and the results of the conversions will be displayed in the returned value of the "A" Command. See "A" command above. You can turn on and off any of the 16 analog channels individually on this microcontroller. Once a channel is turned off, it will no longer show up in the "A" packet returned value list.
    
    The channel numbers correspond to lines ANx on the EBB schematic and on the datasheet of the microcontroller. For example, pin 11 of the PIC, which is labeled RB2 and comes out as the RB2 pin on the servo header, has the text "RB2/AN8/CTEDG1/PMA3/VMO/REFO/RP5" next to it on the CPU symbol. This means that this pin is internally connected to Analog Channel 8. See chapter 21 of the [PIC18F46J50 datasheet](http://ww1.microchip.com/downloads/en/DeviceDoc/39931d.pdf) to read more about the ADC converter.
    

* * *

#### "BL" — Enter Bootloader

*   Command: `BL<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9.5 and newer (with changes)
*   Execution: Immediate
*   Description:
    
    Enter bootloader mode.
    
    This command turns off interrupts and jumps into the bootloader, so that new firmware may be uploaded. This particular method of entering bootloader mode is unique in that no physical button presses are required.
    
    Once in bootloader mode, the EBB will not be able to communicate using the same USB serial port method that the normal firmware commands use. A special bootloader PC application (that uses USB HID to communicate with the bootloader on the EBB) must be run in order to upload new firmware HEX files to the EBB.
    
*   Version History: Added in v1.9.5.
    
    This command will ONLY work if you have a EBB bootloader version later than 7/3/2010 (the version released on 7/3/2010 has a distinct LED blink mode - the USB LED stays on 3 times longer than the USR LED). With a previous version of the bootloader code, this command may cause the EBB to become unresponsive.
    

* * *

#### "C" — Configure (pin directions)

*   Command: `C,_PortA_IO_,_PortB_IO_,_PortC_IO_,_PortD_IO_,_PortE_IO_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _PortA\_IO_: An integer from 0 to 255. Value written to TRISA register.
    *   _PortB\_IO_: An integer from 0 to 255. Value written to TRISB register.
    *   _PortC\_IO_: An integer from 0 to 255. Value written to TRISC register.
    *   _PortD\_IO_: An integer from 0 to 255. Value written to TRISD register.
    *   _PortE\_IO_: An integer from 0 to 255. Value written to TRISE register.
*   Description:
    
    This command takes five bytes worth of parameters, one for each TRISx register in the processor, and writes those values down into the TRIS registers. There is a TRIS register for each 8-bit wide I/O port on the processor, and it controls each pin's direction (input or output). A 0 in a pin's bit in the TRIS register sets the pin to be an output, and a 1 sets it to be an input.
    
    This command is useful if you want to set up the pin directions for each and every pin on the processor at once. If you just one to set one pin at a time, use the `PC` command.
    
    This command does not need to be used to configure analog inputs. The `AC` command is used for that.
    

* * *

#### "CN" — Clear node count \[obsolete\]

*   Command: `CN<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: Removed, as of v1.9.5
*   Execution: Immediate
*   Description:
    
    Clear the value of the Node Counter.
    
    See the `[QN](#QN)` command for a description of the node counter and its operations.
    
*   Version History: Obsolete.
    
    Added in v1.9.3, removed in v1.9.5
    

* * *

#### "CS" — Clear Step position

*   Command: `CS<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: Added in v2.4.3
*   Execution: Immediate
*   Description:
    
    This command zeroes out (i.e. clears) the global motor 1 step position and global motor 2 step position.
    
    See the `[QS](#QS)` command for a description of the global step positions.
    
*   Version History:
    
    Added in v2.4.3
    

* * *

#### "CK" — Check Input

*   Command: `CK,_pVal_1_,_pVal_2_,_pVal_3_,_pVal_4_,_pVal_5_,_pVal_6_,_pVal_7_,_pVal_8_<CR>`
*   Response:
    
    `Param1=`_pVal\_1_`<CR><NL>`
    
    `Param2=`_pVal\_2_`<CR><NL>`
    
    `Param3=`_pVal\_3_`<CR><NL>`
    
    `Param4=`_pVal\_4_`<CR><NL>`
    
    `Param5=`_pVal\_5_`<CR><NL>`
    
    `Param6=`_pVal\_6_`<CR><NL>`
    
    `Param7=`_pVal\_7_`<CR><NL>`
    
    `Param8=`_pVal\_8_`<CR><NL>`
    
    `OK<CR><NL>`
    
*   Firmware versions:All
*   Execution: Immediate
*   Arguments:
    *   _pVal\_1_ An unsigned one byte integer from 0 to 255.
    *   _pVal\_2_ A signed one byte integer from -128 to 127.
    *   _pVal\_3_ An unsigned two byte integer from 0 to 65535.
    *   _pVal\_4_ A signed two byte integer from -32768 to 32767.
    *   _pVal\_5_ An unsigned four byte integer from 0 to 4294967295.
    *   _pVal\_6_ A signed four byte integer from -2147483648 to 2147483647.
    *   _pVal\_7_ A case sensitive character.
    *   _pVal\_8_ A case forced upper case character.
*   Description:
    
    This command is used to test out the various parameter parsing routines in the EBB. Each parameter is a different data type. The command simply prints out the values it parsed to allow the developer to confirm that the parsing code is working properly.
    
    For _pVal\_7_, any type-able character is accepted as input.
    
    For _pVal\_8_, any type-able character is accepted, and converted to upper case before printing.
    

* * *

#### "CU" — Configure User Options

*   Command: `CU,_Param_Number_,_Param_Value_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Param\_Number_ The value 1 or 2. Specifies what _Param\_Value_ means.
    *   _Param\_Value_ An integer from -32768 to 32767. Acceptable values depend on value of _Param\_Number_
*   Description:
    
    This command allows for configuring some run time options. The configuration options chosen with this command do not survive a reboot, and they will return to their default values on a reset or boot.
    
    *   When _Param\_Number_ = 1 : If _Param\_Value_ = 0, then `OK` response to commands is disabled.
    *   When _Param\_Number_ = 1 : If _Param\_Value_ = 1, then `OK` response to commands is enabled (default).
    
    Turning off the `OK` response can help speed up the execution of many commands back to back.
    
    *   When _Param\_Number_ = 2 : If _Param\_Value_ = 0, then `SM` command parameter limit checking is disabled.
    *   When _Param\_Number_ = 2 : If _Param\_Value_ = 1, then `SM` command parameter limit checking is enabled (default).
    
    Turning off the limit checking for the `SM` command will prevent error messages from being sent back to the PC, which may make processing of the data returned from the EBB easier.
    
    *   When _Param\_Number_ = 3 : If _Param\_Value_ = 0, then the red LED will not be used to indicate an empty FIFO (default).
    *   When _Param\_Number_ = 3 : If _Param\_Value_ = 1, then the red LED will be used to indicate an empty FIFO.
    
    Using the red LED to indicate an empty FIFO can aid in debugging certain types of problems. When enabled, this option will cause the red LED (labeled "USR" on the board) to light any time there is no motion command in the FIFO.
    
*   Version History:
    
    `CU,3,0` and `CU,3,1` were added in v2.8.1
    

* * *

#### "EM" — Enable Motors

*   Command: `EM,_Enable1_,_Enable2_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Added to FIFO motion queue
*   Arguments:
    
    For each stepper motor (_Enable1_ for motor1 and _Enable2_ for motor2), an integer in the range of 0 through 5, inclusive. An _Enable_ value of 0 will disable that motor (making it freewheel), while a nonzero value will enable that motor. This command is also used to set the step resolution of the stepper motors.
    
    The allowed values of _Enable1_ are as follows:
    
    *   0: Disable motor 1
    *   1: Enable motor 1, set global step mode to 1/16 step mode (default step mode upon reset)
    *   2: Enable motor 1, set global step mode to 1/8 step mode
    *   3: Enable motor 1, set global step mode to 1/4 step mode
    *   4: Enable motor 1, set global step mode to 1/2 step mode
    *   5: Enable motor 1, set global step mode to full step mode
    
    The allowed values of _Enable2_ are as follows:
    
    *   0: Disable motor 2
    *   1 through 5: Enable motor 2 (at whatever the previously set global step mode is)
*   Description:
    
    Enable or disable stepper motors and set step mode.
    
    Each stepper motor may be independently enabled (energized) or disabled (causing that motor to freewheel). When disabled, the driver will stop sending current to the motor, so the motor will "freewheel" — it will not be actively driven, but instead will present little resistance to being turned by external torques.
    
    When enabled, the stepper motor driver actively drives current through the coils, causing the motors to 'lock' (i.e. be very difficult to turn by external torques).
    
    Each of the motor movement commands (like SM, XM, and LM) automatically enable both motors before they begin their motion, but do not change the global step mode.
    
    The stepper motors may be configured to be in whole, half, quarter, eighth, or sixteenth step modes. When using a motor with a native resolution of 200 steps per revolution, these settings would produce effective stepping resolutions of 200, 400, 800, 1600, and 3200 steps per revolution, respectively. Using fine sub-steps ("microstepping") gives higher resolution at the cost of decreasing step size reproducibility and decreasing maximum step speed. Note that the microstep mode is set for both motors simultaneously, using the parameter value of _Enable1_. It is not possible to set the step mode separately for each motor. Thus there is just one global step mode, and it is set by the value of _Enable1_.
    
    Because only _Enable1_ can set the global step mode, _Enable2_ simply enables or disables axis 2, and can not change the previously set step mode on its own.
    
*   Example: `EM,1,0\r` Enable motor 1, set global step mode to 1/16th and disable motor 2
*   Example: `EM,2,1\r` Set global step mode to 1/8 enable motor 1 and motor 2
*   Example: `EM,3,3\r` Set global step mode to 1/4 and enable both motors.
*   Example: `EM,0,1\r` Enable motor 2, disable motor 1, and continue to use previously set global step mode
*   Example: `EM,0,0\r` Disable both motors (both will freewheel)
*   Example: `EM,3,1\r` Enable both motors and set to 1/4 step mode
*   Version History:
    
    Starting with v2.6.2, the global step counters (available with the QS command) are zeroed out any time this command is executed.
    
    With all versions up to and including v2.7.0, the second parameter `_Enable2_` was optional. If it was not included in the command the motor 2's enable would not be changed.
    
    Starting with v2.8.0, this command is placed on the motion FIFO along with the other motion commands so that it occurs at a precise time in the motion sequence.
    

* * *

#### "ES" — E Stop

*   Command: `ES[,DisableMotors]<CR>`
*   Response:  
    `_interrupted_,_fifo_steps1_,_fifo_steps2_,_steps_rem1_,_steps_rem2_<NL><CR>OK<CR><NL>`
*   Firmware versions: v2.2.7 and newer (with changes)
*   Execution: Immediate
*   Description:
    
    Use this query to abort any in-progress motor move (SM) Command. This query will also delete any motor move command (SM) from the FIFO. It will immediately stop the motors, but leave them energized. In addition, the it queries several FIFO related status values and returns their values.
    
    For versions v2.8.0 and above there is an optional parameter `DisableMotors`. If `DisableMotors` is set to 1, then this query will not only stop all motion but will also disable both stepper motors.
    
    Returned values:
    
    *   _interrupted_: 0 if no FIFO or in-progress move commands were interrupted, 1 if a motor move command was in progress or in the FIFO
    *   _fifo\_steps1_ and _fifo\_steps1_: 24 bit unsigned integers with the number of steps in any SM command sitting in the fifo for axis1 and axis2.
    *   _steps\_rem1_ and _steps\_rem2_: 24 bit unsigned integers with the number of steps left in the currently executing SM command (if any) for axis1 and axis2.
*   Example Return Packet: `0,0,0,0,0<NL><CR>OK<CR><NL>` Indicates that no SM command was executing at the time, and no SM command was in the FIFO.
*   Example Return Packet: `1,50,1200,18634,9848<NL><CR>OK<CR><NL>` Indicates that a command was interrupted, and that the FIFO had steps of 50 and 1200 steps queued to execute next (in axes 1 and 2), as well as 18634 and 9848 steps remaining to move (in axes 1 and 1) on the current move.
*   Version History:
    *   v2.8.0
        
        In versions v2.8.0 and beyond, the optional `DisableMotors` parameter is accepted.
        
    *   Added in v2.2.7
        
        Prior to v2.2.9, this query will return a "1" if it had to stop an in-progress move command, or if it had to delete a move command from the FIFO. This could indicate that some steps might be lost, since the host application may think that moves have already completed, when in fact they were aborted partway through. It will return a "0" if no motor move commands were deleted or aborted.
        
        Returned values:
        
        *   `1<NL><CR>OK<CR><NL>`: If an in-progress move commands was interrupted or removed from the FIFO
        *   `0<NL><CR>OK<CR><NL>`: If no in-progress move commands was interrupted or removed from the FIFO

* * *

#### "HM" — Home or Absolute Move

*   Command: `HM,_StepFrequency_[,_Position1_,_Position2_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.6.2 and newer (with changes)
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _StepFrequency_ is an unsigned integer in the range from 2 to 25000. It represents the step frequency, in steps per second, representing typical speed during the movement.
    *   _Position1_ and _Position2_ (optional) are signed integers in the range of ±4,294,967. If provided, they represents the position, relative to home, that motor1 and motor 2 will travel to.
*   Description:
    
    This command will cause the two stepper motors to move from their current position, as defined by the global step counters, either to Home (0, 0) or to a new position that you specify relative to the Home position. It is worth noting that this is the only EBB motion command for which you can specify an absolute position to move to; all other motion commands are relative to the current position. This command is intended for "utility" moves, to or from a specific point, rather than for smooth or fast motion.
    
    The current position at any given moment is stored in the global step counters, and can be read with the `[QS](#QS)` query. This position _does not_ refer to an absolute position in physical space, but rather the location where the motors were enabled. The global step counters are reset to zero whenever the motors are enabled, disabled, or have their microstep size changed (all via the `EM` command). The step counter can also be cleared directly by the `[CS](#CS)` command.
    
    The step rate at which the move should happen is specified as a parameter. If no destination position is specified, then the move is towards the Home position (0, 0).
    
    Take note that the move _may not be a straight line_. There are circumstances (where one axis has many steps to take, and the other has very few) where the homing operation is broken down into to move segments to prevent a step rate on the small axis from being lower than 1.3Hz. The `HM` command takes care of this internally.
    
    The command will wait until all previous motor motion ceases before executing. There is also a further delay, typically about 5 ms, between when the `HM` command begins execution and when its motion actually begins.
    
    If either of the global step counter values or the distance to move is greater than 4,294,967, the command will error out. If you need to move a larger distance, multiple normal moves back to zero can be sent to accomplish the same thing.
    
*   Version History: Added in v2.6.2, with only the ability to move to the Home position. Expanded in v2.7.0 to allow for moves to any arbitrary position with respect to home.

* * *

#### "I" — Input (digital)

*   Command: `I<CR>`
*   Response: `I,_PortA_,_PortB_,_PortC_,_PortD_,_PortE_<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Description:
    
    This query reads every PORTx register (where x is A through E) and prints out each byte-wide register value as a three digit decimal number. This effectively reads the digital values on each and every pin of the processor and prints them out. If you need the value of a particular pin, you can extract it from the value printed for that port by looking at the binary bit within the byte for that pin. For example, if you wanted to read the value of RB4, you would look at the 5th bit (0x10) of the PortB byte in the return packet.
    
    For pins that are set as outputs, or are set as analog inputs, or are set as something other than digital inputs, this query will still convert the voltage on the pin to a digital value of 1 or 0 (using the standard voltage thresholds specified in the processor datasheet) and return all of their values.
    
*   Example:`I\r`
*   Example Return Packet: `I,128,255,130,000,007<CR><NL>`

* * *

#### "LM" — Low-level Move, Step-limited

*   Command: `LM,_Rate1_,_Steps1_,_Accel1_,_Rate2_,_Steps2_,_Accel2_[,_Clear_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.7.0 and above
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _Rate1_ and _Rate2_ are unsigned 31 bit integers in the range from 0 to 2147483647. They represent step rates for axis 1 and 2, and are added to each axis step Accumulator every 40 μs to determine when steps are taken.
    *   _Steps1_ and _Steps2_ are signed 32 bit integers in the range from -2147483648 to 2147483647. Each number gives the movement distance — the total number of steps — for the given axis, axis 1 or axis 2. The sign of each _Steps_ parameter controls the direction that the axis should turn.
    *   _Accel1_ and _Accel2_ are signed 32 bit integers in the range from -2147483648 to 2147483647. These values are added to their respective _Rate_ values every 40 μs and control acceleration or deceleration during a move.
    *   _Clear_ is an integer in the range 0 - 3. If it is 1 then the step Accumulator for motor1 is zeroed at the start of the command. If it is 2, then the step Accumulator for motor2 is zeroed at the start of the command. If _Clear_ is 3, then both are cleared.
*   Description:
    
    **Overview:** This low-level command causes one or both motors to move for a given number of steps, and allows the option of applying a constant acceleration to one or both motors during their movement. The motion terminates for each axis when the required number of steps have been made, and the command is complete when the both motors have reached their targets.
    
    This command, as compared to the similar `[LT](#LT)` command, allows you to specify an exact step position, but is more difficult to use since the moves for the two axes may complete at different times.
    
    This is a low-latency command where the input values are parsed and passed directly into motion control FIFO of the EBB. No time is lost doing any math operations or limit checking, so maximum command throughput can be achieved. (See [GitHub issue #73](https://github.com/evil-mad/EggBot/issues/73) for more information about the motivation for this command.) While individual movement commands may be as short as a single step, there are practical limits to the rate at which commands can be issued, as discussed under [Performance](#performance).
    
    **Methods and consequences:** Each axis has a separate 32 bit Accumulator to control its timing. When the `LM` command is called, the Accumulator may be initialized to zero or left alone, depending on the value of the _Clear_ argument. The initial value of _Rate_ for each axis is adjusted by subtracting _Accel_/2 from it. Then, every 40 μs (at a rate of 25 kHz) the following operations are executed for each axis, if the total number of steps to be taken is nonzero:
    
    1.  Update the value _Rate_ = _Rate_ + _Accel_.
    2.  If the new (_Rate_ < 0), then "roll it over" with _Rate_ = _Rate_ + 231.
    3.  The value of _Rate_ is added to the Accumulator.
    4.  If the new Accumulator value is greater than or equal to 231 (2147483648 decimal; 0x80000000 hex), then:
        *   The motor on that axis moves one step.
        *   231 is subtracted from the Accumulator for that axis.
    5.  Check to see if the total number of steps moved equals _Steps_. If true, the move is complete for this axis; no further steps will be taken.
    6.  Check if the move is complete for both axes. If so, exit the LM command.
    
    A restriction on the parameters is that motion must be possible on at least one axis. That is to say, you must ensure that both _Steps_ is nonzero _and_ that either _Rate_ or _Accel_ are nonzero for at least one axis of motion, or no motion will occur.
    
    Because the parameters for each axis determine how long the move will take _for that axis_, one axis may finish stepping before the other. In extreme cases, one axis will finish moving long before the other, which can lead to (correct but) unintuitive behavior. For example, in an XY movement command both axes could travel same distance yet have axis 1 finish well before axis 2. The apparent motion would be a diagonal XY movement for the first part of the transit time, followed by a straight movement along axis 2. To the eye, that transit appears as a "bent" line, or perhaps as two distinct movement events.
    
    **Computing values:** The value of _Rate_ can be computed from a motor step frequency _F_, in Hz, as:
    
    *   _Rate_ = 231 × 40 μs × _F_ , or
    *   _Rate_ ≈ 85,899.35 s × _F_.
    
    In the case of constant velocity, where _Accel_ is zero, the value of _Rate_ can thus be computed from the number of steps and desired total travel time _t_, in seconds, as _Rate_ = 231 × 40 μs × ( _Steps_ / _t_ ), or _Rate_ ≈ 85,899.35 s × ( _Steps_ / _t_ ). This computation (along with most of the others in the section) should be performed as a floating point operation, or at least with 64 bit precision, since _Steps_ × 231 may take up to 63 bits.
    
    The _Accel_ value is added to _Rate_ every 40 μs. It can be positive or negative. This is used to cause an axis to accelerate or decelerate during a move. The theoretical final "velocity rate" after _T_ intervals of 40 μs each, starting with initial rate _Rate_ is:
    
    *   _Rate_Final = _Rate_ + _Accel_ × _T_
    
    The value of _Accel_ can be calculated from the initial value _Rate_, its desired final value _Rate_Final, and the number _T_ of 40 μs intervals that the movement will take:
    
    *   _Accel_ = ( _Rate_Final - _Rate_ ) / _T_
    
    If an LM command begins with a specified _Rate_ and _Accel_, as well as a (possibly unknown) initial Accumulator value _C_0, then the Accumulator value _C_T after _T_ intervals of 40 μs each is given by:
    
    *   _C_T = _C_0 + _Rate_ × _T_ + (1/2) _Accel_ × _T_2
    
    (This formula may look familiar from elementary physics, as it has the form: _x_(t) = _x_0 + _v_0_T_ + (1/2) _a__T_2.) The number of motor steps traveled along the axis during the command can be found from this by dividing the Accumulator value _C_T by 231 and rounding down the result. Thus the step count after _T_ intervals is given by:
    
    *   _Steps_ = Floor( ( _C_0 + _Rate_ × _T_ + (1/2) _Accel_ × _T_2 ) / 231 )
    
    This is a quadratic equation, and the exact movement time for a given number of steps can be computed by solving for _T_ using the quadratic formula. If you already know the final speed, then the approximate movement time _t_ in seconds can be found by dividing the number of steps by the average step frequency over the move:
    
    *   _t_ ≈ _Steps_ / _F_AVE = 2 × _Steps_ / ( _F_0 + _F__t_ )
    
    Here, _F_0 and _F__t_ are the initial and final step frequencies _F_ for the move. From this, we can also calculate the approximate move duration _T_ in terms of 40 μs intervals, using _Rate_ = 231 × 40 μs × _F_ and _t_ = 40 μs × _T_:
    
    *   _T_ ≈ 231 × _Steps_ / _R_AVE = 232 × _Steps_ / ( _Rate_ + _Rate_Final )
*   Example 1: Suppose that we wanted to start moving an axis at 45 steps/s, and end at 250 steps/s, over a total of 60 steps. By the above formulas, we know that our starting _Rate_ is 3865471, our ending _Rate_Final is 21474836, and our move time is 60/((45 + 250)/2) = 0.4068 seconds (or _T_ = 10169 intervals). We find _Accel_ from the change in _Rate_ over the number of intervals: (21474836 - 3865470)/10169 = 1732. We then have the following LM command:
    *   `LM,3865471,60,1732,0,0,0`Notice that we are only using axis 1 in this example. You can of course use both axes at the same time, but you usually need to be careful that the times for each axis match up.
*   Example 2: `LM,33865471,1000,0,0,0,0\r` This example will move axis 1 at a constant speed of 45 steps/s for 1000 steps. Axis 2 does not move.
*   Example 3: `LM,85899346,10,0,17180814,2,0\r` This example will cause a 10 ms long move, where axis 1 takes a step every 1 ms, and axis 2 takes a step every 5 ms. Axis 1 will step for 10 steps, and axis 2 will step for 2 steps, and they will both finish together at the end of the 10 ms. This is a constant-rate move without acceleration or deceleration on either axis.
*   Example 4: `LM,85899346,500,0,85899346,250,0\r` This example will step both axis at a 1 ms/step rate, and axis 1 will step for 500 steps and axis 2 will step for 250 steps. This is _usually_ not what you want in practice; it's usually better if the moves for each axis terminate at the same time. This is a "constant-rate" move without acceleration or deceleration on either axis.
*   Example 5: `LM,17180814,6,0,57266231,20,0\r` This example will create a 30 ms long move, with axis 1 stepping 6 times and axis 2 stepping 20 times. There is no acceleration or deceleration on either axis.
*   Example 6: `LM,42950000,50,13400,0,0,0\r` This example will start with axis 1 stepping at 500 steps/second and end with axis 1 stepping at 800 steps/second. It lasts for a duration of 50 steps. Axis 2 does not move. The move will take 77 ms to complete.
*   Example 7: `LM,17179000,75,-687,8592000,75,687\r` This example will start with axis 1 at 200 steps/second, and axis 2 at 100 steps/second. Over the course of 75 steps each, they will end at a speed of 100 steps/second for axis 1 (that is, decelerating) and 200 steps/second for axis 2. The move will take 500 ms.
*   Version History: First added in v2.5.0 with partial functionality, and revised in v2.5.3. Clear parameter added in v2.7.0, along with updates to parameter definitions and detailed behavior. This command should only be considered to be fully functional as of v2.7.0.

* * *

#### "LT" — Low-level Move, Time-limited

*   Command: `LT,_Intervals_,_Rate1_,_Accel1_,_Rate2_,_Accel2_[,_Clear_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.7.0 and above
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _Intervals_ is an unsigned 32 bit integer in the range from 0 to 4294967295, which specifies the duration of time, in units of 40 μs intervals, that the command executes for.
    *   _Rate1_ and _Rate2_ are signed 32 bit integers in the range from -2147483647 to 2147483647. The sign of each _Rate_ parameter controls _the direction_ that the axis should turn. The absolute value abs(_Rate_) of each is added to its axis step Accumulator every 40 μs to determine when steps are taken.
    *   _Accel1_ and _Accel2_ are signed 32 bit integers in the range from -2147483648 to 2147483647. These values are added to their respective _Rate_ values every 40 μs and control acceleration or deceleration during a move.
    *   _Clear_ is an integer in the range 0 - 3. If it is 1 then the step Accumulator for motor1 is zeroed at the start of the command. If it is 2, then the step Accumulator for motor2 is zeroed at the start of the command. If _Clear_ is 3, then both are cleared.
*   Description:
    
    **Overview:** This low-level command causes one or both motors to move for a given duration of time, and allows the option of applying a constant acceleration to one or both motors during their movement. The motion terminates for each axis when the required number of time intervals has elapsed.
    
    This command, as compared to the similar `[LM](#LM)` command, makes it much easier to construct motion sequences that smoothly follow one another, but trades off the ability to exactly specify a destination in the process. You may wish to use sequences of LT commands, followed by a `[HM](#HM)` command, in order to both move quickly and end up at a specific location.
    
    This is a low-latency command where the input values are parsed and passed directly into motion control FIFO of the EBB. No time is lost doing any math operations or limit checking, so maximum command throughput can be achieved. While individual movement commands may be as short as a single 40 μs time interval, there are practical limits to the rate at which commands can be issued, as discussed under [Performance](#performance).
    
    **Methods and consequences:** The `LT` function is essentially identical to the `[LM](#LM)` in every aspect of its operation _except_ that it terminates after a set number of intervals rather than after a set number of steps. That is to say, in the sequence of operations executed every 40 μs, when the check is made to see if the move is complete, the time elapsed — not the step count — is checked.
    
    With that in mind, all of the formulas from the description of the `[LM](#LM)` command, for computing step rates, acceleration, distance, and time are all still applicable when working with `LT`.
    
    Once exception should be noted: Since there is no _Step_ argument in this command to indicate the direction that each motor should turn, the input _Rate_ arguments are given a sign. The sign of _Rate_ indicates _only_ which direction the motor should turn. Only its absolute value |_Rate_| is input to the routines that calculate and manage the motor step frequency. When using the formulas from the `[LM](#LM)` command description, use the unsigned value |_Rate_|.
    
*   Example 1: Suppose that we wanted to start moving an axis at 45 steps/s, and end at 250 steps/s, over a total of 60 steps. Following Example 1 from `[LM](#LM)`, we know that our starting _Rate_ is 3865471, our ending _Rate_Final is 21474836, and our move time is 60/((45 + 250)/2) = 0.4068 seconds (or _T_ = 10169 intervals). We find _Accel_ from the change in _Rate_ over the number of intervals: (21474836 - 3865470)/10169 = 1732. We then have the following LT command, adding the `,3` value on the end to clear the Accumulator:
    
    *   `LT,10169,3865471,1732,0,0,3`
    
    Since this command does not explicitly specify the number of steps to be traveled, you may want to carefully check your math, or use tools like `[QS](#QS)` or `[HM](#HM)` command following a move like this.
*   Example 2: `LT,25000,33865471,0,0,0\r` This example will move axis 1 at a constant speed of 45 steps/s for one second (25000 intervals). Axis 2 does not move.
*   Example 3: `LT,12500,17179000,-687,8592000,687\r` This example will start with axis 1 at 200 steps/second, and axis 2 at 100 steps/second. Over the course of 500 ms, they will end at a speed of 100 steps/second for axis 1 (that is, decelerating) and 200 steps/second for axis 2. The move will cover 75 steps on each axis.
*   Version History: Added in v2.7.0.

* * *

#### "MR" — Memory Read

*   Command: `MR,_Address_<CR>`
*   Response: `MR,_Data_<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Address_: An integer from 0 to 4095. Represents the address in RAM to read.
*   Description:
    
    This query reads one byte from RAM and prints it out. The _Data_ is always printed as a three digit decimal number.
    
*   Example:`MR,422\r`
    
    This query would read from memory address 422 and print out its current value.
    
*   Example Return Packet: `MR,071<CR><NL>`

* * *

#### "MW" — Memory Write

*   Command: `MW,_Address_,_Data_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Address_: An integer from 0 to 4095. Represents the address in RAM that _Data_ will be written to.
    *   _Length_: An integer from 0 to 255. Represents the byte of data to write to _Address_.
*   Description:
    
    This command writes one byte to RAM. In order for this command to be useful, you will need to know what addresses in RAM are useful to you. This would normally be available by reading the source code for the EBB firmware and looking at the .map file for a particular version build to see where certain variables are located in RAM
    
    Writing to areas in RAM that are currently in use by the firmware may result in unplanned crashes.
    

* * *

#### "ND" — Node Count Decrement

*   Command: `ND<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9.5 and newer
*   Execution: Immediate
*   Description:
    
    This command decrements the 32 bit Node Counter by 1.
    
    See the `[QN](#QN)` command for a description of the node counter and its operations.
    
*   Version History: Added in v1.9.5

* * *

#### "NI" — Node Count Increment

*   Command: `NI<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9.5 and newer
*   Execution: Immediate
*   Description:
    
    This command increments the 32 bit Node Counter by 1.
    
    See the `[QN](#QN)` command for a description of the node counter and its operations.
    
*   Version History: Added in v1.9.5

* * *

#### "O" — Output (digital)

*   Command: `O,_PortA_,[_PortB_,_PortC_,_PortD_,_PortE_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _PortA_: An integer from 0 to 255. Represents the new value to write to the LATA register.
    *   _PortB_: (optional) An integer from 0 to 255. Represents the new value to write to the LATB register.
    *   _PortC_: (optional) An integer from 0 to 255. Represents the new value to write to the LATC register.
    *   _PortD_: (optional) An integer from 0 to 255. Represents the new value to write to the LATD register.
    *   _PortE_: (optional) An integer from 0 to 255. Represents the new value to write to the LATE register.
*   Description:
    
    This command simply takes its arguments and write them to the LATx registers. This allows you to output digital values to any or all of the pins of the microcontroller. The pins must be configured as digital outputs before this command can have an effect on the actual voltage level on a pin.
    

* * *

#### "PC" — Pulse Configure

*   Command: `PC,_Length0_,_Period0_[,_Length1_,_Period1[,_Length2_,_Period2_[,_Length3_,_Period3_]]]_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Length0_: An integer from 0 to 65535. This length represents the number of milliseconds RB0 will go high for.
    *   _Period0_: An integer from _Length0_ to 65535. Represents the number of milliseconds between rising edges on RB0.
    *   _Length1_, _Length2_, _Length3_: (optional) Each is an integer from 0 to 65535, that represents the number of milliseconds RB_x_ will go high for, where the value of _x_ is 1, 2, or 3
    *   _Period1_, _Period2_, _Period3_: (optional) Each is integer from _RBx\_Len_ to 65535, that represents the number of milliseconds between rising edges on RB_x_, where the value of _x_ is 1, 2, or 3
*   Description:
    
    This command sets up the internal parameters for the `PG` command. The parameters come in pairs, and the first number in the pair represents the number of milliseconds that a pin (one of RB0, RB1, RB2 and RB3) goes high for, and the second number represents the number of milliseconds between rising edges for that pin. The first pair, for pin RB0, is required. The other three pairs (for RB1, RB2 and RB3) are optional and any number of them (from zero to three) can be included. Pairs which are not included are simply treated as zeros and that pin is not used for output of pulses.
    
    When the `PG,1` command is sent, any pairs from the `PC` command where both values are non-zero and the Rate is greater than the Length will create pulses on that pin.
    
    While the pulses are going, new `PC` commands can be sent, updating the pulse durations and repetition rates.
    
    This command is only available for pins RB0, RB1, RB2 and RB3. If you wish to leave a pin alone (i.e. not create pulses on it) just set its Length and Period values to zero.
    
*   Example: `PC,100,150\r` After sending a `PG,1` command, this Length and Period would causes RB0 to go high for 100 milliseconds, then low for 50 milliseconds, then high for 100 milliseconds, etc.
*   Example: `PC,12,123,0,0,2000,10000\r` After sending a `PG,1` command, these parameters would cause pin RB0 to go high for a duration of 12 milliseconds, repeating every 123 milliseconds. Pin RB1 would be untouched. Pin RB2 would go high for 2 seconds every 10 seconds. And pin RB3 would be untouched (because the last pair of Length and Period are omitted and thus treated as 0,0).
*   Example: `PC,1,2,1,2,1,2,1,2\r` After sending a `S2,0,4` (to turn off RC servo output on pin RB1) and `PG,1` (to turn on pulse generation), these parameters would cause all four pins (RB0, RB1, RB2, and RB3) to output square waves with a 50% duty cycle and 500 Hz frequency.
*   Version History:
    
    Version 2.6.6 and later : The bug preventing some `PC` changes from acting immediately is fixed and `PC/PG` work properly.
    
    Version 2.6.5 and before : There is a bug in the `PC/PG` functions which prevent some `PC` updates from taking effect immediately but instead requiring 64K milliseconds to elapse before the change takes effect.
    

* * *

#### "PD" — Pin Direction

*   Command: `PD,_Port_,_Pin_,_Direction_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Port_: is one of the following letters: A,B,C,D,E. It specifies which port on the processor is to be used.
    *   _Pin_: is an integer in the range from 0 through 7. It specifies the pin to be used.
    *   _Direction_: is either 0 (output) or 1 (input)
*   Description:
    
    This command sets one of the processor pins to be an input or an output, depending on the _Direction_ parameter.
    
    This command is a very low-level I/O command. Higher level commands (like `[SM](#SM)`, `[S2](#S2)`, etc.) will not change the direction of pins that they need after boot, so if this command gets used to change the pin direction, be sure to change it back before expecting the higher level commands that need the pin to work properly.
    
*   Example: `PD,C,3,0\r` This command would set pin PC3 (or Port C, pin 3) as a digital output.

* * *

#### "PG" — Pulse Go

*   Command: `PG,_Value_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Value_: is either 0 or 1. A value of 0 will stop the pulses, a value of 1 will start the pulses.
*   Description:
    
    This command turns on (`PG,1`) or turns off (`PG,0`) the Pulse Generation on pin RB0 (and optionally on RB1, and/or RB2 and/or RB3). It uses the parameters from the `PC` command to control the pulse width and repetition rate on each pin. See the `[PC](#PC)` — Pulse Configure command for complete details.
    
    This command does not turn off any other commands. So if you want to use the Pulse Generation on pins that already have `[S2](#S2)` RC Servo outputs or other outputs on them, be sure to turn those other outputs off yourself before starting the Pulse Generation, or the two signals will get mixed together and create outputs you do not desire.
    
*   Example: `PG,1\r` This command would turn on pulse generation as per the parameters specified in the latest `PC` command.
*   Example: `PG,0\r` This command would turn off pulse generation on any pins (RB0, RB1, RB2 or RB3) which have non-zero Length and Period values from the latest `PC` command.

* * *

#### "PI" — Pin Input

*   Command: `PI,_Port_,_Pin_<CR>`
*   Response: `PI,_Value_<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Port_: is one of the following letters: A,B,C,D,E. It specifies which port on the processor is to be used.
    *   _Pin_: is an integer in the range from 0 through 7. It specifies the pin to be used.
    *   _Value_: is a 0 or 1. It reflects the state of the pin when read as a digital input.
*   Description:
    
    This query reads the given port and pin as a digital input. No matter what direction the pin is set to, or even if the pin is being used as an analog input, the pin can still be read as a digital input.
    
*   Example: `PI,D,2\r` This query would read pin RD2 (or Port D, pin 2) as a digital input and return the pin's value.
*   Example Return Packet: `PI,1<CR><NL>`

* * *

#### "PO" — Pin Output

*   Command: `PO,_Port_,_Pin_,_Value_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _Port_: is one of the following letters: A,B,C,D,E. It specifies which port on the processor is to be used for the output.
    *   _Pin_: is an integer in the range from 0 through 7. It specifies the pin to be used for the output.
    *   _Value_: is either 0 or 1. It specifies the logical value to be output on the pin.
*   Description:
    
    This command outputs a digital value of a 0 (0V) or 1 (3.3V) on one of the pins on the processor, as specified by _Port_ and _Pin_.
    
    This command will not change a pin's direction to output first, so you must set the pin's direction to be an output using the `PD` command first if you want anything to come out of the pin.
    
    This command is a very low-level I/O command. Many other higher level commands (like `[SM](#SM)`, `[S2](#S2)`, etc.) will over-write the output state of pins that they need. This commands allows you low-level access to every pin on the processor.
    
*   Example: `PO,C,7,1\r` This command would set the pin RC7 (or Port C, pin 7) to a high value.

* * *

#### "QB" — Query Button

*   Command: `QB<CR>`
*   Response: `_state_<CR><NL>OK<CR><NL>`
*   Firmware versions: v1.9.2 and newer
*   Execution: Immediate
*   Description:
    
    This query checks whether the PRG button on the EBB has been pressed since the last QB query or not.
    
    The returned value _state_ is 1 if the PRG button has been pressed since the last QB query, and 0 otherwise.
    
    One of the GPIO input pins, B0, can also be used to initiate a "button press" event. B0 is normally pulled high, but if it is taken low, then that registers as though the PRG button itself was pressed. To ensure that a "button press" is registered, ensure that B0 is pulled low for at least 40 microseconds. This "alt\_prg" feature is enabled by default but can be disabled with the `[SC](#SC)` command.
    
*   Version History: Added in v1.9.2

* * *

#### "QC" — Query Current

*   Command: `QC<CR>`
*   Response: `_RA0_VOLTAGE_,_V+_VOLTAGE_<CR><NL>OK<CR><NL>`
*   Firmware versions: v2.2.3 and newer
*   Execution: Immediate
*   Description:
    
    This query reads two analog voltages and returns their raw 10 bit values. You can use this to read the current setpoint for the stepper motor, and to read the input power that the board is receiving.
    
    The two returned values are:
    
    *   _RA0\_VOLTAGE_, the voltage on the REF\_RA0 net. It is expressed as a zero-padded 4-digit 10 bit number where 0 = 0.0V and 1023 = 3.3V
        
        This value yields the voltage level at the REF\_RA0 input to the stepper driver chip. This is the control voltage that sets the maximum instantaneous (not average) current that the driver chips allow into the motor coils.
        
        The maximum current is given approximately by _I\_max_ = _RA0\_VOLTAGE_/1.76. Thus, a voltage of 3 V at REF\_RA0 would correspond to a maximum motor current of about 1.7 A.
        
    *   _V+\_VOLTAGE_ is the voltage on the V+ net, scaled by a voltage divider. It is expressed as a zero-padded 4-digit 10 bit number where 0 = 0.0V and 1023 = 3.3V
        
        This value yields the voltage level at on the EBB's V+ power net, which is the "motor" power coming into the board, as measured after the first input protection diode.
        
        The value of _V+\_VOLTAGE_ as read on the ADC pin is scaled so that it does not exceed the 3.3 V maximum analog input level for the MCU. The scaling is performed by a voltage divider (comprised of R13 and R18 on the EBB), which gives a scaling factor of (1/11) on EBB boards v2.2 and earlier, and a scaling factor of (1/9.2) on EBB boards v2.3 and newer. As there is tolerance on the resistors, these scaling factors should be considered to be only approximate.
        
        If one also wishes to compare the to the voltage read to that at the power input, it is necessary to also account for both the forward voltage across the input diode: the "diode drop" across the input diode is about 0.3 V at the current levels typically encountered.
        
        The value of _V+\_VOLTAGE_ may be very useful in determining whether or not the EBB is plugged into power. One might also compare the value of this voltage with and without the motors enabled, in order to monitor and detect if the power supply voltage should droop due to load on the motors.
        
*   Example Return Packet: `0394,0300<CR><NL>OK<CR><NL>`
    
    This query has returned values of 394 for RA0\_VOLTAGE and 300 for V+\_VOLTAGE.
    
    The first returned value, 0394, indicates a voltage of 1.27 V at REF\_RA0. This indicates that the maximum motor current is currently set to 0.72 A.
    
    The second returned value, 0300, indicates a voltage of 0.96 V at the V+ ADC input. Scaling by 9.2 (for the voltage divider on an EBB v2.3) and adding 0.3 V (for the diode drop), this indicates that the "actual" input voltage is about 9.1 V.
    
*   Version History:
    
    This query was originally introduced in v2.0.0, but should not be considered functional until version 2.2.3.
    
    Note also that this query only works properly on EBB hardware v1.3 and above. (White EBBs from Evil Mad Scientist are v2.0 or newer, and EBBs from SparkFun are v2.0 and above.)
    

* * *

#### "QE" — Query motor Enables and microstep resolutions

*   Command: `QE<CR>`
*   Response: `_MOTOR1_STATE_,_MOTOR2_STATE_<CR><NL>OK<CR><NL>`
*   Firmware versions: v2.8.0 and newer
*   Execution: Immediate
*   Description:
    
    This query reads the current state of the motor enable pins and the microstep resolution pins. It then returns two values which encode the motor enable/disable state and (if enabled) microstep resolution.
    
    There is only one value for the microstepping resolution since both motor drivers share the same MS1, MS2 and MS3 lines on the EBB. So the two values returned by this query will either be the same (if both motors are enabled) or one or both of them will be zero. But they will never show that the two motors are both enabled and have different microstep resolutions.
    
    The two returned values are:
    
    *   _MOTOR1\_STATE_
        *   0: Motor 1 is disabled
        *   1: Motor 1 is enabled and is set to full step
        *   2: Motor 1 is enabled and is set to 1/2 steps
        *   4: Motor 1 is enabled and is set to 1/4 steps
        *   8: Motor 1 is enabled and is set to 1/8 steps
        *   16: Motor 1 is enabled and is set to 1/16 steps
    *   _MOTOR2\_STATE_
        
        Same as for MOTOR1\_STATE but for Motor 2.
        
*   Example Return Packet: `16,16<CR><NL>OK<CR><NL>`
    
    Both motors are enabled and set to 1/16th microsteps.
    
*   Example Return Packet: `0,4<CR><NL>OK<CR><NL>`
    
    Motor 1 is disabled and motor 2 is enabled an set to 1/4 steps.
    

* * *

#### "QG" — Query General

*   Command: `QG<CR>`
*   Response: `_Status Byte_<CR><NL>`
*   Firmware versions: v2.6.2 and newer
*   Execution: Immediate
*   Description:
    
    This query reads the status of eight bits of information, and returns them as a bit field expressed as a single hexadecimal byte.
    
    The returned status byte consists of the following status bits:
    
    
|Bit          |7  |6  |5  |4  |3  |2   |1   |0   |
|-------------|---|---|---|---|---|----|----|----|
|Decimal Value|128|64 |32 |16 |8  |4   |2   |1   |
|Name         |RB5|RB2|PRG|PEN|CMD|MTR1|MTR2|FIFO|

    
    Bit 7: RB5 — Status of GPIO pin RB5
    
    This bit is 1 when GPIO pin RB5 is high, and 0 when it is low. RB5 does not have to be set to an input to be read. The `QG` query will read the state even if it is an output. If the pin is in use as an RC servo output, the bit will be toggling and that will be reflected in the response byte. Pin RB5 can be used for various useful purposes as desired, for example as a home switch input or output to control a tool head.
    
    Bit 6: RB2 — Status of GPIO pin RB2
    
    This bit is 1 when GPIO pin RB2 is high, and 0 when it is low. Its properties are otherwise the same as the RB5 bit.
    
    Bit 5: PRG — PRG Button Pressed
    
    This bit will be 1 if the PRG button has been pushed since the last `QG` or `[QB](#QB)` query. Otherwise it will be 0. Note that input B0 can be used to trigger a "button push" event; see the description of `[QB](#QB)` for more information.
    
    Bit 4: PEN — Pen is up
    
    This bit is 1 when the pen is up, and 0 when the pen is down. The pen status is given by the position of the pen-lift servo output, which can be controlled with the `[SP](#SP)` command and can be read with the `[QP](#QP)` query. Note that this is the _commanded state_ of the pen, and that it does physically take time to lift from or lower to the page.
    
    Bit 3: CMD — Command Executing
    
    This bit will be 1 when a command is being executed, and 0 otherwise. The command may be a command that causes motion (like a motor move command) or any other command listed in this document as 'Execution: Added to FIFO motion queue'.
    
    Bit 2: MTR1 — Motor 1 moving
    
    This bit is 1 when Motor 1 is in motion and 0 when it is idle.
    
    Bit 1: MTR2 — Motor 2 moving
    
    This bit is 1 when Motor 2 is in motion and 0 when it is idle.
    
    Bit 0: FIFO — FIFO motion queue not empty
    
    This bit will be 1 when a command is executing _and_ a second command is awaiting execution in the 1-deep "FIFO" motion queue. It is 0 otherwise. The **CMD** bit will always be 1 when the **FIFO** bit is 1; if the FIFO is full, then a command is currently executing. Additional information about the motion queue can be found in the description of the `[QM](#QM)` query.
*   Equivalence to `[QM](#QM)` query:
    
    Bits 0, 1 2, and 3 are exactly identical to the _FIFOStatus_,_Motor2Status_,_Motor1Status_ and _CommandStatus_ result fields (respectively) of the `QM` query.
    
*   Example Return Packet: `3E<CR><NL>`
    
    This query returns value of `3E`, which corresponds to `0011 1110` in binary, indicates that RB5 and RB2 are low, the PRG button has been pressed, the pen is down, a command is being executed, Motor 1 and Motor 2 are moving, and the FIFO motion queue is empty.
    
*   Version History: The meaning of bit 4 has been corrected. Previous documentation versions had the state inverted.

* * *

#### "QL" — Query Layer

*   Command: `QL<CR>`
*   Response: `_CurrentLayerValue_<CR><NL>OK<CR><NL>`
*   Firmware versions: v1.9.2 and newer
*   Execution: Immediate
*   Description:
    
    This query asks the EBB to report back the current value of the Layer variable. This variable is set with the `[SL](#SL)` command, as a single unsigned byte.
    
*   Example: `QL<CR>`
*   Example Return Packet: `4<CR><NL>OK<CR><NL>`
*   Version History: Added in v1.9.2

* * *

#### "QM" — Query Motors

*   Command: `QM<CR>`
*   Response: `QM,_CommandStatus_,_Motor1Status_,_Motor2Status_,_FIFOStatus_<NL><CR>`
*   Firmware versions: v2.4.4 and above
*   Execution: Immediate
*   Description:
    
    Use this query to see what the EBB is currently doing. It will return the current state of the 'motion system', each motor's current state, and the state of the FIFO.
    
    *   _CommandStatus_ is nonzero if any "motion commands" are presently executing, and zero otherwise.
    *   _Motor1Status_ is 1 if motor 1 is currently moving, and 0 if it is idle.
    *   _Motor2Status_ is 1 if motor 2 is currently moving, and 0 if it is idle.
    *   _FIFOStatus_ is non zero if the FIFO is not empty, and 0 if the FIFO is empty.
    
    The definition of a "motion command" is any command that has a time associated with it. For example, all `[SM](#SM)` commands. Also, any Command (like `[S2](#S2)`, `[SP](#SP)`, or `[TP](#TP)`) that uses a _delay_ or _duration_ parameter. All of these commands cause the motion processor to perform an action that takes some length of time, which then prevents later motion commands from running until they have finished.
    
    It is important to note that with all existing EBB firmware versions, only a very limited number of "motion commands" can be executing or queued simultaneously. In fact, there can only be three. One (the first one) will be actually executing. Another one (the second) will be stored in the 1-deep FIFO buffer that sits between the USB command processor and the motion engine that executes motion commands. Then the last one (the third) will be stuck in the USB command buffer, waiting for the 1-deep FIFO to be emptied before it can be processed. Once these three motion commands are "filled," the whole USB Command processor will block (i.e. lock up) until the FIFO is cleared, and the third motion command can be processed and put into the FIFO. This means that no USB commands can be processed by the EBB once the third motion command gets "stuck" in the USB Command processor. Using the QM query can help prevent this situation by allowing the PC to know when there are no more motion commands to be executed, and so can send the next one on.
    
*   Version History: Added in v2.4.4

#### "QM" — Query Motors (Legacy version)

*   Command: `QM<CR>`
*   Response: `QM,_CommandStatus_,_Motor1Status_,_Motor2Status_<NL><CR>`
*   Firmware versions: v2.2.6 to v2.4.3
*   Execution: Immediate
*   Description:
    
    Use this query to see what the EBB is currently doing. It will return the current state of the 'motion system' and each motor's current state.
    
    *   _CommandStatus_ is nonzero if any "motion commands" are presently executing, and zero otherwise.
    *   _Motor1Status_ is 1 if motor 1 is currently moving, and 0 if it is idle.
    *   _Motor2Status_ is 1 if motor 2 is currently moving, and 0 if it is idle.
    
    The definition of a "motion command" is any command that has a time associated with it. For example, all `[SM](#SM)` commands. Also, any Command (like `[S2](#S2)`, `[SP](#SP)`, or `[TP](#TP)`) that uses a _delay_ or _duration_ parameter. All of these commands cause the motion processor to perform an action that takes some length of time, which then prevents later motion commands from running until they have finished.
    
    It is important to note that with all existing EBB firmware versions, only a very limited number of "motion commands" can be executing or queued simultaneously. In fact, there can only be three. One (the first one) will be actually executing. Another one (the second) will be stored in the 1-deep FIFO buffer that sits between the USB command processor and the motion engine that executes motion commands. Then the last one (the third) will be stuck in the USB command buffer, waiting for the 1-deep FIFO to be emptied before it can be processed. Once these three motion commands are "filled," the whole USB Command processor will block (i.e. lock up) until the FIFO is cleared, and the third motion command can be processed and put into the FIFO. This means that no USB commands can be processed by the EBB once the third motion command gets "stuck" in the USB Command processor. Using the QM query can help prevent this situation by allowing the PC to know when there are no more motion commands to be executed, and so can send the next one on.
    
*   Version History: Added in v2.2.6

* * *

#### "QP" — Query Pen

*   Command: `QP<CR>`
*   Response: `_PenStatus_<NL><CR>OK<CR><NL>`
*   Firmware versions: v1.9 and newer
*   Execution: Immediate
*   Description:
    
    This query reads the current pen state from the EBB. It will return _PenStatus_ of 1 if the pen is up and 0 if the pen is down. If a pen up/down command is pending in the FIFO, it will only report the new state of the pen after the pen move has been started.
    
*   Example: `QP\r`
*   Example Return Packet: `1<NL><CR>OK<CR><NL>`
*   Version History: Added in v1.9

* * *

#### "QR" — Query RC Servo power state

*   Command: `QR<CR>`
*   Response: `_RCServoPowerState_<NL><CR>OK<CR><NL>`
*   Firmware versions: v2.6.0 and newer
*   Execution: Immediate
*   Description:
    
    This query reads the current RC Servo power control state from the EBB. It will return _RCServoPowerState_ of 1 if the RC Servo is receiving power and 0 if it is not.
    
*   Example: `QR\r`
*   Example Return Packet: `1<NL><CR>OK<CR><NL>`
*   Version History: Added in v2.6.0

* * *

#### "QS" — Query Step position

*   Command:`QS<CR>`
*   Response: `_GlobalMotor1StepPosition_,_GlobalMotor2StepPosition_<NL><CR>OK<CR><NL>`
*   Firmware versions:Added in v2.4.3
*   Execution: Immediate
*   Description:
    
    This query prints out the current Motor 1 and Motor 2 global step positions. Each of these positions is a 32 bit signed integer, that keeps track of the positions of each axis. The `CS` command can be used to set these positions to zero.
    
    Every time a step is taken, the appropriate global step position is incremented or decremented depending on the direction of that step.
    
    The global step positions can be be queried even while the motors are stepping, and it will be accurate the instant that the query is executed, but the values will change as soon as the next step is taken. It is normally good practice to wait until stepping motion is complete (you can use the `QM` query to check if the motors have stopped moving) before checking the current positions.
    
*   Example: `QS\r`
*   Example Return Packet: `1421,-429<NL><CR>OK<CR><NL>`
*   Version History:
    
    Added in v2.4.3
    

* * *

#### "QT" — Query EBB nickname Tag

*   Command: `QT<CR>`
*   Response: `_name_<CR><NL>OK<CR><NL>`
*   Firmware versions: v2.5.4 and newer
*   Execution: Immediate
*   Description:
    
    This query gets the EBB's "nickname", which is set with the `[ST](#ST)` command. It simply prints out the current value of the EBB's nickname. If a nickname has not yet been set, then it will print out an empty line before sending the `OK`. The name field can be anywhere from 0 to 16 bytes in length.
    
*   Example: `QT<CR>`
    
    If the EBB's nickname has been set to "East EBB" then the output of this query would be:
    
    `East EBB<CR><NL>OK<CR><NL>`
*   Version History: Added in v2.5.4

* * *

#### "RB" — ReBoot

*   Command: `RB<CR>`
*   Response:
*   Firmware versions: v2.5.4 and newer
*   Execution: Immediate
*   Description:
    
    This command causes the EBB to drop off the USB, then completely reboot as if just plugged in. Useful after a name change with the `ST` command. There is no output after the command executes.
    
*   Version History:
    
    Added in v2.5.4
    

* * *

#### "R" — Reset

*   Command: `R<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Description:
    
    This command reinitializes the the internal state of the EBB to the default power on state. This includes setting all I/O pins in their power on states, stopping any ongoing timers or servo outputs, etc. It does NOT do a complete reset of the EBB - this command does not cause the EBB to drop off the USB and come back, it does not reinitialize the processor's internal register, etc. It is simply a high level EBB-application reset. If you want to completely reset the board, use the `RB` command.
    
*   Example: `R<CR>`
*   Example Return Packet: `OK<CR><NL>`

* * *

#### "S2" — General RC Servo Output

*   Command: `S2,_position_,_output_pin_[,_rate_[,_delay_]]<CR>`
*   Command: `S2,0<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.2.0 and later
*   Execution: Added to FIFO motion queue (with one exception; see below)
*   Arguments:
    *   _position_, a number in the range 0 to 65535.
        
        The "on time" of the signal, in units of 1/12,000,000th of a second (about 83.3 ns).
        
    *   _output\_pin_, a number in the range 0 to 24.
        
        The physical RPx pin number to use for generating the servo pulses.  
        
    *   _rate_ (optional), a number in the range 0 to 65535.
        
        The rate at which to change to the new setting.
        
    *   _delay_ (optional), a number in the range 0 to 65535.
        
        Delay before next command, milliseconds.
        
*   Description:
    
    This command allows you to control the RC servo output system on the EBB, to configure generic RC servo outputs.
    
    **Servo channels and time slices:** Including the pen-lift servo, there are (by default) eight software-defined RC servo 'channels', which have no physical meaning other than we can output up to 8 separate signals at once. These channels are internally assigned as you use the S2 command to create additional servo outputs, up to a maximum of 8 (this maximum can be changed with the SC,8,X command).
    
    Many I/O pins on the MCU have RPx numbers (please refer to the schematic), and you can output RC servo pulses on up to 8 of these RPx pins at once.
    
    The RC servo system will cycle through each of the 8 channels. Each gets a 3 ms "slice" of time, thus giving a 24 ms repeat period for the full RC system.
    
    If a given servo output is enabled, then at the beginning of its 3 ms time slot, its RPx pin is set high. Then, _position_ time later, the RPx pin is set low. This time is controlled by hardware (the ECCP2 in the CPU) so there is very little jitter in the pulse durations. _position_ is in units of 1/12,000 of a second, so 32000 for _position_ would be about 2.666 ms. A value of 0 for the _position_ parameter will disable servo output on the given RPn pin, and that internal servo channel will be deallocated. If the _position_ value is greater than the amount of time allocated for each channel (by default, 3 ms) then the smaller of the two values will be used to generate the pulse.
    
    The number of available channels is normally (by default) 8. This can be changed with the SC,8 command. The S2 RC servo output command cycles from channel 1 through channel _maximum\_S2\_channels_ (normally 8), outputting any enabled channel's pulse from 0 ms to 3 ms. For a given channel, the repetition rate is determined by _maximum\_S2\_channels_ \* _S2\_channel\_duration\_ms_ which is normally 8 \* 3 or 24 ms. Thus, each channel's output pulse will be repeated every 24 ms. However, if you change the _maximum\_S2\_channels_ you will change the repetition rate of the pulses. The _S2\_channel\_duration\_ms_ parameter can also be adjusted with the RC,9 command.
    
    **Delay:** The _delay_ argument gives the number of milliseconds to delay the start of the next command in the motion queue. This is an optional argument that defaults to 0, giving no added delay, thus allowing the next motion command to begin immediately after the S2 command has started.
    
    **Motion Queue:** In all cases but one, S2 commands are added to the motion queue, even if their _delay_ parameters are 0. This means that they will always execute in their correct place in the stream of SM, TP, etc. commands. (The special command `S2,0,_output_pin_<CR>` disables the servo output for _output\_pin_ immediately and is not added to the queue.)
    
    **Slew rate:** The _rate_ argument is used to control how quickly the output changes from the current pulse width (servo position) to the new pulse width. If _rate_ is zero, then the move is made on the next PWM cycle (i.e. the next time the pin is pulsed). If _rate_ is nonzero, then the value of _rate_ is added to (or subtracted from) the current pulse width each time the pulse is generated until the new target is reached. This means that the units of _rate_ are 1/12,000th of a second per _maximum\_S2\_channels_ \* _S2\_channel\_duration\_ms_ or 1/12,000th of a second per 24 ms. The slew rate is completely independent of the _delay_.
    
    **Collisions with SP and TP:** The normal pen up/down servo control (SP and TP) commands internally use the S2 command to manage their actions through one of the software-defined channels. If desired, you can use the S2 command to disable this channel, for example if you need access to all eight channels.
    
    **Turn-on condition:** Note that the S2 command will always make _output\_pin_ an output before it starts outputting pulses to that pin.
    
    **Disabling an S2 servo output:** A special command, `S2,0,_output_pin_<CR>`, will turn off the RC servo output for _output\_pin_. This special command is executed _immediately_; unlike regular S2 commands, it is NOT added to the FIFO motion queue.
    
*   RPx vs pin number (and label on the board) table:  
    
    

* RPx                    : Pin                    
  * RP0                  : REF_RA0                  
  * RP1                  : RA1                  
  * RP2                  : RA5                  
  * RP3                  : RB0                  
  * RP4                  : RB1                  
  * RP5                  : RB2                  
  * RP6                  : RB3                  
  * RP7                  : RB4                  
* RPx                    : Label                    
  * RP0                  :                   
  * RP1                  :                   
  * RP2                  :                   
  * RP3                  : B0                  
  * RP4                  : B1                  
  * RP5                  : B2                  
  * RP6                  : B3                  
  * RP7                  : B4                  

    
    

* RPx                    : Pin                    
  * RP8                  : RB5                  
  * RP9                  : RB6                  
  * RP10                  : RB7                  
  * RP11                  : RC0                  
  * RP13                  : RC2                  
  * RP17                  : RC6                  
  * RP18                  : RC7                  
* RPx                    : Label                    
  * RP8                  : B5                  
  * RP9                  : B6                  
  * RP10                  : B7                  
  * RP11                  :                   
  * RP13                  :                   
  * RP17                  :                   
  * RP18                  :                   

    
*   Example: `S2,24000,6\r` Use RP6 as a RC servo output, and set its on-time to 2 ms.
*   Example: `S2,0,5\r` Turn off the output on RP5 (which is pin RB2) so it stops sending any pulses.
*   Example: `S2,10000,5,100\r` Send a 0.83 ms pulse out pin RB2, and force a pause of 100 ms before the next motion command can start.
*   Example: `S2,27500,5,10,50\r` Start the pulse on RB2 moving from wherever it is at now towards 2.28 ms at a rate of 0.173 ms/S, with a 10 ms delay before the next motion command can begin.
*   Version History: This version of the S2 command is only for firmware versions v2.2.0 and newer

* * *

#### "S2" — General RC Servo Output (Legacy version)

*   Command: `S2,_channel_,_position_,_output_pin_[,_rate_]<CR>`
*   Command: `S2,0<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: Prior to v2.2.0
*   Execution: Immediate
*   Arguments:
    *   _channel_, a number in the range 0 to 8.
        
        A value of 1 through 7 chooses a software-defined channel.
        
        A value of 0 disables all S2 outputs.
        
    *   _position_, a number in the range 0 to 32000.
        
        The "on time" of the signal, in units of 1/12,000,000th of a second (about 83.3 ns).
        
    *   _output\_pin_, a number in the range 0 to 24.
        
        The physical RPx pin number to assign this channel to.
        
    *   _rate_ (optional), a number in the range 0 to 65535.
        
        The rate at which to change to the new setting.
        
*   Description:
    
    This command allows you to control the RC servo output system on the EBB, to configures a generic RC servo output.
    
    **Servo channels and time slices:** There are eight software-defined RC servo 'channels', which have no physical meaning other than we can (by default) output up to 8 separate signals at once. The available channels are numbered 1 though 8. Channel 1 is normally used as the "pen lift" servo motor, controlled through the SP and TP commands.
    
    (A selection of _channel_ equal to 0 is a special command, used to disable all S2 functionality.)
    
    Many I/O pins on the MCU have RPx numbers (please refer to the schematic), and you can output RC servo pulses on up to 8 of these RPx pins. You can assign any channel to any RPx pin.
    
    The RC servo system will cycle through each of the 8 channels. Each gets a 3 ms "slice" of time, thus giving a 24 ms repeat period for the full RC system.
    
    If the current channel is enabled, then at the beginning of its 3 ms time slot, its RPx pin is set high. Then, _position_ time later, the RPx pin is set low. This time is controlled by hardware (the ECCP2 in the CPU) so there is very little jitter in the pulse durations. _position_ is in units of 1/12,000 of a second, so 32000 for _position_ would be 2.666 ms. A value of 0 for _position_ will produce zero length, without any pulse and disable servo output on the pin currently assigned to channel _channel_.
    
    The number of available channels is normally (by default) 8. This can be reduced with the SC,8 command. The S2 RC servo output command cycles from channel 1 through channel _maximum\_S2\_channels_ (normally 8), outputting any enabled channel's pulse from 0 ms to 3 ms. For a given channel, the repetition rate is determined by _maximum\_S2\_channels_ \* _S2\_channel\_duration\_ms_ which is normally 8 \* 3 or 24 ms. Thus, each channel's output pulse will be repeated every 24 ms. However, if you change the _maximum\_S2\_channels_ you will change the repetition rate of the pulses. The _S2\_channel\_duration\_ms_ parameter can also be adjusted with the SC,9 command.
    
    **Slew rate:** The _rate_ argument is used to control how quickly the output changes from the current pulse width (servo position) to the new pulse width. If _rate_ is zero, then the move is made on the next PWM cycle (i.e. the next time the pin is pulsed). If _rate_ is nonzero, then the value of _rate_ is added to (or subtracted from) the current pulse width each time the pulse is generated until the new target is reached. This means that the units of _rate_ are 1/12,000th of a second per _maximum\_S2\_channels_ \* _S2\_channel\_duration\_ms_ or 1/12,000th of a second per 24 ms.
    
    **Collisions with SP and TP:** The normal pen up/down servo control (SP and TP) commands internally use S2 channel 1. If you wish to to use SP and TP, then ignore channel 1 and start with channel 2. If you do not need the SP and TP commands, then you can use channels 1 through _maximum\_S2\_channels_.
    
    **Turn-on condition:** Note that the S2 command will always make _output\_pin_ an output before it starts outputting pulses to that pin.
    
    **Disabling S2 servo outputs:** The special command `S2,0<CR>` will turn off all RC servo support (freeing up CPU time, if that's important to you).
    
*   Example: `S2,3,24000,6\r` Use RP6 as a RC servo output, and set its on-time to 2 ms, and using channel 3 of the RC system, including a _rate_ argument of 6.
*   Example: `S2,0\r` Turn off all RC servo support.
*   Example: `S2,3,0\r` Turn off the output on whatever pin channel 3 was assigned to.
*   Version History: This version of the S2 command is only for firmware versions prior to v2.2.0

* * *

#### "SC" — Stepper and Servo Mode Configure

*   Command: `SC,_value1_,_value2_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _value1_ is an integer in the range from 0 to 255, which specifies the parameter that you are adjusting.
    *   _value2_ is an integer in the range from 0 to 65535. It specifies the value of the parameter given by _value1_.
    *   See the list of these parameters (_value1_) and allowed values (_value2_), below.
*   Description:
    
    This command allows you to configure the motor control modes that the EBB uses, including parameters of the servo or solenoid motor used for raising and lowering the pen, and how the stepper motor driver signals are directed.
    
    The set of parameters and their allowed values is as follows:
    

*   `SC,1,_value2_` Pen lift mechanism. _value2_ may be 0, 1 or 2. Early EggBot models used a small solenoid, driven from an output signal on pin RB4.
    *   `SC,1,0` Enable only the solenoid output (RB4) for pen up/down movement.
    *   `SC,1,1` Enable only the RC servo output (RB1) for pen up/down movement.
    *   `SC,1,2` Enable both the solenoid (RB4) and RC servo (RB1) outputs for pen up/down movement (default)
*   `SC,2,_value2_` Stepper signal control. _value2_ may be 0, 1 or 2.
    *   `SC,2,0` Use microcontroller to control on-board stepper driver chips (default)
    *   `SC,2,1` Disconnect microcontroller from the on-board stepper motor drivers and drive external step/direction motor drivers instead. In this mode, you can use the microcontroller to control external step/direction drivers based on the following pin assignments:
        *   ENABLE1: RD1
        *   ENABLE2: RA1
        *   STEP1: RC6
        *   DIR1: RC2
        *   STEP2: RA5
        *   DIR2: RA2Note also that in this mode, you can externally drive the step/direction/enable lines of the on board stepper motor drivers from the pins of J4 and J5. (Please refer to the schematic for where these pins are broken out.)
    *   `SC,2,2` Disconnect microcontroller from both the built-in motor drivers and external pins. All step/dir/enable pins on the PIC are set to inputs. This allows you to control the on-board stepper motor driver chips externally with your own step/dir/enable signals. Use the pins listed in the schematic from J5 and J4.
*   `SC,4,_servo_min_` Set the minimum value for the RC servo output position. _servo\_min_ may be in the range 1 to 65535, in units of 83.3 ns intervals. This sets the "Pen Up" position.  
    Default: 12000 (1.0 ms) on reset.
*   `SC,5,_servo_max_` Set the maximum value for the RC servo output position. _servo\_max_ may be in the range 1 to 65535, in units of 83.3 ns intervals. This sets the "Pen Down" position.  
    Default: 16000 (1.33 ms) on reset.
*   `SC,8,_maximum_S2_channels_` Sets the number of RC servo PWM channels, each of _S2\_channel\_duration\_ms_ before cycling back to channel 1 for S2 command. Values from 1 to 24 are valid for _maximum\_S2\_channels_.  
    Default: 8 on reset.
*   `SC,9,_S2_channel_duration_ms_` Set the number of milliseconds before firing the next enabled channel for the S2 command. Values from 1 to 6 are valid for _S2\_channel\_duration\_ms_.  
    Default: 3 ms on reset.
*   `SC,10,_servo_rate_` Set rate of change of the servo position, for both raising and lowering movements. Same units as _rate_ parameter in `[S2](#S2)` command.
*   `SC,11,_servo_rate_up_` Set the rate of change of the servo when going up. Same units as _rate_ parameter in `[S2](#S2)` command.
*   `SC,12,_servo_rate_down_` Set the rate of change of the servo when going down. Same units as _rate_ parameter in `[S2](#S2)` command.
*   `SC,13,_use_alt_prg_` - turns on (1) or off (0) alternate pause button function on RB0. On by default. For EBB v1.1 boards, it uses RB2 instead. See the description of `[QB](#QB)` for more information.

*   Example: `SC,4,8000\r` Set the pen-up position to give a servo output of 8000, about 0.66 ms.
*   Example: `SC,1,1\r` Enable only the RC servo for pen lift; disable solenoid control output.
*   Version History:
    *   Mode `SC,11,_servo_rate_up_` added in v1.9.2
    *   Mode `SC,12,_servo_rate_down_` added in v1.9.2
    *   Mode `SC,13,_use_alt_prg_` added in v2.0
    *   Mode `SC,8,_maximum_S2_channels_` added in v2.1.1
    *   Mode `SC,9,_S2_channel_duration_ms_` added in v2.1.1
    *   Mode `SC,2,2` added in v2.2.2.

* * *

#### "SE" — Set Engraver

*   Command: `SE,_state_[,_power_[,_use_motion_queue_]]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.1.0 and newer (with changes)
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _state_ may be either 0 to disable or 1 to enable the engraver output.
    *   _power_ is an optional argument, with allowed values of integers in the range 0 to 1023.
    *   _use\_motion\_queue_ is an optional argument, with allowed values of 0 (immediate) or 1 (use motion queue).
*   Description:
    
    This command is used to enable and disable the engraver PWM output on RB3 (called B3 on the board), and also set its output power. Use SE,0 to disable this feature.
    
    The _power_ argument represents the power (duty cycle of the PWM signal), where 0 is always off and 1023 is always on. If this optional argument is not included, then the power will be set at 512 (50%) duty cycle.
    
    If the _use\_motion\_queue_ parameter has the value of 1, then this SE command will be added to the motion queue just like SM and SP commands, and thus will be executed when the previous motion commands have finished. Note that if you need to use this argument, the _power_ argument is not optional. If _use\_motion\_queue_ has value 0 (or if it is omitted) the command is executed immediately, and is not added to the queue.
    
*   Example: `SE,1,1023\r` Turns on the engraver output with maximum power
*   Example: `SE,0\r` Turns off the engraver output
*   Example: `SE,0,0,1\r` Adds a command to the motion queue, that (when executed) turns off the engraver output.
*   Version History:
    *   Added in v2.1.0
    *   Versions prior to v2.2.2 do not properly set power, and instead use fixed 50% PWM.
    *   Version 2.4.1 adds the option to use the motion queue. Versions prior to this do not have this option. The proper command format for earlier versions is: `SE,_state_[,_power_]<CR>`

* * *

#### "SL" — Set Layer

*   Command: `SL,_NewLayerValue_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9.2 and newer
*   Execution: Immediate
*   Arguments:
    *   _NewLayerValue_ is an integer between 0 and 127.
*   Description:
    
    This command sets the value of the Layer variable, which can be read by the `[QL](#QL)` query. This variable is a single unsigned byte, and is available for the user to store a single variable as needed.
    
*   Example: `SL,4\r` Set the Layer variable to 4.
*   Example: `SL,125\r` Set the Layer variable to 125.
*   Version History: Added in v1.9.2

* * *

#### "SM" — Stepper Move

*   Command: `SM,_duration_,_AxisSteps1_[,_AxisSteps2_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All (with changes)
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _duration_ is an integer in the range from 1 to 16777215, giving time in milliseconds.
    *   _AxisSteps1_ and _AxisSteps2_ are integers, each in the range from -16777215 to 16777215, giving movement distance in steps.
*   Description:
    
    Use this command to make the motors draw a straight line at constant velocity, or to add a delay to the motion queue.
    
    If both _AxisSteps1_ and _AxisSteps2_ are zero, then a delay of _duration_ ms is executed. _AxisSteps2_ is an optional value, and if it is not included in the command, zero steps are assumed for axis 2.
    
    The sign of _AxisSteps1_ and _AxisSteps2_ represent the direction each motor should turn.
    
    The minimum speed at which the EBB can generate steps for each motor is 1.31 steps/second. The maximum speed is 25,000 steps/second. If the SM command finds that this speed range will be violated on either axis, it will output an error message declaring such and it will not complete the move. While individual movement commands may be as short as a single step, there are practical limits to the rate at which commands can be issued, as discussed under [Performance](#performance).
    
    Note that internally the EBB generates an Interrupt Service Routine (ISR) at the 25 kHz rate. Each time the ISR fires, the EBB determines if a step needs to be taken for a given axis or not. The practical result of this is that all steps will be 'quantized' to the 25 kHz (40 μs) time intervals, and thus as the step rate gets close to 25 kHz the 'correct' time between steps will not be generated, but instead each step will land on a 40 μs tick in time. In almost all cases normally used by the EBB, this doesn't make any difference because the overall proper length for the entire move will be correct.
    
    A value of 0 for _duration_ is invalid and will be rejected.
    
*   Example: `SM,1000,250,-766\r` Move axis 1 by 250 steps and axis2 by -766 steps, in 1000 ms of duration.
*   Version History:
    *   In versions prior to 2.2.1, the _duration_ parameter had a issue where values over 2621 would produce incorrect delays (when _AxisSteps1_ and _AxisSteps2_ are 0).
    *   For versions prior to v2.2.5, _duration_ must be a value in the range of 1 to 65535, and the _AxisSteps1_ and _AxisSteps2_ values must be in the range -32767 to +32767.
    *   As of version 2.2.5, the _duration_, _AxisSteps1_, and _AxisSteps2_ parameters for this command are full 24 bit numbers. _duration_ is an unsigned 24-bit value, while _AxisSteps1_ and _AxisSteps2_ are signed. This allows for very long stepper moves.
    *   As of version 2.2.8, the error messages for this command have been corrected. If an input parameter is too large (>16777215) or a step rate is too fast (>25 k steps/s) or too slow (<1.31 steps/s) a proper error will be printed back to the host.

* * *

#### "SN" — Set node count

*   Command: `SN,_value_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9.5 and newer
*   Execution: Immediate
*   Arguments:
    *   _value_ is an unsigned long (four byte) integer.
*   Description:
    
    This command sets the Node Counter to _value_.
    
    See the `[QN](#QN)` command for a description of the node counter and its operations.
    
*   Example: `SN,123456789\r` Set node counter to 123456789.
*   Version History: Added in v1.9.5

* * *

#### "SP" — Set Pen State

*   Command: `SP,_value_[,_duration_[,_portBpin_]]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All (with changes)
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _value_ is either 0 or 1, indicating to raise or lower the pen.
    *   _duration_ (optional) is an integer from 1 to 65535, which gives a delay in milliseconds.
    *   _portBpin_ (optional) is an integer from 0 through 7.
*   Description:
    
    This command instructs the pen to go up or down.
    
    *   When a _value_ of 1 is used, the servo will be moved to the _servo\_min_ value (as set by the "SC,4" command).
    *   When a _value_ of 0 is used, the servo will be moved to the _servo\_max_ value (as set by the "SC,5" command below).
    
    Note that conventionally, we have used the _servo\_min_ ("SC,4") value as the 'Pen up position', and the _servo\_max_ ("SC,5") value as the 'Pen down position'.
    
    The _duration_ argument is in milliseconds. It represents the total length of time between when the pen move is started, and when the next command will be executed. Note that this is not related to how fast the pen moves, which is set with the `[SC](#SC)` command. Rather, it is an intentional delay of a given _duration_, to force the EBB not to execute the next command (often an `[SM](#SM)`) for some length of time, which allows the pen move to complete and possibly some extra settling time before moving the other motors.
    
    If no _duration_ argument is specified, a value of 0 milliseconds is used internally.
    
    The optional _portBpin_ argument allows one to specify which portB pin of the MCU the output will use. If none is specified, pin 1 (the default) will be used.
    
    **Default positions:**The default position for the RC servo output (RB1) on reset is the 'Pen up position' (_servo\_min_), and at boot _servo\_min_ is set to 12000 which results in a pulse width of 1.0 ms on boot. _servo\_max_ is set to 16000 on boot, so the down position will be 1.33 ms unless changed with the "SC,5" Command.
    
    **Digital outputs:** On older EBB hardware versions 1.1, 1.2 and 1.3, this command will make the solenoid output turn on and off. On all EBB versions it will make the RC servo output on RB1 move to the up or down position. Also, by default, it will turn on RB4 or turn off RB4 as a simple digital output, so that you could use this to trigger a laser for example.
    
*   Example: `SP,1<CR>` Move pen-lift servo motor to _servo\_min_ position.
*   Version History:
    *   The _portBpin_ argument was added in firmware v2.1.1. Versions prior to this do not have this option. The proper command format for earlier versions is: `SP,_value_[,_duration_]<CR>`
    *   In versions prior to 2.2.4, the _duration_ parameter had two bugs. One was that the units for _duration_ were 40 μs (instead of the correct 1 ms). The second was that the change of state of the servo would happen AFTER the delay had elapsed. The servo move and the _duration_ delay are both started simultaneously now.

* * *

#### "SR" — Set RC Servo power timeout value

*   Command: `SR,_value_[,_state_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions:v2.6.0 and above
*   Execution:Immediate
*   Arguments:
    *   _value_ is a decimal unsigned long integer (32-bit) representing the number of milliseconds to wait after the last servo move before shutting off power to the RC Servo (RB1).
    *   _state_ is value of either 0 or 1, and is optional. It represents an immediate new state for the servo power (1 = on, 0 = off).
*   Description:
    
    This command sets a new RC Servo power timeout value and optionally a new immediate power state.
    
    The _value_ argument is in milliseconds.
    
    If _value_ is 0, then the auto-poweroff feature is disabled and the power will not be turned off to the RC servo once applied.
    
    On boot, the EBB will use a default value of 60 seconds. This means that 60 seconds after the last servo motion command, the RC servo power will be turned off.
    
    On boot, the power to the RC Servo (on pin RB1) will be off.
    
    Whenever any command that moves the RC Servo is received, power will also be turned on to the RC Servo connector (RB1), and the RC Servo countdown timer will be started. When the timer reaches 0, the power to the RC servo connector will be shut off.
    
    Only EBB boards v2.5 and above have the necessary power switch hardware. On other versions of the EBB hardware, the power to the servo is always on.
    
    Pin RA3 of the MCU is used to control the RC Servo power. So from software version 2.6.0 and above, this pin is now dedicated to RC Servo power control and can't be easily used for other things.
    
*   Example: `SR,60000,1<CR>` Set new RC servo power timeout value to 1 minute and turn power to the servo on.
*   Version History:
*   Added in v2.6.0.
*   Changed default power timeout value from 15 minutes to 60 seconds in v2.6.3.
*   Changed `SR` in v2.6.5 so that it no longer turned on power to the servo when stepper motor motion was commanded. Instead, it only turns on power when a servo motion is commanded. From v2.6.0 to v2.6.4, either stepper or servo motion would trigger power to the servo.

* * *

#### "ST" — Set EBB nickname Tag

*   Command: `ST,_NewNameString_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.5.5 and newer
*   Execution: Immediate
*   Arguments:
    *   _NewNameString_: A string of ASCII characters from 0 to 16 characters in length.
*   Description:
    
    This command sets the EBB's "nickname". This is an arbitrary, user settable string, which is stored in flash memory across reboots.
    
    After setting the EBBs nickname and rebooting, the EBB's USB Device Name will have the nickname appended at the end, after a comma. So if no name is set the Device Name will be "EiBotBoard,". But if the nickname is set to "East EBB", then the Device Name will be "EiBotBoard,East EBB". (The exact device name that appears to your computer is platform dependent.) The nickname will also appear as the USB device's "serial number." Note that the change may not be recognized by your computer until after you reboot the EBB. See the `[RB](#RB)` command.
    
    The nickname string can be any combination of ASCII characters, including an empty string which will clear the EBB's nickname. For best compatibility, use a nickname that is 3-16 characters in length, without apostrophes or quotation marks (single or double quotes) within the name.
    
    Since calling this command requires a change to a particular flash memory cell-- which can only be changed a finite number of times -- it is best practice to avoid any use that involves automated, repeated changes to the nickname tag.
    
    Use the `[QT](#QT)` command to retrieve the nickname at any time.
    
*   Version History: This command was originally introduced in v2.5.4, but should not be considered functional until version 2.5.5

* * *

#### "T" — Timed Analog/Digital Query

*   Command: `T,_duration_,_mode_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Arguments:
    *   _duration_ is from 1 to 65535 and represents the delay, in milliseconds, between reads for a given mode.
    *   _mode_ is 0 for digital or 1 for analog.
*   Description:
    
    This query turns on (or off) the timed digital (I packet) or analog (A packet) queries of pins. Using the T query you can set up a repeated query of input pins, and the generation of an I or A packet back to the PC. Each of the two modes (analog/digital) is independent of the other and can have a different duration time.
    
    For example, to turn the digital queries of all pins on, with a time of 250 ms between queries, use "T,250,0". Then, every 250 ms, the EBB will query all of the pins, and send an I response packet to the PC. This I response packet is exactly the same as the response to an "I" query, and simply contains the binary values of each pin of each port. To turn on the analog queries of any enabled analog inputs every 400 ms, use "T,400,1". This will cause the EBB to query all enabled analog inputs every 400 ms and send back an A packet (exactly the same as the reply to the "A" query) repeatedly. Note that while digital mode will query every pin, analog mode will only query (and report) the pins that are current configured as analog inputs. Pins do not have to be set to be digital inputs to be queried - no matter what the pin is set to, the "I" response packet will query the pin's digital state.
    
    To turn off a mode, use 0 for the duration parameter. Thus "T,0,0" will turn off digital mode, and "T,0,1" will turn off analog mode.
    
    The EBB is actually sampling the digital input pins at an extremely precise time interval of whatever you sent in the T query. The values of the pins are stored in a buffer, and then packet responses are generated whenever there is 'free time' on the USB back to the PC. So you can count the I packet responses between rising or falling edges of pin values and know the time between those events to the precision of the value of _duration_. This is true for digital mode. For analog mode the inputs are sampled every 1 ms. Each time the "A" timer times out, the latest set of analog values is used to create a new "A" packet and that is then sent out.
    
    Just because the EBB can kick out I and A packets every 1 ms (at its fastest) doesn't mean that your PC app can read them in that fast. Some terminal emulators are not able to keep up with this data rate coming back from the EBB, and what happens is that the EBB's internal buffers overflow. This will generate error messages being sent back from the EBB. If you write your own custom application to receive data from the EBB, make sure to not read in one byte at a time from the serial port - always ask for large amounts (10K or more) and then internally parse the contents of the data coming in. (Realizing that the last packet may not be complete.)
    
    If an attempt is made to have all 13 channels of analog be reported any faster than every 4 ms, then an internal EBB buffer overflow occurs. Be careful with the speed you choose for A packets. The maximum speed is based upon how many analog channels are being sent back.
    
*   Example:`T,250,0<CR>` Turn on digital reading of pins and generation of I packet every 250 ms.
*   Note: If the "I" or "A" packet responses stop coming back after you've done a "T" query, and you didn't stop them yourself (with a "T,0,0" or "T,0,1") then what's happened is that the internal buffer in the EBB for I or A packet data has been filled up. (There is room for 3 I packets and 3 A packets.) This means that the USB system is too busy to get the packet responses back to the PC fast enough. You need to have less USB traffic (from other devices) or increase the time between packet responses.

* * *

#### "TP" — Toggle Pen

*   Command: `TP[,_duration_]<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v1.9 and newer
*   Execution: Immediate
*   Arguments:
    *   _duration_: (Optional) an integer in the range of 1 to 65535, giving an delay in milliseconds.
*   Description:
    
    This command toggles the state of the pen (up->down and down->up). EBB firmware resets with pen in 'up' (_servo\_min_) state.
    
    Note that conventionally, we have used the _servo\_min_ ("SC,4") value as the 'Pen up position', and the _servo\_max_ ("SC,5") value as the 'Pen down position'.
    
    The optional _duration_ argument is in milliseconds. It represents the total length of time between when the pen move is started, and when the next command will be executed. Note that this is not related to how fast the pen moves, which is set with the `[SC](#SC)` command. Rather, it is an intentional delay of a given _duration_, to force the EBB not to execute the next command (often an `[SM](#SM)`) for some length of time, which allows the pen move to complete and possibly some extra settling time before moving the other motors.
    
    If no _duration_ argument is specified, a value of 0 milliseconds is used internally.
    
*   Version History:
    *   Added in v1.9
    *   In versions prior to v2.2.1, the _duration_ parameter had a bug and was in units of 40 μs rather than 1 ms.

* * *

#### "QN" — Query node count

*   Command: `QN<CR>`
*   Response: `_NodeCount_<CR><NL>OK<CR><NL>`
*   Firmware versions: v1.9.2 and newer
*   Execution: Immediate
*   Description: Query the value of the Node Counter.
    
    This command asks the EBB what the current value of the Node Counter is. The Node Counter is an unsigned long int (4 bytes) value that gets incremented or decremented with the `NI` and `ND` commands, or set to a particular value with the `SN` command. The Node Counter can be used to keep track of progress during various operations as needed.
    
    The value of the node counter can also be manipulated with the following commands:
    
    *   `SN` — Set Node count
    *   `NI` — Node count Increment
    *   `ND` — Node count Decrement
    *   `CN` — Clear node count \[obsolete\]
*   Example Return Packet: `1234567890<CR><NL>` then `OK<CR><NL>`
*   Version History: Added in v1.9.2

* * *

#### "V" — Version query

*   Command: `V<CR>`
*   Response: `EBBv13_and_above EB Firmware Version 2.4.2<CR><NL>`
*   Firmware versions: All
*   Execution: Immediate
*   Description:
    
    This command prints out the version string of the firmware currently running on the EBB. The actual version string returned may be different from the example above.
    

* * *

#### "XM" — Stepper Move, for Mixed-axis Geometries

*   Command: `XM,_duration_,_AxisStepsA_,_AxisStepsB_<CR>`
*   Response: `OK<CR><NL>`
*   Firmware versions: v2.3.0 and newer
*   Execution: Added to FIFO motion queue
*   Arguments:
    *   _duration_ is an integer in the range from 1 to 16777215, giving time in milliseconds.
    *   _AxisStepsA_ and _AxisStepsB_ are integers, each in the range from -16777215 to 16777215, giving movement distances in steps.
*   Description:
    
    This command takes the _AxisStepsA_ and _AxisStepsB_ values, and creates a call to the `[SM](#SM)` command with the SM command's _AxisSteps1_ value as _AxisStepsA_ + _AxisStepsB_, and _AxisSteps2_ as _AxisStepsA_ - _AxisStepsB_.
    
    This command is designed to allow cleaner operation of machines with mixed-axis geometry, including CoreXY, H-Bot gantry machines, and current AxiDraw models.
    
    If both _AxisStepsA_ and _AxisStepsB_ are zero, then a delay of _duration_ ms is executed.
    
    The minimum speed at which the EBB can generate steps for each motor is 1.31 steps/second. The maximum speed is 25 kSteps/second. If the XM command finds that this speed range will be violated on either axis, it will output an error message declaring such and it will not complete the move. Note that the range is checked on Axis 1 and Axis 2, NOT on Axis A and Axis B. (That is, the range is checked after performing the sum and difference.) While individual movement commands may be as short as a single step, there are practical limits to the rate at which commands can be issued, as discussed under [Performance](#performance).
    
    Note that internally the EBB generates an ISR at the 25 kHz rate. Each time the ISR fires, the EBB determines if a step needs to be taken for a given axis or not. The practical result of this is that all steps will be 'quantized' to the 25 kHz (40 μs) time intervals, and thus as the step rate gets close to 25 kHz the 'correct' time between steps will not be generated, but instead each step will land on a 40 μs tick in time. In almost all cases normally used by the EBB, this doesn't make any difference because the overall proper length for the entire move will be correct.
    
    A value of 0 for _duration_ is invalid and will be rejected.
    
*   Example: `XM,1000,550,-1234\r` Move 550 steps in the A direction and -1234 steps in the B direction, in duration 1000 ms.
*   Version History: Added in v2.3.0

* * *

Initial I/O pin configuration
-----------------------------

In addition to the stepper motor outputs, many applications make use of one or more digital I/O pins.

The most accessible and commonly used of the I/O pins are those in PortB. The eight pins in PortB are physically arranged into 3-pin "header connections", with ground, +5V power, and the "signal" I/O pin itself, from the edge of the board towards the center. Four of these connectors are located "below" the stepper motor terminals, and are labeled as B1, B0, B2, and B3, in order from the "bottom" edge of the board towards the stepper terminals. These four connections are pre-populated with header pins. Four additional connections, B4, B5, B6, and B7 are located on the "bottom" edge of the board, and are not pre-populated with header pins.

On EBB boards v2.5 and above, the 5V power to the pen servo (RB1) is controlled by software, and defaults to an off state at reset; see the `[SR](#SR)` command.

Pins B1, B0, B2 and B3 are not 5-volt tolerant and any voltage above about 3.6V will damage them. Pins B4, B5, B6 and B7 are 5-volt tolerant and will not be damaged by voltages up to 5.5V.

Because all Port B pins (B0 through B7) have weak pull up resistors, any of these pins can be used to read a switch by connecting a switch between the Port B pin and GND. Use the `[PI](#PI)` command to read the state of the switch. If that pin is not already an input at boot (see table below) you can make it an input using the `[PD](#PD)` command.

In addition to the pins of PortB, additional broken-out I/O pins accessible on the EBB include: PortA: RA0,1,2,3,5, PortC: RC0,1,2,6,7, PortD: RD: 0,1,4,5,6,7, and PortE: RE0. Every pin on PortB, PortC and RA6 can source or sink up to 25mA each. All other pins can source or sink up to 4mA each. Note that pins RA0, RC1, RD4, RD5, RD6, RD7 and RE0 are brought out to the I/O header but already have existing analog or digital functions mapped to them and so should only be used for monitoring these signals rather than as GPIO.

All pins of PortB have weak pull ups to 3.3V, which can source between 80 and 400 μA, and are enabled any time the pin is an input. Pull ups are not available on the other (Port A, C, D, E) GPIO pins. Many of the I/O pins can be used for general purpose digital I/O (GPIO), and some can also be used as RC servo outputs, within the limits of `[S2](#S2)`. With the exceptions listed in the table below and RA0, RC1, RD4, RD5, RD6, RD7 and RE0, all of the broken-out I/O pins are initially configured at boot as digital inputs.

Certain PortB signal pins are specially configured at boot time for typical applications, as summarized in the table below.


|Pin|Default Direction|Default State  |5V Tolerant|Typical application                       |
|---|-----------------|---------------|-----------|------------------------------------------|
|RB0|Input            |Weak pull up   |No         |Alternate PRG/Pause button input; see QB  |
|RB1|Output           |RC Servo Pulses|No         |Pen lift servo output; see SC, SP         |
|RB2|Input            |Weak pull up   |No         |General                                   |
|RB3|Output           |Low            |No         |Engraver or laser PWM output control      |
|RB4|Output           |Low            |Yes        |Alternate Pen Up/Down I/O (solenoid/laser)|
|RB5|Input            |Weak pull up   |Yes        |General                                   |
|RB6|Input            |Weak pull up   |Yes        |General                                   |
|RB7|Input            |Weak pull up   |Yes        |General                                   |


Performance
-----------

The EBB has some basic performance limits, which do vary with new firmware releases.

One performance aspect is the duration of the step pulses sent to the stepper driver chips. While the pulses produced by the EBB firmware will always be long enough to guarantee proper operation with the built-in drivers, it is possible to use some of the GPIO pins on the EBB to connect external step/dir driver electronics to drive much larger systems. In this case, the external drivers may have a minimum step pulse length, and so it can be important to know this timing information in that case.

Output step pulse duration for external stepper drivers:

*   EBB firmware 2.7.0 and above: 1.6 - 2.3 μs.
*   EBB firmware 2.6.5 and below: 2.8 - 3.0 μs.

Another important performance measure is the maximum rate at which sequential movement commands can be streamed to the EBB. This rate is expressed as the shortest move duration that can be sent to the EBB as a continuous stream of identical motion commands, such that there are no gaps between the last step of one move and the first step of the next move. For a high enough sustained rate of sufficiently short movement commands, the EBB will not be able to parse and queue each command prior to starting the subsequent move. The available CPU time available for parsing and queueing commands does depend on the active step rate, as the EBB shares CPU time between command parsing and step generation.

The following table shows the minimum move duration, in milliseconds, which the EBB can sustain indefinitely without gaps, under different move commands. The worst case values (highest sustained minimum move duration) were measured with step rates of approximately 25 kHz on both motors — a condition rarely achieved in typical applications. The best case values (lowest sustained minimum move duration) are for low step rates, and was measured at 2.5 kHz. You may be able to sustain movement at these rates with higher motion frequencies.


|Command|Firmware >= 2.7.0|Firmware < 2.7.0|
|-------|-----------------|----------------|
|SM     |3-4 ms           |4-7 ms          |
|XM     |3-4 ms           |4-6 ms          |
|LM, LT |3-5 ms           |4-6 ms          |


The times in the table above are measured under conditions where the PC sending the commands is able to sustain the same data rate. In practice, PCs — especially Windows PCs — can have occasional brief gaps when sending long strings of USB commands. The incidence of these gaps depend upon the system configuration, CPU speed, load, other USB communication, and additional factors. Best practice is to try and use fewer, longer-duration movement commands (rather than more, shorter-duration commands) whenever possible, to minimize the effects of both EBB CPU time constraints and any USB performance issues on the PC.

Frequently Asked Questions
--------------------------

**Q1)** How can I calculate how long it will take to move from one RC servo position to another? Specifically in relation to the standard pen-arm servo that is activated with the SP,1 and SP,0 commands.

**A1)** By default, with the latest version of EBB firmware, we add (or subtract) the rate value from the current pulse duration every time the pulse fires until the current pulse duration equals the target duration. Normally we have 8 servo channels available, and each gets 3 ms, so that means that each channel can fire once every 24 ms. So the rate value gets added or subtracted from the current pulse duration every 24 ms.

For example, if you're currently at a position (pulse duration) of 10000 and you send a new command to move to position 15000, then you have a 'distance' of 5000 to go. So when we start out, our current duration is 10000 and our target is 15000. If our rate value is 100, then it will take 50 of these 24 ms periods to get from 10000 to 15000, or 1.2 seconds total.

Now, when you're using the SP,0 and SP,1 commands, the servo\_min (defaults to 16000, or 1.33 ms) and servo\_max (defaults to 20000, or 1.6 ms) get used as the positions. And the servo\_rate\_up and servo\_rate\_down get used as the rates. So the formula is as follows:

((_servo\_max_ - _servo\_min_) \* .024)/_servo\_rate_ = total time to move

For the example above. ((15000 - 10000) \* .024)/100 = 1.2 seconds.

**Q2)** What do the LED patterns mean?

**A2)** There are two applications that live on the EBB: The bootloader and the main EBB firmware. They each have different LED blink patterns. There is a green power LED labeled 3.3V which is always lit as long as either the USB or barrel jack connector is receiving power. It is located next to the large electrolytic capacitor.

The LED timing mechanism used by both bootloader and main EBB applications is sensitive to the commands currently being executed. For example, the timing of the alternating LED pattern when the bootloader is running will change as a new EBB firmware application is being actively programmed.

**Bootloader** When the bootloader is running (only used to update main EBB firmware), the two LEDs between the USB and barrel jack connectors can take on two different states:



* Pattern: Idle
  * Description: Waiting for USB connection with host
  * USR/Red: Off
  * USB/Green: On
* Pattern: Alternating
  * Description: USB connection established to host
  * USR/Red: 200 ms on, 200 ms off, alternating with Green
  * USB/Green: 200 ms on, 200 ms off, alternating with Red


**Main EBB Firmware** When the main EBB firmware is running (normal operating mode) the two LEDs between the USB and barrel jack connectors can take on three different states:



* Pattern: Fast Blink
  * Description: No connection to USB host
  * USR/Red: Off
  * USB/Green: 60 ms on, 60 ms off
* Pattern: Slow Blink
  * Description: Connected to USB host but not enumerated
  * USR/Red: Off
  * USB/Green: 750 ms on, 750 ms off
* Pattern: Short Long Blink
  * Description: Fully enumerated and communicating with USB host
  * USR/Red: Off
  * USB/Green: 365 ms on, 365 ms off, 1.25s on, 365 ms off


The Fast Blink pattern is almost never seen in practice, since the USB host normally enumerates the EBB immediately upon connection with a USB cable. However, if proper drivers are not installed on the host or if there is some other problem with the USB enumeration process the Fast Blink pattern can be observed and used as a debugging aid.

* * *

[![Creative Commons License](images/88x31.png)](http://creativecommons.org/licenses/by/3.0/us/)

EiBotBoard by [Brian Schmalz](http://www.schmalzhaus.com/EBB) is licensed under a [Creative Commons Attribution 3.0 United States License](http://creativecommons.org/licenses/by/3.0/us/). Based on a work at [www.schmalzhaus.com/EBB](http://www.schmalzhaus.com/EBB). Permissions beyond the scope of this license may be available at [www.schmalzhaus.com/EBB](http://www.schmalzhaus.com/EBB).

* * *

Extended EggBot documentation available at: [http://wiki.evilmadscientist.com/eggbot](http://wiki.evilmadscientist.com/eggbot)