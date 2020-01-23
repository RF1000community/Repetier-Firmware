/*
    This file is part of the Repetier-Firmware for RF devices from Conrad Electronic SE.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Repetier-Firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Repetier-Firmware.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef REPETIER_H
#define REPETIER_H

#include "Constants.h"

//https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html#Stringizing for using number-#defines in string-#defines
#define xstr(a) str(a)
#define str(a) #a

#include "Configuration.h"

#if MOTHERBOARD == DEVICE_TYPE_RF1000
#include "RF1000.h"
#endif // MOTHERBOARD == DEVICE_TYPE_RF1000

#if MOTHERBOARD == DEVICE_TYPE_RF2000
#include "RF2000.h"
#endif // MOTHERBOARD == DEVICE_TYPE_RF2000

#if MOTHERBOARD == DEVICE_TYPE_RF2000v2
#include "RF2000v2.h"
#endif // MOTHERBOARD == DEVICE_TYPE_RF2000v2

#include "pins.h"
#include "HAL.h"
#include "gcode.h"
#include "RF.h"
#include "ui.h"
#include "Communication.h"

#undef min
#undef max

class RMath {
public:
    static inline float min(float a, float b) {
        if (a < b)
            return a;
        return b;
    } // min

    static inline float max(float a, float b) {
        if (a < b)
            return b;
        return a;
    } // max

    static inline long min(long a, long b) {
        if (a < b)
            return a;
        return b;
    } // min

    static inline long max(long a, long b) {
        if (a < b)
            return b;
        return a;
    } // max

    static inline int min(int a, int b) {
        if (a < b)
            return a;
        return b;
    } // min

    static inline int max(int a, int b) {
        if (a < b)
            return b;
        return a;
    } // max

    static inline unsigned int min(unsigned int a, unsigned int b) {
        if (a < b)
            return a;
        return b;
    } // min

    static inline unsigned int max(unsigned int a, unsigned int b) {
        if (a < b)
            return b;
        return a;
    } // max

    static inline long sqr(long a) {
        return a * a;
    } // sqr

    static inline float sqr(float a) {
        return a * a;
    } // sqr
};

extern const uint8 osAnalogInputChannels[] PROGMEM;
extern volatile uint8 osAnalogInputCounter[ANALOG_INPUTS];
extern volatile uint osAnalogInputBuildup[ANALOG_INPUTS];
extern volatile uint8 osAnalogInputPos; // Current sampling position
extern volatile uint osAnalogInputValues[ANALOG_INPUTS];
extern uint8_t pwm_pos[NUM_EXTRUDER + 3]; // 0-NUM_EXTRUDER = Heater 0-NUM_EXTRUDER of extruder, NUM_EXTRUDER = Heated bed, NUM_EXTRUDER+1 Board fan, NUM_EXTRUDER+2 = Fan
extern uint8_t fanSpeed;                  //remember user input fan speed at a 0..255 scale.

#if USE_ADVANCE
extern int maxadv2;
extern float maxadvspeed;
// Delay filament relax at the end of print, could be a simple timeout
extern volatile int waitRelax;
#endif // USE_ADVANCE

#include "Extruder.h"

#if FEATURE_DITTO_PRINTING && NUM_EXTRUDER != 2
#error Ditto printing requires exactly 2 extruder.
#endif // FEATURE_DITTO_PRINTING && NUM_EXTRUDER!=2

extern millis_t previousMillisCmd;
extern millis_t maxInactiveTime;
extern millis_t stepperInactiveTime;

extern void drv8711Init();

#include "Printer.h"
#include "motion.h"

extern long baudrate;

extern volatile uint8_t execute100msPeriodical;
extern volatile uint8_t execute50msPeriodical;
extern volatile uint8_t execute16msPeriodical;
extern volatile uint8_t execute10msPeriodical;

#if FAN_PIN > -1 && FEATURE_FAN_CONTROL
extern uint8_t fanKickstart;
#endif // FAN_PIN>-1 && FEATURE_FAN_CONTROL

#if SDSUPPORT
#define SHORT_FILENAME_LENGTH 14
extern char tempLongFilename[LONG_FILENAME_LENGTH + 1];
extern char fullName[LONG_FILENAME_LENGTH * SD_MAX_FOLDER_DEPTH + SD_MAX_FOLDER_DEPTH + 1];
#include "SdFat.h"

inline void memcopy2(void* dest, void* source) {
    *((int16_t*)dest) = *((int16_t*)source);
}
inline void memcopy4(void* dest, void* source) {
    *((int32_t*)dest) = *((int32_t*)source);
}

class SDCard {
public:
    SdFat fat;
    SdFile file;
    uint32_t filesize;
    uint32_t sdpos;
    char* shortname; // Pointer to start of filename itself
    char* pathend;   // File to char where pathname in fullname ends
    uint8_t sdmode;  // 1 if we are printing from sd card, 2 = stop accepting new commands
    bool sdactive;
    bool savetosd;

    SDCard();
    void initsd(bool silent = false);
    void writeCommand(GCode* code);
    bool selectFile(char *filename, bool silent = false);
    void mount(bool silent = false);
    void unmount();
    void startPrint();

    inline void setIndex(uint32_t newpos) {
        if (!sdactive)
            return;
        sdpos = newpos;
        file.seekSet(sdpos);
    }

    void printStatus();
    void ls();
    void startWrite(char* filename);
    void deleteFile(char* filename);
    void finishWrite();
    void writePSTR(FSTRINGPARAM(str));
    char* createFilename(char* buffer, const dir_t& p);
    void makeDirectory(char* filename);
    bool showFilename(const uint8_t* name);
    void automount();
};

extern SDCard sd;
#endif // SDSUPPORT

#include "Commands.h"
#include "Eeprom.h"

#endif // REPETIER_H
