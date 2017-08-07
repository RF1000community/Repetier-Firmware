[<img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/ae/Flag_of_the_United_Kingdom.svg/100px-Flag_of_the_United_Kingdom.svg.png" height="30">](README.md)
[<img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/a4/Flag_of_the_United_States.svg/100px-Flag_of_the_United_States.svg.png" height="30">](README.md)
[<img src="https://upload.wikimedia.org/wikipedia/commons/thumb/b/ba/Flag_of_Germany.svg/100px-Flag_of_Germany.svg.png" height="30">](README.de_DE.md)

# Inofficial modification of the RF firmware for RF1000 and RF2000 devices
Based on Repetier-Firmware - the fast and user friendly firmware.

## Disclaimer

THIS VERSION IS AN UNOFFICIAL MODIFICATION OF mhiers UNOFFICIAL MODIFICATION OF THE ORIGINAL FIRMWARE. It is neither endorsed nor
supported by the developers of the RF1000 firmware.  
USE AT YOUR OWN RISK.

For the official version, have a look at the upstream repository:
https://github.com/RF1000/Repetier-Firmware (see branch development)

## HowTo Install

- Download the Firmware `Branch: community_development` and unzip all the files.  
- Install Arduino.cc 1.6.5 or 1.8.3 and later if it is not installed on your computer already.  
- Edit and save Configuration.h @Line46 and 47 according to your printers model. You have to remove the two **//** in front of the printers name you wish to activate:  
`#define MOTHERBOARD                         DEVICE_TYPE_RF1000` or  
`#define MOTHERBOARD                         DEVICE_TYPE_RF2000`
- Optional: Edit and save the Configuration.h and RF1000.h or RF2000.h to your needs in case you use special specialized hardware.
- Connect your Printer with a USB-cable and switch it on.
- Go to and doubleclick /Repetier/Repetier.ino
- Within the cyan colored Arduino-IDE you have to choose some preconfiguration settings for your RFx000 Board at the menu **Tools->**:  
Board: `Arduino Mega 2560 or Mega ADK`,  
Processor: `ATMega 2560 (Mega 2560)`,  
Port: Your printers Port like `COM3`, if successfully connected.
- **Check** and **Upload** the Firmware to your Printer.
## If you upgrade to this Version from 1.37r or earlier please do a fresh M303 PID-Autotune on all heaters!  
- RF2000/RF1000 extruder 1: `M303 P0 X0 S230 R10 J1`
- RF2000 extruder 2: `M303 P1 X0 S230 R10 J1`
- RF2000 heated bed: `M303 P2 X0 S70 R15 J3`
- RF1000 heated bed: `M303 P1 X0 S70 R15 J3`  
And start with EEPROM-values `PID drive min = 5` and `PID drive max = 100`  
Or set #define PID_CONTROL_DRIVE_MIN_LIMIT_FACTOR to 10.0f in configuration.h to use the old version PID-control.

## Version RF.01.37mod 

See:  
http://www.rf1000.de/viewtopic.php?f=74&t=1674 (Nibbels/Wessix SenseOffset-Thread)  
http://www.rf1000.de/viewtopic.php?f=7&t=1504#p14882 (mhier Mod)  
http://www.rf1000.de/viewtopic.php?f=7&t=1504&start=60#p16397 (added feature)  
http://www.rf1000.de/viewtopic.php?f=7&t=1610 (rf1000s developement version thread to 1.35)  
http://www.rf1000.de/viewtopic.php?f=7&t=1665 (rf1000s developement version thread to 1.37)  
https://github.com/Nibbels/Repetier-Firmware/commits/heat_bed_z_offset_scan-%26-heat_bed_decrease (all the commits)

## Main Features of this Mod:
* mhier Z-Offset-Scan
* Nibbels/Wessix SensiblePressure Adjustment

## List of Features and additional G-Codes for RF2000 and RF1000

_by mhier_:  
M3900               - Scan the heatbeds offset and correct the active zMatrix within the RAM of the Printer. (X-Y-Pos, Learning-Factor, linear distance weight.)  
RF2000: Z_OVERRIDE_MAX = 1.0mm, instead of 0.5mm  
RF1000: Z_OVERRIDE_MAX = 1.0mm, instead of 0.5mm  

_by StarTony_:  
Fan-Speed-Patch, see http://www.rf1000.de/viewtopic.php?f=7&t=1638  

_by Wessix_:  
M355 Sx - Turn on and off, or switch Port X19 to controll lights etc. (24V-MOSFET @Connector X19)  
M355    - Switch Port X19 to controll lights etc. (24V-MOSFET @Connector X19)  
Idea to implement the SensiblePressure-Function, see M3909  
Idea to send the active digits with the printTemperature-Status.

_by Nibbels_:  
M3900 Xn Ym Px Sy   - Scan the heatbeds offset and correct the active zMatrix within the RAM of the Printer. (X-Y-Pos, Learning-Factor, linear distance weight.)  
M3901 Xn Ym Px Sy   - to preconfigure M3900 (X-Y-Pos, Learning-Factor, linear distance weight.)  

M3902 R1            - to fix a single HeatBeds Hole within the HBS Matrix.  
M3902 Zn.n          - to add an Offset to the Matrix. n.n = {-0.2 .. 0.2} [mm]  
M3902 Z0            - to shift your active zOffset to the zMatrix. The M3006 zOffset will be zero afterwards. The zMatrix is altered within Ram and might be saved to EEPROM[n] with M3902 S[n] afterwards.  
M3902 Sn            - to save the active Matrix to position n = {1..9}  
M3902 Z0 S1         - to shift the zOffset to your zMatrix and save the Matrix at position 1. This is an example to show that the options of M3902 can be combined.  
M3939 Fn St1 Pt2 Ex Iy Rm   - to messure a curve of your Filaments velocity over digits -> viscosity.  

M3920 Sb - to go into or switch back from SilentMode (This will lower your steppers current to an alternative profile)  
Removed all compiler warnings and errors within the original Firmware.  
Included some Fixes the original developers of the branch "repetier/Repetier-Firmware" committed to their firmware.  
Upgraded the Firmware to the latest RF.01.37 (2017-01-20)  
Activated the X35 Temperature-Sensor for RF2000.  
Listed all spare pins for RF1000 or RF2000.  
Fixed the RF2000's Status Message length to 20chars.  
Fix for M3117
Third Z-Scale Mode: G-Code @ Configuration->Z-Configuration->Z-Scale.

_by Nibbels/Wessix_:  
M3909 Pn Sm         - See "Nibbels/Wessix SenseOffset"  

## mhier Z-Offset-Scan
* M3900 Xn Ym Sy Px             - Run a Z-Offset Scan at the specified Location.

Preconfiguration Options for M3900:  
* M3901 Xn Ym Sy Px 

[X]/[Y] specifies the Location of the Scan within the original HBS-Locations.  
X = {1..9}, {0 = random}
Y = {2..12}, {0 = random}
With two extruders X=1 is allowed otherwise forbidden. Normally X=2 is the lowest possible scan position. This limit is caused by border values within every zMatrix. You cannot set it wrong, the upper and lower limits are limited and autoadjusted by the firmware if set wrong.  

[S] specifies the learning rate of M3900  
S= {100}  
The measured offset will be added/substracted by 100% of its Value from/to the original matrix. The original matrix is the matrix stored in your EEPROM. Thus the matrix will be reloaded from the EEPROM to the RAM prior to the following z-offset scan.
S= {0..99}  
the Matrix in RAM will be adjusted by 0% to 99% of the messured offset. This is a great feature for multiple incremental corrections, in case you would not want to start allover or you cannot trust your first values 100% (you want to sum up corrections from different scanning-locations).

[P] ZOS learning linear distance weight  
P = {0..100}  
This is a configuration option to commit a scans offset to its surrounding area only.  
If you scan the middle of the heated bed and then all the corners with a high distance weight, this feature will behave like autobed leveling of your zMatrix.
Example: Put P to a high value and messure all corners. Then the matrix is somehow "bended" and not constantly updated. (Might work very well on heat-beds which tend to lift edges within different temperature ranges or to correct tilting).

You can activate the mhier Z-Offset-Scan within ->Configuration->Z-Configuration->Z-Offset Scan within your printers menu structure. This will always use the preconfigured or changed (see M3901) settings within your printers RAM. These M3900/M3901-Settings are not stored within the printers EEPROM.  

## Nibbels/Wessix SenseOffset  
* M3909 Pn Sm  
P = max. digits = {1...14999} [digits]  
S = max. SenseOffset = {1...300} std.: 180 [um]  

Feature called "Nibbels/Wessix SenseOffset". This is a digit limiter which decreases the printers digits by lowering the heatbed. It compensates the thermal expansion of some second order printer parts.  
 The printer will automaticly release Pressure inbetween the Nozzle and the HeatBed, whenever the digits rise atop [max.digits] by adjusting the Z-Offset. 
 The adjustment-offset is fixed to positive values (bed does never go closer to the nozzle than without M3909). The adjustment will not violate the "max. SenseOffset" restriction in order to avoid unwanted offsets (not caused by too close nozzle).  
 When choosing "max. SenseOffset" you should already know how much digits your printer normally has (Same Material, same Temperatures, same Speed, same Nozzle) and then add some plus 20% or plus 1000digits of force-tolerance.  
 Senseoffset can have two states. Active controling and idle. It is idle, whenever Z is not within negative and ~0.3mm height. You can tune this height limit by tweaking M3007 (http://www.rf1000.de/wiki/index.php/GCodes#M3007_-_Minimale_H.C3.B6he_f.C3.BCr_Z-Kompensation_festlegen). Whenever there is active control you see a circumflex "^" within the modmenu, instead of the @-Symbol.
 ![ ](https://downfight.de/picproxy.php?url=https://downfight.de/grafiken/dimi/modmenudisplay_eng.png "Angebratenes ABS")  
 When the z-compensation gets deactivated the senseoffset-function is deactivated as well.  
 Use "M3909 P0" for manual shutdown of the feature, but normally this is not necessary.  
 You cannot activate the Feature if zCompensation is not active already.  
 
 While Printing you can adjust or activate this feature with the "Left"- and "Right"-Button, while being in the "Mod-Menu" which is page 2 on the printers display.  

## SensibleViscosity  
* M3939 Fn St1 Pt2   Ex Iy Rm  
S = Starting Temperature for the Test in [°C]  
P = Final Temperature for the Test in [°C]  
F = max. test digits = 10000 or {1...12000} [digits]  
E = max. test extrusion velocity = 5 or {0.05...maxStartFeedrate} [mm/s]  
optional:  
I = test velocity increment = 0.1 or {0.02...0.4} [mm/s]  
R = max. digit change needed to consider having a filled nozzle. = 800 [digits] 

Feature called "SensibleViscosity"  
 The printer will automaticly extrude into the thin air and messure the resulting force. 
 After every messurement the velocity of extrusion is increased.  
 You will get a CSV-Output within your Console-Window (Repetierserver/Repetierhost/Octoprint/...) which you can easily copy and paste to some text file. Excel can then read the Data and you can put it into some Chart with 2 Clicks.
 You might ask why you need this feature, but in case you know your Filament quite well you can compare these resulting chars to really see how your filament extrusion behaves under different circumstances or when your nozzle is or gets clogged.
 For longer heated/boiled (my) ABS: http://www.rf1000.de/viewtopic.php?f=62&t=1549&p=16617#p16617  
 ![ ](https://downfight.de/picproxy.php?url=http://image.prntscr.com/image/3508699873f7431489d1df9344110e7f.png "Boiled ABS")  
 For the general viscosity of (my) ABS: http://www.rf1000.de/viewtopic.php?f=23&t=1620&p=16613#p16605  
 ![ ](https://downfight.de/picproxy.php?url=http://image.prntscr.com/image/610de7720945474590668b20c05d7652.png "Temperature ABS")  
 That is a clogged nozzle with the same ABS:
 ![ ](https://downfight.de/picproxy.php?url=http://image.prntscr.com/image/c00ac11b3a384994b37ce8bc3cf03bd9.png "Clogged Nozzle ABS")  
 That is red PLA:
 ![ ](https://downfight.de/picproxy.php?url=http://image.prntscr.com/image/2a3253c930794afc81e4fa4d4b2a4261.png "Clogged Nozzle ABS")  

 Have fun finally seeing your Filaments behaviour over time, temp, nozzles ;)  
 
## SilentMode   
* M3920 Sb  
S = {0,1} Switch to SilentMode or back to normal stepper current and unhome your printer.  

This Feature might be handy if you want to predefine another set of stepper current, but you dont want to have this lower values all the time. 

If you make your current too low you might risk "lost steps". The Motor cannot stand the applied force and you might get shifted Layers (X,Y). If that is the case you should lower your acceleration or rise up the current again.

If you can lower your current, you will have a huge improvement on stepper temperature and noise production as well. The tone of your steppers might sound deeper and will not be such a pain as the original (milling like-) configuration.
You should not include M3920 when your z-Compensation is active. This MCode will shut down z-Compensation and unhome your Printer. You would have to re-home your axes and activate z-Compensation again. I put this code to the beginning of my startcodes.

## Dualhead Tip-Down Support (beta)
* M3919 [S]mikrometer - Testfunction for Dip-Down-Hotend @ T1: T1 can now be springloaded and the bed will be adjusted "down" whenever T1 is selected. This is alike Ultimaker 3 does it with the right hotend.   
Example: M3919 T1 Z-0.6 tells the Printer that the right hotend will reach 0.6mm more down than the left hotend. Test it :)

## Feature Digit-Z-Kompensation (beta)
If you apply force to the DMS sensors the bed is adjusted (if Z-Compensation is active). This corrects the error which bending of the sensors apply to your layers. The physical change of the beds level is approximately +0,01mm for +1000 applied force (digits).

## RF2000: Additional Temperature Sensor
This optional 3rd temperature T3 is automatically sent out with the other Temperatures and Digits.  
``` 15:16:14.637: T:204.67 /205 B:28.60 /20 B@:0 @:143 T0:28.60 /0 @0:0 T1:204.67 /205 @1:143 T3:28.23 F:322 ```
See RESERVE_ANALOG_SENSOR_TYPE in RF2000.h for configuration options.  
See RESERVE_ANALOG_INPUTS to find the involved code within the firmware.  
You will need to install and additional cable and a temperature sensor at X35 on your printers Board. Then you can wire your sensor to some place needed.  
This sensor might keep track of your boards temperature or the air-temperature within your enclosure.  

## Wessix`s help video:
[![ScreenShot](https://downfight.de/picproxy.php?url=http://image.prntscr.com/image/d7b7fade0c7343eeb67b680339478894.png)](http://youtu.be/iu9Nft7SXD8)

## !! 03.02.2017: Project is Work in Progress and untested changes are possible.
## !! 28.02.2017: This mod should be save to complile with Arduino 1.8.1. Former problems are gone.
