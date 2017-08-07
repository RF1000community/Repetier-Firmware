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


#include "Repetier.h"
#include "pins_arduino.h"

#if EEPROM_MODE!=0
#include "Eeprom.h"
#endif // EEPROM_MODE!=0


uint8_t             manageMonitor = 255; ///< Temp. we want to monitor with our host. 1+NUM_EXTRUDER is heated bed
volatile uint8_t    execute100msPeriodical = 0;
volatile uint8_t    execute16msPeriodical = 0;
volatile uint8_t    execute10msPeriodical = 0;

#if FEATURE_DITTO_PRINTING
uint8_t             Extruder::dittoMode = 0;
#endif // FEATURE_DITTO_PRINTING

#if ANALOG_INPUTS>0
const uint8         osAnalogInputChannels[] PROGMEM = ANALOG_INPUT_CHANNELS;
volatile uint8      osAnalogInputCounter[ANALOG_INPUTS];
volatile uint       osAnalogInputBuildup[ANALOG_INPUTS];
volatile uint8      osAnalogInputPos=0; // Current sampling position
volatile uint       osAnalogInputValues[ANALOG_INPUTS];
#endif // ANALOG_INPUTS>0

#ifdef USE_GENERIC_THERMISTORTABLE_1
short               temptable_generic1[GENERIC_THERM_NUM_ENTRIES][2];
#endif
#ifdef USE_GENERIC_THERMISTORTABLE_2
short               temptable_generic2[GENERIC_THERM_NUM_ENTRIES][2];
#endif
#ifdef USE_GENERIC_THERMISTORTABLE_3
short               temptable_generic3[GENERIC_THERM_NUM_ENTRIES][2];
#endif

/** Makes updates to temperatures and heater state every call.
Is called every 100ms.
*/
static uint8_t extruderTempErrors = 0;
void Extruder::manageTemperatures()
{
#if FEATURE_MILLING_MODE
    if( Printer::operatingMode != OPERATING_MODE_PRINT )
    {
        // we do not check temperatures in case we are not in operating mode print
        return;
    }
#endif // FEATURE_MILLING_MODE

    uint8_t errorDetected = 0;

    for(uint8_t controller=0; controller<NUM_TEMPERATURE_LOOPS; controller++)
    {
        TemperatureController *act = tempController[controller];

        // Get Temperature
        act->updateCurrentTemperature();
        if(controller == autotuneIndex) continue;  // Ignore heater we are currently testing

        if(controller<NUM_EXTRUDER)
        {
#if NUM_EXTRUDER>=2 && EXT0_EXTRUDER_COOLER_PIN==EXT1_EXTRUDER_COOLER_PIN && EXT0_EXTRUDER_COOLER_PIN>=0
            if(controller==1 && autotuneIndex!=0 && autotuneIndex!=1)
                if(tempController[0]->currentTemperatureC<EXTRUDER_FAN_COOL_TEMP && tempController[0]->targetTemperatureC<EXTRUDER_FAN_COOL_TEMP &&
                        tempController[1]->currentTemperatureC<EXTRUDER_FAN_COOL_TEMP && tempController[1]->targetTemperatureC<EXTRUDER_FAN_COOL_TEMP)
                    extruder[0].coolerPWM = 0;
                else
                    extruder[0].coolerPWM = extruder[0].coolerSpeed;
            if(controller>1)
#endif // NUM_EXTRUDER>=2 && EXT0_EXTRUDER_COOLER_PIN==EXT1_EXTRUDER_COOLER_PIN && EXT0_EXTRUDER_COOLER_PIN>=0

                if(act->currentTemperatureC<EXTRUDER_FAN_COOL_TEMP && act->targetTemperatureC<EXTRUDER_FAN_COOL_TEMP)
                    extruder[controller].coolerPWM = 0;
                else
                    extruder[controller].coolerPWM = extruder[controller].coolerSpeed;
        }

        if(!Printer::isAnyTempsensorDefect() && (act->currentTemperatureC < MIN_DEFECT_TEMPERATURE || act->currentTemperatureC > MAX_DEFECT_TEMPERATURE))   // no temp sensor or short in sensor, disable heater
        {
            extruderTempErrors++;
            errorDetected = 1;

            if(extruderTempErrors > 10)   // Ignore short temporary failures
            {
                Printer::flag0 |= PRINTER_FLAG0_TEMPSENSOR_DEFECT;
                reportTempsensorError();

                showError( (void*)ui_text_temperature_manager, (void*)ui_text_sensor_error );
            }
        }
        if(Printer::isAnyTempsensorDefect()) continue;
        uint8_t on = act->currentTemperature>=act->targetTemperature ? LOW : HIGH;

        if(!on && act->isAlarm())
        {
            beep(50*(controller+1),3);
            act->setAlarm(false);  //reset alarm
        }

        act->tempArray[act->tempPointer++] = act->currentTemperatureC;
        //act->tempPointer &= 3; // 3 = springe von 4 = 100b auf 0 zurück,    wenn 3. -> 1/300ms  -> 3.33 = reciproke     !!tempArray needs [4] ...
        //act->tempPointer &= 7; // 7 = springe von 8 = 1000b auf 0 zurück,   wenn 7. -> 1/700ms  -> 1.42 = reciproke     !!tempArray needs [8] ...
        act->tempPointer &= 15; // 15 = springe von 16 = 10000b zurück auf 0, wenn 15 -> 1/1500ms -> 0.666 = reciproke    !!tempArray needs [16] ...
        if(act->heatManager == 1)
        {
            uint8_t output;
            float error = act->targetTemperatureC - act->currentTemperatureC;

            if( act->targetTemperatureC < 20.0f )
            {
                output = 0; // off is off, even if damping term wants a heat peak!
            } 
            else if( error > PID_CONTROL_RANGE )
            {
                output = act->pidMax;
            }
            else if( error < -PID_CONTROL_RANGE )
            {
                output = 0;
            }
            else
            {
                float pidTerm = 0;

                float pgain = act->pidPGain * error;
                pidTerm += pgain;
                act->tempIState = constrain(act->tempIState + error, act->tempIStateLimitMin, act->tempIStateLimitMax);
                float igain = act->pidIGain * act->tempIState * 0.1;  // 0.1 = 10Hz
                pidTerm += igain;
                float dgain = act->pidDGain * (act->tempArray[act->tempPointer] - act->currentTemperatureC)*0.666f; // raising dT/dt, 3.33 = reciproke of time interval (300 ms) -> temparray greift weiter zurück als letzte messung.
                pidTerm += dgain;

#if SCALE_PID_TO_MAX==1
                pidTerm = (pidTerm*act->pidMax)*0.0039062;
#endif // SCALE_PID_TO_MAX==1

                output = constrain((int)pidTerm, 0, act->pidMax);
/*
                Com::printF( PSTR( " err:" ), error,5  );
                Com::printF( PSTR( " IMin:" ), act->tempIStateLimitMin,5 );
                Com::printF( PSTR( " IMax:" ), act->tempIStateLimitMax,5  );
                Com::printF( PSTR( " I:" ), act->tempIState,5  );
                Com::printF( PSTR( " KP:" ), act->pidPGain,5  );
                Com::printF( PSTR( " KI:" ), act->pidIGain,5  );
                Com::printF( PSTR( " KD:" ), act->pidDGain,5  );
                Com::printF( PSTR( " pG:" ), pgain,5  );
                Com::printF( PSTR( " iG:" ), igain,5  );
                Com::printF( PSTR( " dG:" ), dgain,5  );
                Com::printF( PSTR( " Term:" ), pidTerm ,5 );
                Com::printFLN( PSTR( " = " ), output );
*/
            }
            pwm_pos[act->pwmIndex] = output;
        }
        else if(act->heatManager == 3)     // dead-time control
        {
            uint8_t output;
            float error  = act->targetTemperatureC - act->currentTemperatureC;
            float target = act->targetTemperatureC;

            if( act->targetTemperatureC < 20.0f )
			{
                output = 0; // off is off, even if damping term wants a heat peak!
			}
            else if( error > PID_CONTROL_RANGE )
			{
                output = act->pidMax;
			}
            else if( error < -PID_CONTROL_RANGE )
			{
                output = 0;
			}
            else
            {
                float raising = 3.333 * (act->currentTemperatureC - act->tempArray[act->tempPointer]); // raising dT/dt, 3.33 = reciproke of time interval (300 ms)
                act->tempIState = 0.25 * (3.0 * act->tempIState + raising); // damp raising
                output = (act->currentTemperatureC + act->tempIState * act->pidPGain > target ? 0 : act->pidDriveMax);
            }
            pwm_pos[act->pwmIndex] = output;
        }
        else

            if(act->heatManager == 2)    // Bang-bang with reduced change frequency to save relais life
            {
                uint32_t time = HAL::timeInMilliseconds();
                if (time - act->lastTemperatureUpdate > HEATED_BED_SET_INTERVAL)
                {
                    pwm_pos[act->pwmIndex] = (on ? 255 : 0);
                    act->lastTemperatureUpdate = time;
                }
            }
            else     // Fast Bang-Bang fallback
            {
                pwm_pos[act->pwmIndex] = (on ? 255 : 0);
            }

#ifdef EXTRUDER_MAX_TEMP
        if(act->currentTemperatureC>EXTRUDER_MAX_TEMP) // Force heater off if EXTRUDER_MAX_TEMP is exceeded
            pwm_pos[act->pwmIndex] = 0;
#endif // EXTRUDER_MAX_TEMP

#if LED_PIN>-1
        if(act == &Extruder::current->tempControl)
            WRITE(LED_PIN,on);
#endif // LED_PIN>-1
    }

    if(errorDetected == 0 && extruderTempErrors>0)
        extruderTempErrors--;

    if(Printer::isAnyTempsensorDefect())
    {
        for(uint8_t i=0; i<NUM_TEMPERATURE_LOOPS; i++)
        {
            pwm_pos[tempController[i]->pwmIndex] = 0;
        }
        Printer::debugLevel |= 8; // Go into dry mode
    }

} // manageTemperatures


void Extruder::initHeatedBed()
{
#if HAVE_HEATED_BED
    heatedBedController.updateTempControlVars();
#endif // HAVE_HEATED_BED

} // initHeatedBed


#if defined(USE_GENERIC_THERMISTORTABLE_1) || defined(USE_GENERIC_THERMISTORTABLE_2) || defined(USE_GENERIC_THERMISTORTABLE_3)
void createGenericTable(short table[GENERIC_THERM_NUM_ENTRIES][2],short minTemp,short maxTemp,float beta,float r0,float t0,float r1,float r2)
{
    t0 += 273.15f;
    float rs, vs;
    if(r1==0)
    {
        rs = r2;
        vs = GENERIC_THERM_VREF;
    }
    else
    {
        vs =static_cast<float>((GENERIC_THERM_VREF * r1) / (r1 + r2));
        rs = (r2 * r1) / (r1 + r2);
    }
    float k = r0 * exp(-beta / t0);
    float delta = (maxTemp-minTemp) / (GENERIC_THERM_NUM_ENTRIES - 1.0f);
    for(uint8_t i = 0; i < GENERIC_THERM_NUM_ENTRIES; i++)
    {
        float t = maxTemp - i * delta;
        float r = exp(beta / (t + 272.65)) * k;
        float v = 4092 * r * vs / ((rs + r) * GENERIC_THERM_VREF);
        int adc = static_cast<int>(v);
        t *= 8;
        if(adc > 4092) adc = 4092;
        table[i][0] = (adc >> (ANALOG_REDUCE_BITS));
        table[i][1] = static_cast<int>(t);
#ifdef DEBUG_GENERIC
        Com::printF(Com::tGenTemp,table[i][0]);
        Com::printFLN(Com::tComma,table[i][1]);
#endif
    }
}
#endif


/** \brief Initalizes all extruder.
Updates the pin configuration needed for the extruder and activates extruder 0.
Starts a interrupt based analog input reader, which is used by simple thermistors
for temperature reading.
*/
void Extruder::initExtruder()
{
    uint8_t i;
    Extruder::current = &extruder[0];

#ifdef USE_GENERIC_THERMISTORTABLE_1
    createGenericTable(temptable_generic1,GENERIC_THERM1_MIN_TEMP,GENERIC_THERM1_MAX_TEMP,GENERIC_THERM1_BETA,GENERIC_THERM1_R0,GENERIC_THERM1_T0,GENERIC_THERM1_R1,GENERIC_THERM1_R2);
#endif // USE_GENERIC_THERMISTORTABLE_1

#ifdef USE_GENERIC_THERMISTORTABLE_2
    createGenericTable(temptable_generic2,GENERIC_THERM2_MIN_TEMP,GENERIC_THERM2_MAX_TEMP,GENERIC_THERM2_BETA,GENERIC_THERM2_R0,GENERIC_THERM2_T0,GENERIC_THERM2_R1,GENERIC_THERM2_R2);
#endif // USE_GENERIC_THERMISTORTABLE_2

#ifdef USE_GENERIC_THERMISTORTABLE_3
    createGenericTable(temptable_generic3,GENERIC_THERM3_MIN_TEMP,GENERIC_THERM3_MAX_TEMP,GENERIC_THERM3_BETA,GENERIC_THERM3_R0,GENERIC_THERM3_T0,GENERIC_THERM3_R1,GENERIC_THERM3_R2);
#endif // USE_GENERIC_THERMISTORTABLE_3

#if defined(EXT0_STEP_PIN) && EXT0_STEP_PIN>-1
    SET_OUTPUT(EXT0_DIR_PIN);
    SET_OUTPUT(EXT0_STEP_PIN);
#endif // defined(EXT0_STEP_PIN) && EXT0_STEP_PIN>-1

#if defined(EXT1_STEP_PIN) && EXT1_STEP_PIN>-1 && NUM_EXTRUDER>1
    SET_OUTPUT(EXT1_DIR_PIN);
    SET_OUTPUT(EXT1_STEP_PIN);
#endif // defined(EXT1_STEP_PIN) && EXT1_STEP_PIN>-1 && NUM_EXTRUDER>1

#if defined(EXT2_STEP_PIN) && EXT2_STEP_PIN>-1 && NUM_EXTRUDER>2
    SET_OUTPUT(EXT2_DIR_PIN);
    SET_OUTPUT(EXT2_STEP_PIN);
#endif // defined(EXT2_STEP_PIN) && EXT2_STEP_PIN>-1 && NUM_EXTRUDER>2

#if defined(EXT3_STEP_PIN) && EXT3_STEP_PIN>-1 && NUM_EXTRUDER>3
    SET_OUTPUT(EXT3_DIR_PIN);
    SET_OUTPUT(EXT3_STEP_PIN);
#endif // defined(EXT3_STEP_PIN) && EXT3_STEP_PIN>-1 && NUM_EXTRUDER>3

#if defined(EXT4_STEP_PIN) && EXT4_STEP_PIN>-1 && NUM_EXTRUDER>4
    SET_OUTPUT(EXT4_DIR_PIN);
    SET_OUTPUT(EXT4_STEP_PIN);
#endif // defined(EXT4_STEP_PIN) && EXT4_STEP_PIN>-1 && NUM_EXTRUDER>4

#if defined(EXT5_STEP_PIN) && EXT5_STEP_PIN>-1 && NUM_EXTRUDER>5
    SET_OUTPUT(EXT5_DIR_PIN);
    SET_OUTPUT(EXT5_STEP_PIN);
#endif // defined(EXT5_STEP_PIN) && EXT5_STEP_PIN>-1 && NUM_EXTRUDER>5

    for(i=0; i<NUM_EXTRUDER; ++i)
    {
        Extruder *act = &extruder[i];
        if(act->enablePin > -1)
        {
            HAL::pinMode(act->enablePin,OUTPUT);
            if(!act->enableOn) HAL::digitalWrite(act->enablePin,HIGH);
        }
        act->tempControl.lastTemperatureUpdate = HAL::timeInMilliseconds();
        act->tempControl.updateTempControlVars();
    }

#if HEATED_BED_HEATER_PIN>-1
    SET_OUTPUT(HEATED_BED_HEATER_PIN);
    WRITE(HEATED_BED_HEATER_PIN,HEATER_PINS_INVERTED);
    Extruder::initHeatedBed();
#endif // HEATED_BED_HEATER_PIN>-1

    HAL::analogStart();

} // initExtruder


void TemperatureController::updateTempControlVars()
{
    if(heatManager==1 && pidIGain!=0)   // prevent division by zero
    {
        tempIStateLimitMax = (float)pidDriveMax * PID_CONTROL_DRIVE_MAX_LIMIT_FACTOR / pidIGain;
        tempIStateLimitMin = (float)pidDriveMin * PID_CONTROL_DRIVE_MIN_LIMIT_FACTOR / pidIGain; //Bisher hatte der PID-Regler keinen negativen I-Anteil, weil die Limits nicht ins Negative dürfen. Jetzt schon. Es ist das Minus in der Config, das die Stabilität bringt.
/*
        Com::printF( PSTR( " pidDriveMax:" ), pidDriveMax );
        Com::printF( PSTR( " pidDriveMin:" ), pidDriveMin );
        Com::printF( PSTR( " pidIGain:" ), pidIGain );
        Com::printF( PSTR( " tempIStateLimitMax:" ), tempIStateLimitMax );
        Com::printFLN( PSTR( " tempIStateLimitMin:" ), tempIStateLimitMin );
*/
    }

} // updateTempControlVars


/** \brief Select extruder ext_num.
This function changes and initalizes a new extruder. This is also called, after the eeprom values are changed.
*/
void Extruder::selectExtruderById(uint8_t extruderId)
{
#if FEATURE_MILLING_MODE
    if( Printer::operatingMode == OPERATING_MODE_MILL )
    {
        // in operating mode mill, the extruders are not used
        return;
    }
#endif // FEATURE_MILLING_MODE

    if(extruderId>=NUM_EXTRUDER)
        extruderId = 0;

#if NUM_EXTRUDER>1
    bool executeSelect = false;
    if(extruderId!=Extruder::current->id)
    {
        GCode::executeFString(Extruder::current->deselectCommands);
        executeSelect = true;
    }
#endif // NUM_EXTRUDER>1

#if STEPPER_ON_DELAY
    Extruder::current->enabled = 0;
#endif // STEPPER_ON_DELAY

    Extruder::current->extrudePosition = Printer::queuePositionLastSteps[E_AXIS];
    Extruder::current = &extruder[extruderId];

#ifdef SEPERATE_EXTRUDER_POSITIONS
    // Use seperate extruder positions only if beeing told. Slic3r e.g. creates a continuous extruder position increment
    Printer::queuePositionLastSteps[E_AXIS] = Extruder::current->extrudePosition;
#endif // SEPERATE_EXTRUDER_POSITIONS

    Printer::queuePositionTargetSteps[E_AXIS] = Printer::queuePositionLastSteps[E_AXIS];
    Printer::axisStepsPerMM[E_AXIS] = Extruder::current->stepsPerMM;
    Printer::invAxisStepsPerMM[E_AXIS] = 1.0f/Printer::axisStepsPerMM[E_AXIS];
    Printer::maxFeedrate[E_AXIS] = Extruder::current->maxFeedrate;
    Printer::maxAccelerationMMPerSquareSecond[E_AXIS] = Printer::maxTravelAccelerationMMPerSquareSecond[E_AXIS] = Extruder::current->maxAcceleration;
    Printer::maxTravelAccelerationStepsPerSquareSecond[E_AXIS] = Printer::maxPrintAccelerationStepsPerSquareSecond[E_AXIS] = Printer::maxAccelerationMMPerSquareSecond[E_AXIS] * Printer::axisStepsPerMM[E_AXIS];

#if defined(USE_ADVANCE)
    Printer::minExtruderSpeed = (uint8_t)floor(HAL::maxExtruderTimerFrequency()/(Extruder::current->maxStartFeedrate*Extruder::current->stepsPerMM));
    Printer::maxExtruderSpeed = (uint8_t)floor(HAL::maxExtruderTimerFrequency()/(Extruder::current->maxFeedrate*Extruder::current->stepsPerMM));
    if(Printer::maxExtruderSpeed>15) Printer::maxExtruderSpeed = 15;
    if(Printer::maxExtruderSpeed>=Printer::minExtruderSpeed)
    {
        Printer::maxExtruderSpeed = Printer::minExtruderSpeed;
    }
    else
    {
        float maxdist = Extruder::current->maxFeedrate*Extruder::current->maxFeedrate*0.00013888/Extruder::current->maxAcceleration;
        maxdist-= Extruder::current->maxStartFeedrate*Extruder::current->maxStartFeedrate*0.5/Extruder::current->maxAcceleration;
    }
    float fmax=((float)HAL::maxExtruderTimerFrequency()/((float)Printer::maxExtruderSpeed*Printer::axisStepsPerMM[E_AXIS])); // Limit feedrate to interrupt speed
    if(fmax<Printer::maxFeedrate[E_AXIS]) Printer::maxFeedrate[E_AXIS] = fmax;
#endif // defined(USE_ADVANCE)

    Extruder::current->tempControl.updateTempControlVars();
    Printer::extruderOffset[X_AXIS] = -Extruder::current->xOffset*Printer::invAxisStepsPerMM[X_AXIS];
    Printer::extruderOffset[Y_AXIS] = -Extruder::current->yOffset*Printer::invAxisStepsPerMM[Y_AXIS];
    Printer::extruderOffset[Z_AXIS] = -Extruder::current->zOffset*Printer::invAxisStepsPerMM[Z_AXIS];
    if(Printer::areAxisHomed())
    {
        float oldfeedrate = Printer::feedrate;
        Printer::moveToReal(IGNORE_COORDINATE,IGNORE_COORDINATE,IGNORE_COORDINATE,IGNORE_COORDINATE,Printer::homingFeedrate[X_AXIS]);
        Printer::feedrate = oldfeedrate;
    }
    Printer::updateCurrentPosition();

#if NUM_EXTRUDER>1
    if(executeSelect) // Run only when changing
        GCode::executeFString(Extruder::current->selectCommands);
#endif // NUM_EXTRUDER>1

} // selectExtruderById


void Extruder::setTemperatureForExtruder(float temperatureInCelsius,uint8_t extr,bool beep)
{
    if( extr >= NUM_EXTRUDER )
    {
        // do not set the temperature for an extruder which is not present - this attempt could heat up the extruder without any control and could significantly overheat the extruder
        Com::printFLN( PSTR( "setTemperatureForExtruder(): aborted" ) );
        return;
    }

    bool alloffs = true;
    for(uint8_t i=0; i<NUM_EXTRUDER; i++)
        if(tempController[i]->targetTemperatureC>15) alloffs = false;

#ifdef EXTRUDER_MAX_TEMP
    if(temperatureInCelsius>EXTRUDER_MAX_TEMP) temperatureInCelsius = EXTRUDER_MAX_TEMP;
#endif // EXTRUDER_MAX_TEMP

    if(temperatureInCelsius<0) temperatureInCelsius=0;
    TemperatureController *tc = tempController[extr];
    tc->setTargetTemperature(temperatureInCelsius,0);
    tc->updateTempControlVars();
    if(beep && temperatureInCelsius>30)
        tc->setAlarm(true);
    if(temperatureInCelsius>=EXTRUDER_FAN_COOL_TEMP) extruder[extr].coolerPWM = extruder[extr].coolerSpeed;

    if( Printer::debugInfo() )
    {
        Com::printF(Com::tTargetExtr,extr,0);
        Com::printFLN(Com::tColon,temperatureInCelsius,0);
    }

#if FEATURE_CASE_FAN && !CASE_FAN_ALWAYS_ON
    if( temperatureInCelsius >= CASE_FAN_ON_TEMPERATURE )
    {
        if(Printer::ignoreFanOn){
            //ignore the case fan whenever there is another cooling solution available // Nibbels
            //the fan should still be connected within the rf2000 but might be avoided to suppress noise.
            //the ignore-flag has to be set at runtime to prevent unlearned persons to risk overheat having the wrong startcode.
            //enable and disable the fan with M3120 or M3121 or M3300 P3 S{1,0}
            Printer::prepareFanOff = 0;
            Printer::fanOffDelay = 0;
        }else{
            // enable the case fan in case the extruder is turned on
            Printer::prepareFanOff = 0;
            WRITE(CASE_FAN_PIN, 1);         
        }
    }
    else
    {
        // disable the case fan in case the extruder is turned off
        if( Printer::fanOffDelay )
        {
            // we are going to disable the case fan after the delay
            Printer::prepareFanOff = HAL::timeInMilliseconds();
        }
        else
        {
            // we are going to disable the case fan now
            Printer::prepareFanOff = 0;
            WRITE(CASE_FAN_PIN, 0);
        }
    }
#endif // FEATURE_CASE_FAN && !CASE_FAN_ALWAYS_ON

#if FEATURE_DITTO_PRINTING
    if(Extruder::dittoMode && extr == 0)
    {
        TemperatureController *tc2 = tempController[1];
        tc2->setTargetTemperature(temperatureInCelsius,0);
        tc2->updateTempControlVars();
        if(temperatureInCelsius>=EXTRUDER_FAN_COOL_TEMP) extruder[1].coolerPWM = extruder[1].coolerSpeed;
    }
#endif // FEATURE_DITTO_PRINTING

    bool alloff = true;
    for(uint8_t i=0; i<NUM_EXTRUDER; i++)
        if(tempController[i]->targetTemperatureC>15) alloff = false;

#if EEPROM_MODE != 0
    if(alloff && !alloffs) // All heaters are now switched off?
    {
#if FEATURE_MILLING_MODE
        if( Printer::operatingMode == OPERATING_MODE_PRINT )
        {
            EEPROM::updatePrinterUsage();
        }
#else
        EEPROM::updatePrinterUsage();
#endif // FEATURE_MILLING_MODE
    }
#endif // EEPROM_MODE != 0

    if(alloffs && !alloff) // heaters are turned on, start measuring printing time
    {
        Printer::msecondsPrinting = HAL::timeInMilliseconds();
    }

} // setTemperatureForExtruder


void Extruder::setHeatedBedTemperature(float temperatureInCelsius,bool beep)
{
#if HAVE_HEATED_BED
    float   offset = 0.0;

    if(temperatureInCelsius>HEATED_BED_MAX_TEMP) temperatureInCelsius = HEATED_BED_MAX_TEMP;
    if(temperatureInCelsius<0) temperatureInCelsius = 0;
    if(heatedBedController.targetTemperatureC==temperatureInCelsius) return; // don't flood log with messages if killed

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
    offset = -getHeatBedTemperatureOffset( temperatureInCelsius );

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
    Com::printF( PSTR( "setHeatedBedTemperature(): " ), temperatureInCelsius );
    Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

    heatedBedController.setTargetTemperature(temperatureInCelsius, offset);
    if(beep && temperatureInCelsius>30) heatedBedController.setAlarm(true);

    if( Printer::debugInfo() )
    {
        Com::printFLN(Com::tTargetBedColon,heatedBedController.targetTemperatureC,0);
    }
#endif // HAVE_HEATED_BED

} // setHeatedBedTemperature

float Extruder::getHeatedBedTemperature()
{
#if HAVE_HEATED_BED
    TemperatureController *c = tempController[NUM_TEMPERATURE_LOOPS-1];
    return c->currentTemperatureC;
#else
    return -1;
#endif // HAVE_HEATED_BED

} // getHeatedBedTemperature


void Extruder::disableCurrentExtruderMotor()
{
    if(Extruder::current->enablePin > -1)
        digitalWrite(Extruder::current->enablePin,!Extruder::current->enableOn);

#if FEATURE_DITTO_PRINTING
    if(Extruder::dittoMode)
    {
        if(extruder[1].enablePin > -1)
            digitalWrite(extruder[1].enablePin,!extruder[1].enableOn);
    }
#endif // FEATURE_DITTO_PRINTING

#if STEPPER_ON_DELAY
    Extruder::current->enabled = 0;
#endif // STEPPER_ON_DELAY

    cleanupEPositions();

} // disableCurrentExtruderMotor


void Extruder::disableAllExtruders()
{
    Extruder*   e;
    uint8_t     mode = OPERATING_MODE_PRINT;


#if FEATURE_MILLING_MODE
    if( Printer::operatingMode == OPERATING_MODE_MILL )
    {
        mode = OPERATING_MODE_MILL;
    }
#endif // FEATURE_MILLING_MODE

    if( mode == OPERATING_MODE_PRINT )
    {
        for(uint8_t i=0; i<NUM_EXTRUDER; i++ )
        {
            e = &extruder[i];

            if(e->enablePin > -1)
                digitalWrite(e->enablePin,!e->enableOn);

#if STEPPER_ON_DELAY
            e->enabled = 0;
#endif // STEPPER_ON_DELAY
        }
    }

    cleanupEPositions();

} // disableAllExtruders


#define NUMTEMPS_1 28
// Epcos B57560G0107F000
const short temptable_1[NUMTEMPS_1][2] PROGMEM =
{
    {0,4000},{92,2400},{105,2320},{121,2240},{140,2160},{162,2080},{189,2000},{222,1920},{261,1840},{308,1760},
    {365,1680},{434,1600},{519,1520},{621,1440},{744,1360},{891,1280},{1067,1200},{1272,1120},
    {1771,960},{2357,800},{2943,640},{3429,480},{3760,320},{3869,240},{3912,200},{3948,160},{4077,-160},{4094,-440}
};

// is 200k thermistor
#define NUMTEMPS_2 21
const short temptable_2[NUMTEMPS_2][2] PROGMEM =
{
    {1*4, 848*8},{54*4, 275*8}, {107*4, 228*8}, {160*4, 202*8},{213*4, 185*8}, {266*4, 171*8}, {319*4, 160*8}, {372*4, 150*8},
    {425*4, 141*8}, {478*4, 133*8},{531*4, 125*8},{584*4, 118*8},{637*4, 110*8},{690*4, 103*8},{743*4, 95*8},{796*4, 86*8},
    {849*4, 77*8},{902*4, 65*8},{955*4, 49*8},{1008*4, 17*8},{1020*4, 0*8} //safety
};

// mendel-parts thermistor (EPCOS G550) = NTC mit 100kOhm
#define NUMTEMPS_3 28 

const short temptable_3[NUMTEMPS_3][2] PROGMEM =
{
    {1*4,864*8},{21*4,300*8},{25*4,290*8},{29*4,280*8},{33*4,270*8},{39*4,260*8},{46*4,250*8},{54*4,240*8},{64*4,230*8},{75*4,220*8},
    {90*4,210*8},{107*4,200*8},{128*4,190*8},{154*4,180*8},{184*4,170*8},{221*4,160*8},{265*4,150*8},{316*4,140*8},{375*4,130*8},
    {441*4,120*8},{513*4,110*8},{588*4,100*8},{734*4,80*8},{856*4,60*8},{938*4,40*8},{986*4,20*8},{1008*4,0*8},{1018*4,-20*8}
};

// is 10k thermistor
#define NUMTEMPS_4 20
const short temptable_4[NUMTEMPS_4][2] PROGMEM =
{
    {1*4, 430*8},{54*4, 137*8},{107*4, 107*8},{160*4, 91*8},{213*4, 80*8},{266*4, 71*8},{319*4, 64*8},{372*4, 57*8},{425*4, 51*8},
    {478*4, 46*8},{531*4, 41*8},{584*4, 35*8},{637*4, 30*8},{690*4, 25*8},{743*4, 20*8},{796*4, 14*8},{849*4, 7*8},{902*4, 0*8},
    {955*4, -11*8},{1008*4, -35*8}
};

// ATC Semitec 104GT-2 / E3D Hotend Thermistor
#define NUMTEMPS_8 34 
const short temptable_8[NUMTEMPS_8][2] PROGMEM =
{
    {0,8000},{69,2400},{79,2320},{92,2240},{107,2160},{125,2080},{146,2000},{172,1920},{204,1840},{222,1760},{291,1680},{350,1600},
    {422,1520},{511,1440},{621,1360},{755,1280},{918,1200},{1114,1120},{1344,1040},{1608,960},{1902,880},{2216,800},{2539,720},
    {2851,640},{3137,560},{3385,480},{3588,400},{3746,320},{3863,240},{3945,160},{4002,80},{4038,0},{4061,-80},{4075,-160}
};

// 100k Honeywell 135-104LAG-J01
#define NUMTEMPS_9 67 
const short temptable_9[NUMTEMPS_9][2] PROGMEM =
{
    {1*4, 941*8},{19*4, 362*8},{37*4, 299*8}, //top rating 300C
    {55*4, 266*8},{73*4, 245*8},{91*4, 229*8},{109*4, 216*8},{127*4, 206*8},{145*4, 197*8},{163*4, 190*8},{181*4, 183*8},{199*4, 177*8},
    {217*4, 171*8},{235*4, 166*8},{253*4, 162*8},{271*4, 157*8},{289*4, 153*8},{307*4, 149*8},{325*4, 146*8},{343*4, 142*8},{361*4, 139*8},
    {379*4, 135*8},{397*4, 132*8},{415*4, 129*8},{433*4, 126*8},{451*4, 123*8},{469*4, 121*8},{487*4, 118*8},{505*4, 115*8},{523*4, 112*8},
    {541*4, 110*8},{559*4, 107*8},{577*4, 105*8},{595*4, 102*8},{613*4, 99*8},{631*4, 97*8},{649*4, 94*8},{667*4, 92*8},{685*4, 89*8},
    {703*4, 86*8},{721*4, 84*8},{739*4, 81*8},{757*4, 78*8},{775*4, 75*8},{793*4, 72*8},{811*4, 69*8},{829*4, 66*8},{847*4, 62*8},
    {865*4, 59*8},{883*4, 55*8},{901*4, 51*8},{919*4, 46*8},{937*4, 41*8},
    {955*4, 35*8},{973*4, 27*8},{991*4, 17*8},{1009*4, 1*8},{1023*4, 0}  //to allow internal 0 degrees C
};

// 100k 0603 SMD Vishay NTCS0603E3104FXT (4.7k pullup)
#define NUMTEMPS_10 20 
const short temptable_10[NUMTEMPS_10][2] PROGMEM =
{
    {1*4, 704*8},{54*4, 216*8},{107*4, 175*8},{160*4, 152*8},{213*4, 137*8},{266*4, 125*8},{319*4, 115*8},{372*4, 106*8},{425*4, 99*8},
    {478*4, 91*8},{531*4, 85*8},{584*4, 78*8},{637*4, 71*8},{690*4, 65*8},{743*4, 58*8},{796*4, 50*8},{849*4, 42*8},{902*4, 31*8},
    {955*4, 17*8},{1008*4, 0}
};

// 100k GE Sensing AL03006-58.2K-97-G1 (4.7k pullup)
#define NUMTEMPS_11 31 
const short temptable_11[NUMTEMPS_11][2] PROGMEM =
{
    {1*4, 936*8},{36*4, 300*8},{71*4, 246*8},{106*4, 218*8},{141*4, 199*8},{176*4, 185*8},{211*4, 173*8},{246*4, 163*8},{281*4, 155*8},
    {316*4, 147*8},{351*4, 140*8},{386*4, 134*8},{421*4, 128*8},{456*4, 122*8},{491*4, 117*8},{526*4, 112*8},{561*4, 107*8},{596*4, 102*8},
    {631*4, 97*8},{666*4, 92*8},{701*4, 87*8},{736*4, 81*8},{771*4, 76*8},{806*4, 70*8},{841*4, 63*8},{876*4, 56*8},{911*4, 48*8},
    {946*4, 38*8},{981*4, 23*8},{1005*4, 5*8},{1016*4, 0}
};

// 100k RS thermistor 198-961 (4.7k pullup)
#define NUMTEMPS_12 31 
const short temptable_12[NUMTEMPS_12][2] PROGMEM =
{
    {1*4, 929*8},{36*4, 299*8},{71*4, 246*8},{106*4, 217*8},{141*4, 198*8},{176*4, 184*8},{211*4, 173*8},{246*4, 163*8},{281*4, 154*8},{316*4, 147*8},
    {351*4, 140*8},{386*4, 134*8},{421*4, 128*8},{456*4, 122*8},{491*4, 117*8},{526*4, 112*8},{561*4, 107*8},{596*4, 102*8},{631*4, 97*8},{666*4, 91*8},
    {701*4, 86*8},{736*4, 81*8},{771*4, 76*8},{806*4, 70*8},{841*4, 63*8},{876*4, 56*8},{911*4, 48*8},{946*4, 38*8},{981*4, 23*8},{1005*4, 5*8},{1016*4, 0*8}
};

// PT100 E3D
#define NUMTEMPS_13 19 
const short temptable_13[NUMTEMPS_13][2] PROGMEM =
{
    {0,0},{908,8},{942,10*8},{982,20*8},{1015,8*30},{1048,8*40},{1080,8*50},{1113,8*60},{1146,8*70},{1178,8*80},{1211,8*90},{1276,8*110},{1318,8*120}
    ,{1670,8*230},{2455,8*500},{3445,8*900},{3666,8*1000},{3871,8*1100},{4095,8*2000}
};

// Thermistor NTC 3950 100k Ohm (result seems a bit to cold for my amazon-ntcs)
#define NUMTEMPS_14 46 
const short temptable_14[NUMTEMPS_14][2] PROGMEM = {
    {1*4,8*938}, {31*4,8*314}, {41*4,8*290}, {51*4,8*272}, {61*4,8*258}, {71*4,8*247}, {81*4,8*237}, {91*4,8*229}, {101*4,8*221}, {111*4,8*215}, {121*4,8*209},
    {131*4,8*204}, {141*4,8*199}, {151*4,8*195}, {161*4,8*190}, {171*4,8*187}, {181*4,8*183}, {191*4,8*179}, {201*4,8*176}, {221*4,8*170}, {241*4,8*165}, 
    {261*4,8*160}, {281*4,8*155}, {301*4,8*150}, {331*4,8*144}, {361*4,8*139}, {391*4,8*133}, {421*4,8*128}, {451*4,8*123}, {491*4,8*117}, {531*4,8*111}, 
    {571*4,8*105}, {611*4,8*100}, {681*4,8*90}, {711*4,8*85}, {811*4,8*69}, {831*4,8*65}, {881*4,8*55}, 
    {901*4,8*51},  {941*4,8*39}, {971*4,8*28}, {981*4,8*23}, {991*4,8*17}, {1001*4,8*9}, {1021*4,8*-27},{1023*4,8*-200}
};

// Thermistor NTC 3950 100k Ohm (other source)
#define NUMTEMPS_15 103 
const short temptable_15[NUMTEMPS_15][2] PROGMEM = {
    {1*4,938*8},{11*4,423*8},{21*4,351*8},{31*4,314*8},{41*4,290*8},{51*4,272*8},{61*4,258*8},{71*4,247*8},\
{81*4,237*8},{91*4,229*8},{101*4,221*8},{111*4,215*8},{121*4,209*8},{131*4,204*8},{141*4,199*8},{151*4,195*8},\
{161*4,190*8},{171*4,187*8},{181*4,183*8},{191*4,179*8},{201*4,176*8},{211*4,173*8},{221*4,170*8},{231*4,167*8},\
{241*4,165*8},{251*4,162*8},{261*4,160*8},{271*4,157*8},{281*4,155*8},{291*4,153*8},{301*4,150*8},{311*4,148*8},\
{321*4,146*8},{331*4,144*8},{341*4,142*8},{351*4,140*8},{361*4,139*8},{371*4,137*8},{381*4,135*8},{391*4,133*8},\
{401*4,131*8},{411*4,130*8},{421*4,128*8},{431*4,126*8},{441*4,125*8},{451*4,123*8},{461*4,122*8},{471*4,120*8},\
{481*4,119*8},{491*4,117*8},{501*4,116*8},{511*4,114*8},{521*4,113*8},{531*4,111*8},{541*4,110*8},{551*4,108*8},\
{561*4,107*8},{571*4,105*8},{581*4,104*8},{591*4,102*8},{601*4,101*8},{611*4,100*8},{621*4,98*8},{631*4,97*8},\
{641*4,95*8},{651*4,94*8},{661*4,92*8},{671*4,91*8},{681*4,90*8},{691*4,88*8},{701*4,87*8},{711*4,85*8},{721*4,84*8},\
{731*4,82*8},{741*4,81*8},{751*4,79*8},{761*4,77*8},{771*4,76*8},{781*4,74*8},{791*4,72*8},{801*4,71*8},{811*4,69*8},\
{821*4,67*8},{831*4,65*8},{841*4,63*8},{851*4,62*8},{861*4,60*8},{871*4,57*8},{881*4,55*8},{891*4,53*8},{901*4,51*8},\
{911*4,48*8},{921*4,45*8},{931*4,42*8},{941*4,39*8},{951*4,36*8},{961*4,32*8},{971*4,28*8},{981*4,23*8},{991*4,17*8},\
{1001*4,9*8},{1011*4,-1*8},{1021*4,-26*8}   
};


#if NUM_TEMPS_USERTHERMISTOR0>0
const short temptable_5[NUM_TEMPS_USERTHERMISTOR0][2] PROGMEM = USER_THERMISTORTABLE0 ;
#endif // NUM_TEMPS_USERTHERMISTOR0>0

#if NUM_TEMPS_USERTHERMISTOR1>0
const short temptable_6[NUM_TEMPS_USERTHERMISTOR1][2] PROGMEM = USER_THERMISTORTABLE1 ;
#endif // NUM_TEMPS_USERTHERMISTOR1>0

#if NUM_TEMPS_USERTHERMISTOR2>0
const short temptable_7[NUM_TEMPS_USERTHERMISTOR2][2] PROGMEM = USER_THERMISTORTABLE2 ;
#endif // NUM_TEMPS_USERTHERMISTOR2>0

const short * const temptables[15] PROGMEM = {(short int *)&temptable_1[0][0],(short int *)&temptable_2[0][0],(short int *)&temptable_3[0][0],(short int *)&temptable_4[0][0]
#if NUM_TEMPS_USERTHERMISTOR0>0
        ,(short int *)&temptable_5[0][0]
#else
        ,0
#endif // NUM_TEMPS_USERTHERMISTOR0>0

#if NUM_TEMPS_USERTHERMISTOR1>0
        ,(short int *)&temptable_6[0][0]
#else
        ,0
#endif // NUM_TEMPS_USERTHERMISTOR1>0

#if NUM_TEMPS_USERTHERMISTOR2>0
        ,(short int *)&temptable_7[0][0]
#else
        ,0
#endif // NUM_TEMPS_USERTHERMISTOR2>0

        ,(short int *)&temptable_8[0][0]
        ,(short int *)&temptable_9[0][0]
        ,(short int *)&temptable_10[0][0]
        ,(short int *)&temptable_11[0][0]
        ,(short int *)&temptable_12[0][0]
        ,(short int *)&temptable_13[0][0]
        ,(short int *)&temptable_14[0][0]
        ,(short int *)&temptable_15[0][0]
                                             };
const uint8_t temptables_num[15] PROGMEM = {NUMTEMPS_1,NUMTEMPS_2,NUMTEMPS_3,NUMTEMPS_4,NUM_TEMPS_USERTHERMISTOR0,NUM_TEMPS_USERTHERMISTOR1,NUM_TEMPS_USERTHERMISTOR2,NUMTEMPS_8,
                                 NUMTEMPS_9,NUMTEMPS_10,NUMTEMPS_11,NUMTEMPS_12,NUMTEMPS_13,NUMTEMPS_14,NUMTEMPS_15
                                           };


void TemperatureController::updateCurrentTemperature()
{
    uint8_t type = sensorType;


    // get raw temperature
    switch(type)
    {
#if ANALOG_INPUTS>0
        case 1: // Epcos B57560G0107F000
        case 2: // is 200k thermistor
        case 3: // V2 Sensor Conrad Renkforce / mendel-parts thermistor (EPCOS G550) = NTC mit 100kOhm
        case 4: // is 10k thermistor
        case 5: // user thermistor 0
        case 6: // user thermistor 1
        case 7: // user thermistor 2
        case 8: // E3D Thermistor
        case 9: // 100k Honeywell 135-104LAG-J01
        case 10: // 100k 0603 SMD Vishay NTCS0603E3104FXT (4.7k pullup)
        case 11: // 100k GE Sensing AL03006-58.2K-97-G1 (4.7k pullup)
        case 12: // 100k RS thermistor 198-961 (4.7k pullup)
        //case 13 weiter unten, E3D PT100.
        case 14: // Thermistor NTC 3950 100k Ohm
        case 15: // Thermistor NTC 3950 100k Ohm
        case 97: // Define Raw Thermistor and Restistor-Settings within configuration.h see USE_GENERIC_THERMISTORTABLE_1 and GENERIC_THERM_NUM_ENTRIES 
        case 98: // Define Raw Thermistor and Restistor-Settings within configuration.h see USE_GENERIC_THERMISTORTABLE_2 and GENERIC_THERM_NUM_ENTRIES 
        case 99: // Define Raw Thermistor and Restistor-Settings within configuration.h see USE_GENERIC_THERMISTORTABLE_3 and GENERIC_THERM_NUM_ENTRIES 
        {
            currentTemperature = (1023<<(2-ANALOG_REDUCE_BITS))-(osAnalogInputValues[sensorPin]>>(ANALOG_REDUCE_BITS)); // Convert to 10 bit result
            break;
        }
        case 13: // PT100 E3D
        case 50: // User defined PTC table
        case 51:
        case 52:
        case 60: // HEATER_USES_AD8495 (Delivers 5mV/degC)
        case 100: // AD595
        {
            currentTemperature = (osAnalogInputValues[sensorPin]>>(ANALOG_REDUCE_BITS));
            break;
        }
#endif // ANALOG_INPUTS>0
    
        default:
        {
            currentTemperature = 4095; // unknown method, return high value to switch heater off for safety
            break;
        }
    }

    switch(type)
    {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5: //user thermistor 0
        case 6: //user thermistor 1
        case 7: //user thermistor 2
        case 8: //E3D Thermistor
        case 9:
        case 10:
        case 11:
        case 12:
        case 14: // Thermistor NTC 3950 100k Ohm
        case 15: // Thermistor NTC 3950 100k Ohm
        {
            type--;
            uint8_t num = pgm_read_byte(&temptables_num[type])<<1;
            uint8_t i=2;
            const short *temptable = (const short *)pgm_read_word(&temptables[type]); //pgm_read_word_near(&temptables[type]);
            short oldraw = pgm_read_word(&temptable[0]);
            short oldtemp = pgm_read_word(&temptable[1]);
            short newtemp = 0;
            int temp = (1023<<(2-ANALOG_REDUCE_BITS))-currentTemperature;


            while(i<num)
            {
                short newraw = pgm_read_word(&temptable[i++]);
                newtemp = pgm_read_word(&temptable[i++]);
                if (newraw > temp)
                {
                    //OUT_P_I("RC O:",oldtemp);OUT_P_I_LN(" OR:",oldraw);
                    //OUT_P_I("RC N:",newtemp);OUT_P_I_LN(" NR:",newraw);
                    currentTemperatureC = TEMP_INT_TO_FLOAT(oldtemp + (float)(temp-oldraw)*(float)(newtemp-oldtemp)/(newraw-oldraw));

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
                    float   offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
                    if( targetTemperatureC > 50 )
                    {
                        Com::printF( PSTR( "updateCurrentTemperature().1: " ), currentTemperatureC );
                        Com::printF( PSTR( ", " ), offset );
                        Com::printF( PSTR( ", " ), currentTemperature );
                        Com::printFLN( PSTR( ", " ), targetTemperature );
                    }
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

                    currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }
            // Overflow: Set to last value in the table
            currentTemperatureC = TEMP_INT_TO_FLOAT(newtemp);

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            float offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printF( PSTR( "updateCurrentTemperature().2: " ), currentTemperatureC );
            Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

            currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

            break;
        }
        case 13:
        case 50: // User defined PTC thermistor
        case 51:
        case 52:
        {
            type-=46;
            uint8_t num = pgm_read_byte(&temptables_num[type])<<1;
            uint8_t i=2;
            const short *temptable = (const short *)pgm_read_word(&temptables[type]); //pgm_read_word_near(&temptables[type]);
            short oldraw = pgm_read_word(&temptable[0]);
            short oldtemp = pgm_read_word(&temptable[1]);
            short newtemp = 0;


            while(i<num)
            {
                short newraw = pgm_read_word(&temptable[i++]);
                newtemp = pgm_read_word(&temptable[i++]);
                if (newraw > currentTemperature)
                {
                    currentTemperatureC = TEMP_INT_TO_FLOAT(oldtemp + (float)(currentTemperature-oldraw)*(float)(newtemp-oldtemp)/(newraw-oldraw));

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
                    float offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
                    Com::printF( PSTR( "updateCurrentTemperature().3: " ), currentTemperatureC );
                    Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

                    currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }
            // Overflow: Set to last value in the table
            currentTemperatureC = TEMP_INT_TO_FLOAT(newtemp);

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            float offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printF( PSTR( "updateCurrentTemperature().4: " ), currentTemperatureC );
            Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

            currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
            break;
        }
        case 60: // AD8495 (Delivers 5mV/degC vs the AD595's 10mV)
        {
            currentTemperatureC = ((float)currentTemperature * 1000.0f/(1024<<(2-ANALOG_REDUCE_BITS)));

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            float   offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printF( PSTR( "updateCurrentTemperature().5: " ), currentTemperatureC );
            Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

            currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
            break;
        }
        case 100: // AD595
        {
            currentTemperatureC = ((float)currentTemperature * 500.0f/(1024<<(2-ANALOG_REDUCE_BITS)));

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            float   offset = getHeatBedTemperatureOffset( currentTemperatureC );


#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printF( PSTR( "updateCurrentTemperature().6: " ), currentTemperatureC );
            Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

            currentTemperatureC += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
            break;
        }

#if defined(USE_GENERIC_THERMISTORTABLE_1) || defined(USE_GENERIC_THERMISTORTABLE_2) || defined(USE_GENERIC_THERMISTORTABLE_3)
        case 97:
        case 98:
        case 99:
        {
            uint8_t i=2;
            const short *temptable;

#ifdef USE_GENERIC_THERMISTORTABLE_1
            if(type == 97)
                temptable = (const short *)temptable_generic1;
#endif // USE_GENERIC_THERMISTORTABLE_1

#ifdef USE_GENERIC_THERMISTORTABLE_2
            if(type == 98)
                temptable = (const short *)temptable_generic2;
#endif // USE_GENERIC_THERMISTORTABLE_2

#ifdef USE_GENERIC_THERMISTORTABLE_3
            if(type == 99)
                temptable = (const short *)temptable_generic3;
#endif // USE_GENERIC_THERMISTORTABLE_3

            short oldraw = temptable[0];
            short oldtemp = temptable[1];
            short newraw,newtemp;


            currentTemperature = (1023<<(2-ANALOG_REDUCE_BITS))-currentTemperature;
            while(i<GENERIC_THERM_NUM_ENTRIES*2)
            {
                newraw = temptable[i++];
                newtemp = temptable[i++];
                if (newraw > currentTemperature)
                {
                    //OUT_P_I("RC O:",oldtemp);OUT_P_I_LN(" OR:",oldraw);
                    //OUT_P_I("RC N:",newtemp);OUT_P_I_LN(" NR:",newraw);
                    currentTemperatureC = TEMP_INT_TO_FLOAT(oldtemp + (float)(currentTemperature-oldraw)*(float)(newtemp-oldtemp)/(newraw-oldraw));

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
                    currentTemperatureC += getHeatBedTemperatureOffset( currentTemperatureC );
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }
            // Overflow: Set to last value in the table
            currentTemperatureC = TEMP_INT_TO_FLOAT(newtemp);

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            currentTemperatureC += getHeatBedTemperatureOffset( currentTemperatureC );
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
            break;
        }
#endif // defined(USE_GENERIC_THERMISTORTABLE_1) || defined(USE_GENERIC_THERMISTORTABLE_2) || defined(USE_GENERIC_THERMISTORTABLE_3)
    }

} // updateCurrentTemperature


void TemperatureController::setTargetTemperature(float target, float offset)
{
    targetTemperatureC = target;

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
#if DEBUG_HEAT_BED_TEMP_COMPENSATION
    Com::printF( PSTR( "setTargetTemperature(): " ), target );
    Com::printFLN( PSTR( ", " ), offset );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

    offsetC =  offset;
    target  += offset;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

    int temp     = TEMP_FLOAT_TO_INT(target);
    uint8_t type = sensorType;

    switch(sensorType)
    {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 14: // Thermistor NTC 3950 100k Ohm
        case 15: // Thermistor NTC 3950 100k Ohm
        {
            type--;
            uint8_t num = pgm_read_byte(&temptables_num[type])<<1;
            uint8_t i=2;
            const short *temptable = (const short *)pgm_read_word(&temptables[type]); //pgm_read_word(&temptables[type]);
            short oldraw = pgm_read_word(&temptable[0]);
            short oldtemp = pgm_read_word(&temptable[1]);
            short newraw = 0;

            while(i<num)
            {
                newraw = pgm_read_word(&temptable[i++]);
                short newtemp = pgm_read_word(&temptable[i++]);
                if (newtemp < temp)
                {
                    targetTemperature = (1023<<(2-ANALOG_REDUCE_BITS))- oldraw + (int32_t)(oldtemp-temp)*(int32_t)(oldraw-newraw)/(oldtemp-newtemp);

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
                    Com::printFLN( PSTR( "setTargetTemperature().1: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION

                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }

            // Overflow: Set to last value in the table
            targetTemperature = (1023<<(2-ANALOG_REDUCE_BITS))-newraw;

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printFLN( PSTR( "setTargetTemperature().2: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
            break;
        }
        case 13: // PT100 E3D
        case 50: // user defined PTC thermistor
        case 51:
        case 52:
        {
            type-=46;
            uint8_t num = pgm_read_byte(&temptables_num[type])<<1;
            uint8_t i=2;
            const short *temptable = (const short *)pgm_read_word(&temptables[type]); //pgm_read_word(&temptables[type]);
            short oldraw = pgm_read_word(&temptable[0]);
            short oldtemp = pgm_read_word(&temptable[1]);
            short newraw = 0;

            while(i<num)
            {
                newraw = pgm_read_word(&temptable[i++]);
                short newtemp = pgm_read_word(&temptable[i++]);
                if (newtemp > temp)
                {
                    targetTemperature = oldraw + (int32_t)(oldtemp-temp)*(int32_t)(oldraw-newraw)/(oldtemp-newtemp);

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
                    Com::printFLN( PSTR( "setTargetTemperature().3: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }

            // Overflow: Set to last value in the table
            targetTemperature = newraw;

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printFLN( PSTR( "setTargetTemperature().4: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
            break;
        }
        case 60: // HEATER_USES_AD8495 (Delivers 5mV/degC)
        {
            targetTemperature = (int)((int32_t)temp * (1024<<(2-ANALOG_REDUCE_BITS))/ 1000);

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printFLN( PSTR( "setTargetTemperature().5: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
            break;
        }
        case 100: // HEATER_USES_AD595
        {
            targetTemperature = (int)((int32_t)temp * (1024<<(2-ANALOG_REDUCE_BITS))/ 500);

#if DEBUG_HEAT_BED_TEMP_COMPENSATION
            Com::printFLN( PSTR( "setTargetTemperature().6: " ), targetTemperature );
#endif // DEBUG_HEAT_BED_TEMP_COMPENSATION
            break;
        }
#if defined(USE_GENERIC_THERMISTORTABLE_1) || defined(USE_GENERIC_THERMISTORTABLE_2) || defined(USE_GENERIC_THERMISTORTABLE_3)
        case 97:
        case 98:
        case 99:
        {
            uint8_t i=2;
            const short *temptable;

#ifdef USE_GENERIC_THERMISTORTABLE_1
            if(type == 97)
                temptable = (const short *)temptable_generic1;
#endif // USE_GENERIC_THERMISTORTABLE_1

#ifdef USE_GENERIC_THERMISTORTABLE_2
            if(type == 98)
                temptable = (const short *)temptable_generic2;
#endif // USE_GENERIC_THERMISTORTABLE_2

#ifdef USE_GENERIC_THERMISTORTABLE_3
            if(type == 99)
                temptable = (const short *)temptable_generic3;
#endif // USE_GENERIC_THERMISTORTABLE_3

            short oldraw = temptable[0];
            short oldtemp = temptable[1];
            short newraw = 0;
            while(i<GENERIC_THERM_NUM_ENTRIES*2)
            {
                newraw = temptable[i++];
                short newtemp = temptable[i++];
                if (newtemp < temp)
                {
                    targetTemperature = (1023<<(2-ANALOG_REDUCE_BITS))- oldraw + (int32_t)(oldtemp-temp)*(int32_t)(oldraw-newraw)/(oldtemp-newtemp);

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
                    currentTemperatureC += getHeatBedTemperatureOffset( currentTemperatureC );
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
                    return;
                }
                oldtemp = newtemp;
                oldraw = newraw;
            }
            // Overflow: Set to last value in the table
            targetTemperature = (1023<<(2-ANALOG_REDUCE_BITS))-newraw;

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            currentTemperatureC += getHeatBedTemperatureOffset( currentTemperatureC );
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
            break;
        }
#endif
    }
    (void)offset;
} // setTargetTemperature


uint8_t autotuneIndex = 255;
void Extruder::disableAllHeater()
{
    for(uint8_t i=0; i<NUM_TEMPERATURE_LOOPS; i++)
    {
        TemperatureController *c = tempController[i];
        c->targetTemperature = 0;
        c->targetTemperatureC = 0;

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
        c->offsetC = 0;
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

        pwm_pos[c->pwmIndex] = 0;
    }
    autotuneIndex = 255;

} // disableAllHeater

void TemperatureController::autotunePID(float temp, uint8_t controllerId, int maxCycles, bool storeValues, int method)
{
    float currentTemp;
    int cycles=0;
    bool heating = true;

    uint32_t temp_millis = HAL::timeInMilliseconds();
    uint32_t t1=temp_millis;
    uint32_t t2=temp_millis;
    int32_t t_high = 0;
    int32_t t_low = 0;

    int32_t bias=pidMax>>1;
    int32_t d = pidMax>>1;
    float Ku, Tu;
    float Kp = 0, Ki = 0, Kd = 0;
    float maxTemp=20, minTemp=20; 
    if(maxCycles < 5)
        maxCycles = 5;
    if(maxCycles > 20)
        maxCycles = 20;
    Com::printInfoFLN(Com::tPIDAutotuneStart);
    Com::printF( PSTR("Ruleset: "), (int)method );
    switch(method){
        case 6: //P
            Com::printFLN(Com::tAPIDsimpleP);
        break;
        case 5: //PI
            Com::printFLN(Com::tAPIDsimplePI);
        break;
        case 4: //PD
            Com::printFLN(Com::tAPIDsimplePD);
        break;
        case 3: //PID no overshoot
            Com::printFLN(Com::tAPIDNone);
        break;
        case 2: //PID some overshoot
            Com::printFLN(Com::tAPIDSome);
        break;
        case 1: //PID Pessen Integral Rule
            Com::printFLN(Com::tAPIDPessen);
        break;
        default: //PID classic Ziegler-Nichols
            Com::printFLN(Com::tAPIDClassic);
            method = 0; //filter not available methods.
    }
    //Extruder::disableAllHeater(); // switch off all heaters. https://github.com/repetier/Repetier-Firmware/commit/241c550ac004023842d6886c6e0db15a1f6b56d7
    autotuneIndex = controllerId;
    pwm_pos[pwmIndex] = pidMax;
    if(controllerId<NUM_EXTRUDER)
    {
        extruder[controllerId].coolerPWM = extruder[controllerId].coolerSpeed;
        extruder[0].coolerPWM = extruder[0].coolerSpeed;
    }

    for(;;)
    {
        Commands::checkForPeriodicalActions(); // update heaters etc. https://github.com/repetier/Repetier-Firmware/commit/241c550ac004023842d6886c6e0db15a1f6b56d7
        GCode::keepAlive( WaitHeater );
        updateCurrentTemperature();
        currentTemp = currentTemperatureC;

        millis_t time = HAL::timeInMilliseconds();
        maxTemp=RMath::max(maxTemp,currentTemp);
        minTemp=RMath::min(minTemp,currentTemp);
        if(heating == true && currentTemp > temp)   // switch heating -> off
        {
            if(time - t2 > (controllerId<NUM_EXTRUDER ? 2500 : 1500))
            {
                heating = false;
                pwm_pos[pwmIndex] = (bias - d);
                t1 = time;
                t_high = t1 - t2;
                maxTemp=temp;
            }
        }
        if(heating == false && currentTemp < temp)
        {
            if(time - t1 > (controllerId<NUM_EXTRUDER ? 5000 : 3000))
            {
                heating = true;
                t2 = time;
                t_low = t2 - t1; // half wave length
                if(cycles > 0)
                {
                    bias += (d*(t_high - t_low))/(t_low + t_high);
                    bias = constrain(bias, 20 ,pidMax - 20);
                    if(bias > pidMax/2) d = pidMax - 1 - bias;
                    else d = bias;

                    Com::printF(Com::tAPIDBias,bias);
                    Com::printF(Com::tAPIDD,d);
                    Com::printF(Com::tAPIDMin,minTemp);
                    Com::printFLN(Com::tAPIDMax,maxTemp);
                    if(cycles > 2)
                    {
                        // Parameter according Ziegler¡§CNichols method: http://en.wikipedia.org/wiki/Ziegler%E2%80%93Nichols_method
                        Ku = (4.0 * d)/(3.14159 * (maxTemp - minTemp));
                        Tu = static_cast<float>(t_low + t_high) / 1000.0;
                        Com::printF(Com::tAPIDKu,Ku);
                        Com::printFLN(Com::tAPIDTu,Tu);
/**
https://en.wikipedia.org/wiki/Ziegler%E2%80%93Nichols_method
KP = KP
KI = KP / Ti
KD = KP * Td
wegen Formel u(t) = KP*( rest )

Ti = Tu / Maßzahl lt. Tabelle
Td = Tu / Maßzahl lt. Tabelle
KP = Ku * Maßzahl lt. Tabelle

see also: http://www.mstarlabs.com/control/znrule.html
*/
                        switch(method){
                            case 6: //P
                               Kp = 0.5f*Ku;          //0.5 KRkrit
                               Ki = 0;
                               Kd = 0;
                               Com::printFLN(Com::tAPIDsimpleP);
                            break;
                            case 5: //PI
                               Kp = 0.45f*Ku;          //0.45 KRkrit
                               Ki = 1.2f*Kp/Tu;       //0.833 Tkrit
                               Kd = 0;
                               Com::printFLN(Com::tAPIDsimplePI);
                            break;
                            case 4: //PD
                               Kp = 0.8f*Ku;          //0.8 KRkrit
                               Ki = 0;
                               Kd = Kp*Tu/8.0f;       //0.125 Tkrit
                               Com::printFLN(Com::tAPIDsimplePD);
                            break;
                            case 3: //PID no overshoot
                               Kp = 0.2f*Ku;          //0.2 KRkrit
                               Ki = 2.0f*Kp/Tu;       //0.5 Tkrit
                               Kd = Kp*Tu/3.0f;       //0.333 Tkrit
                               Com::printFLN(Com::tAPIDNone);
                            break;
                            case 2: //PID some overshoot
                               Kp = 0.33f*Ku;         //0.33 KRkrit
                               Ki = 2.0f*Kp/Tu;       //0.5 Tkrit
                               Kd = Kp*Tu/3.0f;       //0.333 Tkrit
                               Com::printFLN(Com::tAPIDSome);
                            break;
                            case 1: //PID Pessen Integral Rule
                               Kp = 0.7f*Ku;          //0.7 KRkrit
                               Ki = 2.5f*Kp/Tu;       //0.4 Tkrit
                               Kd = Kp*Tu*3.0f/20.0f; //0.15 Tkrit
                               Com::printFLN(Com::tAPIDPessen);
                            break;
                            default: //PID classic Ziegler-Nichols
                               Kp = 0.6f*Ku;          //0.6 KRkrit
                               Ki = 2.0f*Kp/Tu;       //0.5 Tkrit
                               Kd = Kp*Tu/8.0f;       //0.125 Tkrit
                               Com::printFLN(Com::tAPIDClassic);
                        }
                        Com::printFLN(Com::tAPIDKp,Kp);
                        Com::printFLN(Com::tAPIDKi,Ki);
                        Com::printFLN(Com::tAPIDKd,Kd);
                    }
                }
                pwm_pos[pwmIndex] = (bias + d);
                cycles++;
                minTemp=temp;
            }
        }
        if(currentTemp > (temp + 40))
        {
            Com::printErrorFLN(Com::tAPIDFailedHigh);
            showError( (void*)ui_text_autodetect_pid, (void*)ui_text_temperature_wrong );
            //Extruder::disableAllHeater();
            autotuneIndex = 255;
            return;
        }

        Commands::printTemperatures();
        
        if(((time - t1) + (time - t2)) > (10L*60L*1000L*2L))   // 20 Minutes
        {
            Com::printErrorFLN(Com::tAPIDFailedTimeout);
            showError( (void*)ui_text_autodetect_pid, (void*)ui_text_timeout );
            //Extruder::disableAllHeater();
            autotuneIndex = 255;
            return;
        }
        if(cycles > maxCycles)
        {
            Com::printInfoFLN(Com::tAPIDFinished);
            UI_STATUS_UPD( UI_TEXT_AUTODETECT_PID_DONE );
            //Extruder::disableAllHeater();
            autotuneIndex = 255;
            if(storeValues)
            {
                pidPGain = Kp;
                pidIGain = Ki;
                pidDGain = Kd;
                heatManager = 1; //HTR_PID / activate PID MANAGER
                EEPROM::storeDataIntoEEPROM();
            }
            return;
        }
        UI_MEDIUM;
        UI_SLOW;
    }
} // autotunePID

bool reportTempsensorError()
{
    if(!Printer::isAnyTempsensorDefect()) return false;

    for(uint8_t i=0; i<NUM_TEMPERATURE_LOOPS; i++)
    {
        int temp = tempController[i]->currentTemperatureC;

        if( Printer::debugInfo() )
        {
            if(i==NUM_EXTRUDER) Com::printF(Com::tHeatedBed);
            else Com::printF(Com::tExtruderSpace,i);
        }

        if(temp<MIN_DEFECT_TEMPERATURE || temp>MAX_DEFECT_TEMPERATURE)
        {
            if( Printer::debugErrors() )
            {
                Com::printFLN(Com::tTempSensorDefect);
            }
        }
        else if( Printer::debugInfo() )
        {
            Com::printFLN(Com::tTempSensorWorking);
        }
    }

#if FEATURE_MILLING_MODE
    if( Printer::operatingMode == OPERATING_MODE_PRINT )
    {
        if( Printer::debugErrors() )
        {
            Com::printErrorFLN(Com::tDryModeUntilRestart);
        }
    }
#else
    if( Printer::debugErrors() )
    {
        Com::printErrorFLN(Com::tDryModeUntilRestart);
    }
#endif // FEATURE_MILLING_MODE

    return true;

} // reportTempsensorError


Extruder *Extruder::current;

#if NUM_EXTRUDER>0
const char ext0_select_cmd[] PROGMEM = EXT0_SELECT_COMMANDS;
const char ext0_deselect_cmd[] PROGMEM = EXT0_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>0

#if NUM_EXTRUDER>1
const char ext1_select_cmd[] PROGMEM = EXT1_SELECT_COMMANDS;
const char ext1_deselect_cmd[] PROGMEM = EXT1_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>1

#if NUM_EXTRUDER>2
const char ext2_select_cmd[] PROGMEM = EXT2_SELECT_COMMANDS;
const char ext2_deselect_cmd[] PROGMEM = EXT2_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>2

#if NUM_EXTRUDER>3
const char ext3_select_cmd[] PROGMEM = EXT3_SELECT_COMMANDS;
const char ext3_deselect_cmd[] PROGMEM = EXT3_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>3

#if NUM_EXTRUDER>4
const char ext4_select_cmd[] PROGMEM = EXT4_SELECT_COMMANDS;
const char ext4_deselect_cmd[] PROGMEM = EXT4_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>4

#if NUM_EXTRUDER>5
const char ext5_select_cmd[] PROGMEM = EXT5_SELECT_COMMANDS;
const char ext5_deselect_cmd[] PROGMEM = EXT5_DESELECT_COMMANDS;
#endif // NUM_EXTRUDER>5


Extruder extruder[NUM_EXTRUDER] =
{
#if NUM_EXTRUDER>0
    {
        0,EXT0_X_OFFSET,EXT0_Y_OFFSET,EXT0_Z_OFFSET,EXT0_STEPS_PER_MM,EXT0_ENABLE_PIN,EXT0_ENABLE_ON,
        EXT0_MAX_FEEDRATE,EXT0_MAX_ACCELERATION,EXT0_MAX_START_FEEDRATE,0,EXT0_WATCHPERIOD
        ,EXT0_WAIT_RETRACT_TEMP,EXT0_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT0_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT0_ADVANCE_L,EXT0_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            0,EXT0_TEMPSENSOR_TYPE,EXT0_SENSOR_INDEX,0,0,0,0,

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            0,
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

            0,EXT0_HEAT_MANAGER
            ,0,EXT0_PID_INTEGRAL_DRIVE_MAX,EXT0_PID_INTEGRAL_DRIVE_MIN,EXT0_PID_P,EXT0_PID_I,EXT0_PID_D,EXT0_PID_MAX,0,0,0,{0,0,0,0}
        ,0}
        ,ext0_select_cmd,ext0_deselect_cmd,EXT0_EXTRUDER_COOLER_SPEED,0
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>0

#if NUM_EXTRUDER>1
    ,{
        1,EXT1_X_OFFSET,EXT1_Y_OFFSET,EXT1_Z_OFFSET,EXT1_STEPS_PER_MM,EXT1_ENABLE_PIN,EXT1_ENABLE_ON,
        EXT1_MAX_FEEDRATE,EXT1_MAX_ACCELERATION,EXT1_MAX_START_FEEDRATE,0,EXT1_WATCHPERIOD
        ,EXT1_WAIT_RETRACT_TEMP,EXT1_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT1_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT1_ADVANCE_L,EXT1_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            1,EXT1_TEMPSENSOR_TYPE,EXT1_SENSOR_INDEX,0,0,0,0,

#if FEATURE_HEAT_BED_TEMP_COMPENSATION
            0,
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION

            0,EXT1_HEAT_MANAGER
            ,0,EXT1_PID_INTEGRAL_DRIVE_MAX,EXT1_PID_INTEGRAL_DRIVE_MIN,EXT1_PID_P,EXT1_PID_I,EXT1_PID_D,EXT1_PID_MAX,0,0,0,{0,0,0,0}
        ,0}
        ,ext1_select_cmd,ext1_deselect_cmd,EXT1_EXTRUDER_COOLER_SPEED,0
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>1

#if NUM_EXTRUDER>2
    ,{
        2,EXT2_X_OFFSET,EXT2_Y_OFFSET,EXT2_Z_OFFSET,EXT2_STEPS_PER_MM,EXT2_ENABLE_PIN,EXT2_ENABLE_ON,
        EXT2_MAX_FEEDRATE,EXT2_MAX_ACCELERATION,EXT2_MAX_START_FEEDRATE,0,EXT2_WATCHPERIOD
        ,EXT2_WAIT_RETRACT_TEMP,EXT2_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT2_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT2_ADVANCE_L,EXT2_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            2,EXT2_TEMPSENSOR_TYPE,EXT2_SENSOR_INDEX,0,0,0,0,0,EXT2_HEAT_MANAGER
            ,0,EXT2_PID_INTEGRAL_DRIVE_MAX,EXT2_PID_INTEGRAL_DRIVE_MIN,EXT2_PID_P,EXT2_PID_I,EXT2_PID_D,EXT2_PID_MAX,0,0,0,{0,0,0,0}
        ,0}
        ,ext2_select_cmd,ext2_deselect_cmd,EXT2_EXTRUDER_COOLER_SPEED,0     
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>2

#if NUM_EXTRUDER>3
    ,{
        3,EXT3_X_OFFSET,EXT3_Y_OFFSET,EXT3_Z_OFFSET,EXT3_STEPS_PER_MM,EXT3_ENABLE_PIN,EXT3_ENABLE_ON,
        EXT3_MAX_FEEDRATE,EXT3_MAX_ACCELERATION,EXT3_MAX_START_FEEDRATE,0,EXT3_WATCHPERIOD
        ,EXT3_WAIT_RETRACT_TEMP,EXT3_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT3_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT3_ADVANCE_L,EXT3_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            3,EXT3_TEMPSENSOR_TYPE,EXT3_SENSOR_INDEX,0,0,0,0,0,EXT3_HEAT_MANAGER
            ,0,EXT3_PID_INTEGRAL_DRIVE_MAX,EXT3_PID_INTEGRAL_DRIVE_MIN,EXT3_PID_P,EXT3_PID_I,EXT3_PID_D,EXT3_PID_MAX,0,0,0,{0,0,0,0}

        ,0}
        ,ext3_select_cmd,ext3_deselect_cmd,EXT3_EXTRUDER_COOLER_SPEED,0
        
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>3

#if NUM_EXTRUDER>4
    ,{
        4,EXT4_X_OFFSET,EXT4_Y_OFFSET,EXT4_Z_OFFSET,EXT4_STEPS_PER_MM,EXT4_ENABLE_PIN,EXT4_ENABLE_ON,
        EXT4_MAX_FEEDRATE,EXT4_MAX_ACCELERATION,EXT4_MAX_START_FEEDRATE,0,EXT4_WATCHPERIOD
        ,EXT4_WAIT_RETRACT_TEMP,EXT4_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT4_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT4_ADVANCE_L,EXT4_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            4,EXT4_TEMPSENSOR_TYPE,EXT4_SENSOR_INDEX,0,0,0,0,0,EXT4_HEAT_MANAGER
            ,0,EXT4_PID_INTEGRAL_DRIVE_MAX,EXT4_PID_INTEGRAL_DRIVE_MIN,EXT4_PID_P,EXT4_PID_I,EXT4_PID_D,EXT4_PID_MAX,0,0,0,{0,0,0,0}
        ,0}
        ,ext4_select_cmd,ext4_deselect_cmd,EXT4_EXTRUDER_COOLER_SPEED,0
        
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>4

#if NUM_EXTRUDER>5
    ,{
        5,EXT5_X_OFFSET,EXT5_Y_OFFSET,EXT5_Z_OFFSET,EXT5_STEPS_PER_MM,EXT5_ENABLE_PIN,EXT5_ENABLE_ON,
        EXT5_MAX_FEEDRATE,EXT5_MAX_ACCELERATION,EXT5_MAX_START_FEEDRATE,0,EXT5_WATCHPERIOD
        ,EXT5_WAIT_RETRACT_TEMP,EXT5_WAIT_RETRACT_UNITS,0

#ifdef USE_ADVANCE
#ifdef ENABLE_QUADRATIC_ADVANCE
        ,EXT5_ADVANCE_K
#endif // ENABLE_QUADRATIC_ADVANCE
        ,EXT5_ADVANCE_L,EXT5_ADVANCE_BACKLASH_STEPS
#endif // USE_ADVANCE

        ,{
            5,EXT5_TEMPSENSOR_TYPE,EXT5_SENSOR_INDEX,0,0,0,0,0,EXT5_HEAT_MANAGER
            ,0,EXT5_PID_INTEGRAL_DRIVE_MAX,EXT5_PID_INTEGRAL_DRIVE_MIN,EXT5_PID_P,EXT5_PID_I,EXT5_PID_D,EXT5_PID_MAX,0,0,0,{0,0,0,0}
        ,0}
        ,ext5_select_cmd,ext5_deselect_cmd,EXT5_EXTRUDER_COOLER_SPEED,0
        
        #if STEPPER_ON_DELAY
        , '\x0'
        #endif // STEPPER_ON_DELAY by Nibbels gegen xtruder.cpp:1620:1: warning: missing initializer for member 'Extruder::enabled'
    }
#endif // NUM_EXTRUDER>5
};

#if HAVE_HEATED_BED
#define NUM_TEMPERATURE_LOOPS NUM_EXTRUDER+1
TemperatureController heatedBedController = {NUM_EXTRUDER,HEATED_BED_SENSOR_TYPE,BED_SENSOR_INDEX,0,0,0,0,
    #if FEATURE_HEAT_BED_TEMP_COMPENSATION
        0,
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
        0,HEATED_BED_HEAT_MANAGER
        ,0,HEATED_BED_PID_INTEGRAL_DRIVE_MAX,HEATED_BED_PID_INTEGRAL_DRIVE_MIN,HEATED_BED_PID_PGAIN,HEATED_BED_PID_IGAIN,HEATED_BED_PID_DGAIN,HEATED_BED_PID_MAX,0,0,0,{0,0,0,0}
        ,0};
#else
#define NUM_TEMPERATURE_LOOPS NUM_EXTRUDER
#endif // HAVE_HEATED_BED



#if RESERVE_ANALOG_INPUTS
/** \brief This is an optional Temperature Sensor. //Nibbels
There is no Controller involved! It is not hooked into tempController
Do not try to do anything other than updateCurrentTemperature and reading the Temps.
TODO: Making a totally clean class, without the logic to controll something.
*/
TemperatureController optTempController = {0,RESERVE_ANALOG_SENSOR_TYPE,RESERVE_SENSOR_INDEX,0,0,0,0,   
#if FEATURE_HEAT_BED_TEMP_COMPENSATION
        0,
#endif // FEATURE_HEAT_BED_TEMP_COMPENSATION
        0, 0
        ,0,0,0,0,0,0,0,0,0,0,{0,0,0,0}
        ,0};
#endif // RESERVE_ANALOG_INPUTS


TemperatureController *tempController[NUM_TEMPERATURE_LOOPS] =
{
#if NUM_EXTRUDER>0
    &extruder[0].tempControl
#endif // NUM_EXTRUDER>0

#if NUM_EXTRUDER>1
    ,&extruder[1].tempControl
#endif // NUM_EXTRUDER>1

#if NUM_EXTRUDER>2
    ,&extruder[2].tempControl
#endif // NUM_EXTRUDER>2

#if NUM_EXTRUDER>3
    ,&extruder[3].tempControl
#endif // NUM_EXTRUDER>3

#if NUM_EXTRUDER>4
    ,&extruder[4].tempControl
#endif // NUM_EXTRUDER>4

#if NUM_EXTRUDER>5
    ,&extruder[5].tempControl
#endif // NUM_EXTRUDER>5

#if HAVE_HEATED_BED
#if NUM_EXTRUDER==0
    &heatedBedController
#else
    ,&heatedBedController
#endif // NUM_EXTRUDER==0
#endif // HAVE_HEATED_BED
};

