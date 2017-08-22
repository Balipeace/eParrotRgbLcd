/* Copyright (C) 2017 Edwin Croissant
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the version 3 GNU General Public License as
 * published by the Free Software Foundation.
 */
#include <I2C.h>				//https://github.com/rambo/I2C
#include <OneWire.h>			//https://github.com/bigjosh/OneWireNoResistor
#include <RgbLcdKeyShieldI2C.h>	//https://github.com/EdwinCroissantArduinoLibraries/RgbLcdKeyShieldI2C
#include <SdFat.h>				//https://github.com/greiman/SdFat
#include <SimpleBMP280I2C.h>	//https://github.com/EdwinCroissantArduinoLibraries/SimpleBMP280I2C
#include <SimpleDS1307I2C.h>	//https://github.com/EdwinCroissantArduinoLibraries/SimpleDS1307I2C
#include <SingleDS18B20.h>		//https://github.com/EdwinCroissantArduinoLibraries/SingleDS18B20
#include <SMT172.h>				//https://github.com/EdwinCroissantArduinoLibraries/SMT172
#include "T2ABV.h"
#include "myEnums.h"

const char msgSplash1[] PROGMEM = "eParrot  RGB LCD";
const char msgSplash2[] PROGMEM = "V 0.0     (c) EC";
const char logFilename[] PROGMEM =  "RUN_00.CSV";
const char msgNoBaro[] PROGMEM = "No Barometer";
const char msgCanceled[] PROGMEM = "Canceled";
const char msgSaved[] PROGMEM = "Saved";
const char msgToLog[] PROGMEM = "Press S to log ";
const char msgToStop[] PROGMEM = "Press S to stop";
const char msgNoCard[] PROGMEM = "No card   ";
const char msgFullCard[] PROGMEM = "Card full ";
const char msghPa[] PROGMEM = "hPa";
const char msgNoValue[] PROGMEM = "--.-";
const char msgNoSensor[] PROGMEM = "  Sensor error  ";
const char msgBoilerOffset[] PROGMEM = "BLR OFFS";
const char msgVaporOffset[] PROGMEM = "VPR OFFS";

/*----make instances for the Dallas sensors, BMP280, etc. ----*/
OneWire pinBoilerSensor(pinBoiler), pinVaporSensor(pinVapor);
SingleDS18B20 BoilerDS18B20(&pinBoilerSensor), VaporDS18B20(&pinVaporSensor);
SimpleBMP280I2C baro(0x77); // for I2C address 0x77
// SimpleBMP280I2C baro(0x76); // for I2C address 0x76
RgbLcdKeyShieldI2C lcd;
SdFat sd;

/*-----( Declare Variables )-----*/

struct sensors {
	float BaroPressure;				// in hPa
	float BaroTemperature;			// in C
	sensorType VaporType;
	float VaporTemperature;			// in C
	float VaporABV;					// in %
	float VaporAlarmSetpoint;		// in C
	float H2OBoilingPoint;			// in C
	sensorType BoilerType;
	float BoilerTemperature;		// in C
	float BoilerABV;				// in %
	float BoilerLastTemperature;	// in C
	int16_t WarmupTime;				// in minutes
} Sensors;

union {
	uint8_t settingsArray[13];
	struct {
		float VaporOffset;			// in C
		float BoilerOffset;			// in C
		uint8_t Alarm[4];			// in %
		uint8_t AlarmWarmedUp;		// in C
	};
} Settings;

alarmStatus AlarmStatus;

struct log {
	char name[11];
	bool isLogging;
} LogFile;

char lineBuffer[20];
void (*AutoPageSlowRefresh)();
void (*AutoPageFastRefresh)();
void (*ReturnPage)();

uint32_t LastFastSensorUpdate;
uint32_t LastSlowSensorUpdate;
uint32_t StartTimeLog;
uint32_t LastAlarmUpdate;
uint32_t LastWarmingupUpdate;
uint8_t CurrentAlarm;


float *offset;
float oldOffset;


void saveSettings() {
	rtc.writeRam(0,Settings.settingsArray,13);
}

void loadSettings() {
	rtc.readRam(0, 13, Settings.settingsArray);
}


// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
	// get the time
	rtc.readClock();
	// return date using FAT_DATE macro to format fields
	*date = FAT_DATE(2000 + rtc.year, rtc.month, rtc.day);
	// return time using FAT_TIME macro to format fields
	*time = FAT_TIME(rtc.hour, rtc.minute, rtc.second);
}


// Helper function to initialize the sensors and start the conversion

void initVaporSensor() {
	SMT172::startTemperature(0.001);
	delay(5);
	if (SMT172::getStatus() != 2) {
		Sensors.VaporType = smt172;
	} else if (VaporDS18B20.read()) {
		VaporDS18B20.setResolution(SingleDS18B20::res12bit);
		VaporDS18B20.convert();
		Sensors.VaporType = DS18B20;
	} else {
		// disable internal pullup resistor
		pinMode(pinVapor, INPUT);
		digitalWrite(pinVapor, LOW);
		Sensors.VaporType = NoSensor;
	}
}

void initBoilerSensor() {
	if (BoilerDS18B20.read()) {
		BoilerDS18B20.setResolution(SingleDS18B20::res12bit);
		BoilerDS18B20.convert();
		Sensors.BoilerType = DS18B20;
	} else
		Sensors.BoilerType = NoSensor;
}


bool initDS18B20(SingleDS18B20* sensor) {
	if (sensor->setResolution(SingleDS18B20::res12bit)) {
		return sensor->convert();
	} else
		return false;
}


//The setup function is called once at startup of the sketch
void setup()
{
	I2c.begin();
	I2c.setSpeed(1);
	I2c.timeOut(10);
	lcd.begin();
	lcd.setColor(RgbLcdKeyShieldI2C::clWhite);

	if (!baro.begin()) {
		lcd.printP(msgNoBaro);
		while (true) {
		};
	}

	// check if time is set
	if (!rtc.readClock()) {
		rtc.clearRam();
		rtc.year = 17;
		rtc.month = 1;
		rtc.day = 1;
		rtc.hour = 12;
		rtc.minute = 0;
		rtc.second = 0;
		setTimeInit();
		// wait until time is set
		while (!rtc.readClock()) {
			lcd.readKeys();
		}
	}

	loadSettings();

	// Initialize the temperature sensors

	initBoilerSensor();
	initVaporSensor();

	// show splash
	lcd.printP(msgSplash1);
	lcd.setCursor(0,1);
	lcd.printP(msgSplash2);

	delay(3000);

	lcd.clear();

	readSlowSensors();
	LastSlowSensorUpdate=millis();
	readFastSensors();
	LastFastSensorUpdate=millis();

	for (int i = 0; i < 4; ++i) {
		if (Settings.Alarm[i] > 99)
			Settings.Alarm[i] = 99;
	}
	if (Settings.AlarmWarmedUp > 99)
		Settings.AlarmWarmedUp = 99;
	showMainInit();
	Settings.AlarmWarmedUp = 80;
}

void doFunctionAtInterval(void (*callBackFunction)(), uint32_t *lastEvent,
		uint32_t Interval) {
	uint32_t now = millis();
	if ((now - *lastEvent) >= Interval) {
		*lastEvent = now;
		callBackFunction();
	}
}

// The loop function is called in an endless loop
void loop()
{
	doFunctionAtInterval(readFastSensors, &LastFastSensorUpdate, 250);	// read the SMT172 every 250 millisecond
	doFunctionAtInterval(readSlowSensors, &LastSlowSensorUpdate, 1000);	// read the baro and DS18B20's every second
	doFunctionAtInterval(handleWarmingup, &LastWarmingupUpdate, 60000);	// check warming up every minute
	lcd.readKeys();
}

void handleWarmingup() {
	float DeltaT;
	// calculate the warmup time
	if ((Sensors.BoilerType != NoSensor) && (Sensors.BoilerLastTemperature > 0)
			&& (Sensors.BoilerTemperature < Settings.AlarmWarmedUp)) {
		DeltaT = Sensors.BoilerTemperature - Sensors.BoilerLastTemperature;
		Sensors.WarmupTime = int16_t(constrain((float(Settings.AlarmWarmedUp)
					- Sensors.BoilerTemperature) / DeltaT + 0.5, 0, 5999));
	} else
		Sensors.WarmupTime = 0;

	Sensors.BoilerLastTemperature = Sensors.BoilerTemperature;
}

void HandleAlarms() {
	if (	Sensors.VaporABV >= 0 &&
			Sensors.VaporABV < Settings.Alarm[CurrentAlarm]) {
		if (AlarmStatus == armed) {
			AlarmStatus = triggered;
		}
	} else {
		if (AlarmStatus != disabled) {
			AlarmStatus = armed;
		}
	}
	switch (AlarmStatus) {
		case disabled:
			lcd.setColor(RgbLcdKeyShieldI2C::clWhite);
			break;
		case armed:
			lcd.setColor(RgbLcdKeyShieldI2C::clGreen);
			break;
		case triggered:
			tone(pinBeeper, 440, 500);
			//no break
		case acknowledged:
			lcd.setColor(RgbLcdKeyShieldI2C::clRed);
			break;
	}
}


void readFastSensors() {
	if (Sensors.VaporType == smt172) {
		switch (SMT172::getStatus()) {
		case 0:
			break;
		case 1:
			Sensors.VaporTemperature = SMT172::getTemperature()
					+ Settings.VaporOffset;
			Sensors.VaporABV = TtoVaporABV(
					correctedAzeo(Sensors.VaporTemperature,
							Sensors.BaroPressure));
			SMT172::startTemperature(0.001);
			break;
		case 2:
			Sensors.VaporType = NoSensor;
		}
	}
	if (AutoPageFastRefresh)
		AutoPageFastRefresh();
}

void readSlowSensors() {
	// Retrieve the current pressure in Pascal.
	Sensors.BaroPressure = float(baro.getPressure()) / 100;
	// Retrieve the current temperature in 0.01 degrees Celcius.
	Sensors.BaroTemperature = float(baro.getLastTemperature()) / 100;
	// calculate the boiling point of water
	Sensors.H2OBoilingPoint = h2oBoilingPoint(Sensors.BaroPressure);

	if (Sensors.VaporType == DS18B20) {
		if (VaporDS18B20.read() && VaporDS18B20.convert()) {
			Sensors.VaporTemperature = VaporDS18B20.getTempAsC()
					+ Settings.VaporOffset;
			Sensors.VaporABV = TtoVaporABV(
					correctedAzeo(Sensors.VaporTemperature,
							Sensors.BaroPressure));
		} else
			Sensors.VaporType = NoSensor;
	}

	if (Sensors.BoilerType == DS18B20) {
		if (BoilerDS18B20.read() && BoilerDS18B20.convert()) {
			Sensors.BoilerTemperature = BoilerDS18B20.getTempAsC()
					+ Settings.BoilerOffset;
			Sensors.BoilerABV = TtoLiquidABV(
					correctedH2O(Sensors.BoilerTemperature,
							Sensors.BaroPressure));
		} else
			Sensors.BoilerType = NoSensor;
	}

	if (Sensors.VaporType == NoSensor)
		initVaporSensor();

	if (Sensors.BoilerType == NoSensor)
		initBoilerSensor();

	if (AutoPageSlowRefresh)
		AutoPageSlowRefresh();

	if (LogFile.isLogging)
		LogFile.isLogging = writeDataToFile();

	HandleAlarms();
}

createStatus createFile() {
	strcpy_P(LogFile.name, logFilename);
	if (!sd.begin(pinCS, SPI_QUARTER_SPEED))
		return noCard;

	// create a new file
	for (uint8_t i = 0; i < 100; ++i) {
		LogFile.name[4] = i / 10 + '0';
		LogFile.name[5] = i % 10 + '0';
		// only open a new file if it doesn't exist
		if (!sd.exists(LogFile.name)) {
			// set the file time stamp callback
			SdFile::dateTimeCallback(dateTime);
			File dataFile = sd.open(LogFile.name, FILE_WRITE);
			dataFile.print(LogFile.name);
			dataFile.print(' ');
			sprintf(lineBuffer, "20%.2hd-%.2hd-%.2hdT%.2hd:%.2hd:%.2hd",
					rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute,
					rtc.second);
			dataFile.println(lineBuffer);
			dataFile.close();
			// timestamp is creation time
			SdFile::dateTimeCallbackCancel();
			StartTimeLog = millis();
			return fileOk;
		}
	}
	return fullCard;
}

bool writeDataToFile() {
	File dataFile = sd.open(LogFile.name, FILE_WRITE);
	if (dataFile) {
		dataFile.print(millis() - StartTimeLog, DEC);
		dataFile.print(F(", "));
		dataFile.print(Sensors.BaroPressure, 1);
		dataFile.print(F(", "));
		dataFile.print(Sensors.BaroTemperature, 1);
		dataFile.print(F(", "));
		dataFile.print(Sensors.BoilerTemperature,2);
		dataFile.print(F(", "));
		if (Sensors.BoilerABV > 0)
			dataFile.print(Sensors.BoilerABV,1);
		dataFile.print(F(", "));
		dataFile.print(Sensors.VaporTemperature,2);
		dataFile.print(F(", "));
		if (Sensors.VaporABV > 0)
			dataFile.print(Sensors.VaporABV,1);
		dataFile.println();
		dataFile.close();
		return true;
	}
	return false;
}

void increaseDigit() {
	lcd.noCursor();
	uint8_t value = lcd.read();
	lcd.moveCursorLeft();
	if (value == ' ')
		lcd.print('-');
	else if (value == '-')
		lcd.print(' ');
	else if (value < (9 + 0x30))
		lcd.print(char(++value));
	else
		lcd.print('0');
	lcd.moveCursorLeft();
	lcd.cursor();
}

void decreaseDigit() {
	lcd.noCursor();
	uint8_t value = lcd.read();
	lcd.moveCursorLeft();
	if (value == ' ')
		lcd.print('-');
	else if (value == '-')
		lcd.print(' ');
	else if (value > (0 + 0x30))
		lcd.print(char(--value));
	else
		lcd.print('9');
	lcd.moveCursorLeft();
	lcd.cursor();
}

void nextDigitTime() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 9:
	case 12:
	case 9 + 0x40:
	case 12 + 0x40:
		lcd.setCursor(pos + 2, 0);
		break;
	case 15:
		lcd.setCursor(8, 1);
		break;
	case 15 + 0x40:
		lcd.setCursor(8, 0);
		break;
	default:
		lcd.moveCursorRight();
		break;
	}
	lcd.cursor();
}

void prevDigitTime() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 11:
	case 14:
	case 11 + 0x40:
	case 14 + 0x40:
		lcd.setCursor(pos - 2, 0);
		break;
	case 8:
		lcd.setCursor(15, 1);
		break;
	case 8 + 0x40:
		lcd.setCursor(15, 0);
		break;
	default:
		lcd.moveCursorLeft();
		break;
	}
	lcd.cursor();
}

void cancel() {
	lcd.noCursor();
	lcd.clear();
	lcd.printP(msgCanceled);
	delay(1000);
	ReturnPage();
}

void save() {
	lcd.noCursor();
	lcd.clear();
	lcd.printP(msgSaved);
	delay(1000);
	ReturnPage();
}

void showMainInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.clearKeys();
	lcd.keyUp.onShortPress = showAlarmsInit;
	lcd.keyDown.onShortPress = showBaroInit;
	lcd.keyRight.onShortPress = nextAlarm;
	lcd.keyRight.onRepPress = nextAlarm;
	lcd.keyLeft.onShortPress = prevAlarm;
	lcd.keyLeft.onRepPress = prevAlarm;
	lcd.keySelect.onShortPress = toggleAlarm;
	showMainRefreshSlow();
	AutoPageSlowRefresh = showMainRefreshSlow;
	AutoPageFastRefresh = showMainRefreshFast;
}


void nextAlarm() {
	if (CurrentAlarm < 3) {
		CurrentAlarm++;
	} else CurrentAlarm = 0;
}

void prevAlarm() {
	if (CurrentAlarm > 0) {
		CurrentAlarm--;
	} else CurrentAlarm = 3;
}

void toggleAlarm() {
	switch (AlarmStatus) {
		case disabled:
			AlarmStatus = armed;
			break;
		case armed:
		case acknowledged:
			AlarmStatus = disabled;
			break;
		case triggered:
			AlarmStatus = acknowledged;
			break;
		default:
			break;
	}
}

void printVaporValues() {
	lcd.print(dtostrf(Sensors.VaporTemperature, 6, 2, lineBuffer));
	lcd.print('C');
	lcd.print(' ');
	if (Sensors.VaporABV < 0)
		lcd.printP(msgNoValue);
	else
		lcd.print(dtostrf(Sensors.VaporABV, 4, 1, lineBuffer));
	lcd.print('%');
	lcd.print(dtostrf(Settings.Alarm[CurrentAlarm], 3, 0, lineBuffer));
}

void printBoilerValues() {
	lcd.print(dtostrf(Sensors.BoilerTemperature, 6, 2, lineBuffer));
	lcd.print('C');
	lcd.print(' ');
	if (Sensors.BoilerABV < 0)
		lcd.printP(msgNoValue);
	else
		lcd.print(dtostrf(Sensors.BoilerABV, 4, 1, lineBuffer));
	lcd.print('%');
	for (int i = 0; i < 3; ++i) {
		lcd.print(' ');
	}
}

void printRemainingTime() {
	lcd.print(dtostrf(Sensors.BoilerTemperature, 6, 2, lineBuffer));
	sprintf(lineBuffer, "C %.2hd:%.2hd %.2hd", Sensors.WarmupTime / 60 , Sensors.WarmupTime % 60, Settings.AlarmWarmedUp);
	lcd.print(lineBuffer);
}

void showMainRefreshSlow() {
	lcd.setCursor(0, 0);
	switch (Sensors.VaporType) {
		case DS18B20:
			printVaporValues();
			break;
		case NoSensor:
			lcd.printP(msgNoSensor);
			break;
		default:
			break;
	}
	lcd.setCursor(0, 1);
	switch (Sensors.BoilerType) {
		case DS18B20:
			if (Sensors.WarmupTime > 0)
				printRemainingTime();
			else
				printBoilerValues();
			break;
		case NoSensor:
			lcd.printP(msgNoSensor);
			break;
		default:
			break;
	}
}
void showMainRefreshFast() {
	if (Sensors.VaporType == smt172) {
	lcd.setCursor(0, 0);
	printVaporValues();
	}
}

void showBaroInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.clearKeys();
	lcd.keyUp.onShortPress = showMainInit;
	lcd.keyDown.onShortPress = showTimeInit;
	showBaroRefresh();
	AutoPageSlowRefresh = showBaroRefresh;
	AutoPageFastRefresh = nullptr;
}

void showBaroRefresh() {
	lcd.setCursor(0,0);
	lcd.print(dtostrf(Sensors.BaroPressure, 4, 0, lineBuffer));
	lcd.moveCursorRight();
	lcd.printP(msghPa);
	lcd.moveCursorRight();
	lcd.print(dtostrf(Sensors.BaroTemperature, 4, 2, lineBuffer));
	lcd.moveCursorRight();
	lcd.print('C');

	lcd.setCursor(0,1);
	lcd.print(F("BP H2O  "));
	lcd.print(dtostrf(Sensors.H2OBoilingPoint, 6, 2, lineBuffer));
	lcd.moveCursorRight();
	lcd.print('C');
}
void showTimeInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.clearKeys();
	lcd.keyUp.onShortPress = showBaroInit;
	lcd.keyDown.onShortPress = showLogStatusInit;
	lcd.keySelect.onShortPress = setTimeInit;
	showTimeRefresh();
	AutoPageSlowRefresh = showTimeRefresh;
	AutoPageFastRefresh = nullptr;
	ReturnPage = showTimeInit;
}

void showTimeRefresh() {
	rtc.readClock();
	lcd.setCursor(4,0);
	sprintf(lineBuffer, "%.2hd/%.2hd/%.2hd", rtc.year, rtc.month, rtc.day);
	lcd.print(lineBuffer);
	lcd.setCursor(4,1);
	sprintf(lineBuffer, "%.2hd:%.2hd:%.2hd", rtc.hour, rtc.minute, rtc.second);
	lcd.print(lineBuffer);
}

void setTimeInit() {
	AutoPageSlowRefresh = nullptr;
	AutoPageFastRefresh = nullptr;
	lcd.clear();
	lcd.noCursor();
	sprintf(lineBuffer, "YYMMDD  %.2hd/%.2hd/%.2hd", rtc.year, rtc.month, rtc.day);
	lcd.print(lineBuffer);
	lcd.setCursor(0,1);
	sprintf(lineBuffer, "HHMMSS  %.2hd:%.2hd:%.2hd", rtc.hour, rtc.minute, rtc.second);
	lcd.print(lineBuffer);
	lcd.setCursor(8,0);
	lcd.cursor();
	lcd.clearKeys();
	lcd.keyUp.onShortPress = increaseDigit;
	lcd.keyUp.onRepPress = increaseDigit;
	lcd.keyDown.onShortPress = decreaseDigit;
	lcd.keyDown.onRepPress = decreaseDigit;
	lcd.keyRight.onShortPress = nextDigitTime;
	lcd.keyRight.onRepPress = nextDigitTime;
	lcd.keyLeft.onShortPress = prevDigitTime;
	lcd.keyLeft.onRepPress = prevDigitTime;
	lcd.keySelect.onShortPress = cancel;
	lcd.keySelect.onLongPress = setTime;
}

void setTime() {
	lcd.noCursor();
	uint8_t value;

	lcd.setCursor(8,0);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.year = value;

	lcd.setCursor(11,0);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.month = value;

	lcd.setCursor(14,0);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.day = value;

	lcd.setCursor(8,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.hour = value;

	lcd.setCursor(11,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.minute = value;

	lcd.setCursor(14,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	rtc.second = value;

	rtc.setClock();
	save();
}

void showLogStatusInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.clearKeys();
	if (LogFile.isLogging) {
		lcd.setCursor(0,0);
		lcd.print(LogFile.name);
		lcd.setCursor(0,1);
		lcd.printP(msgToStop);
	}
	else {
		lcd.setCursor(0,1);
		lcd.printP(msgToLog);
	}
	lcd.keyUp.onShortPress = showTimeInit;;
	lcd.keyDown.onShortPress = showOffsetVaporInit;
	lcd.keySelect.onShortPress = toggleLogging;
	AutoPageSlowRefresh = nullptr;
	AutoPageFastRefresh = nullptr;
}

void toggleLogging () {
	if (LogFile.isLogging) {
		LogFile.isLogging = false;
		lcd.setCursor(0,0);
		for (int i = 0; i < 11; ++i) {
			lcd.print(' ');
		};
		lcd.setCursor(0,1);
		lcd.printP(msgToLog);
	} else {
		lcd.setCursor(0,0);
		switch (createFile()) {
			case noCard:
				lcd.printP(msgNoCard);
				break;
			case fullCard:
				lcd.printP(msgFullCard);
				break;
			case fileOk:
				lcd.print(LogFile.name);
				LogFile.isLogging = true;
				lcd.setCursor(0,1);
				lcd.printP(msgToStop);
				break;
		}
	}
}

void showOffsetVaporInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.printP(msgVaporOffset);
	lcd.setCursor(15,0);
	lcd.print('C');
	lcd.clearKeys();
	lcd.keyUp.onShortPress = showLogStatusInit;
	lcd.keyDown.onShortPress = showOffsetBoilerInit;
	lcd.keySelect.onShortPress = offsetKeyRemap;
	ReturnPage = showOffsetVaporInit;
	AutoPageSlowRefresh = nullptr;
	AutoPageFastRefresh = showOffsetVaporRefresh;
	oldOffset = Settings.VaporOffset;
	offset = &Settings.VaporOffset;
}


void offsetKeyRemap() {
	lcd.setCursor(14,0);
	lcd.keyRight.onShortPress = nextDigitOffset;
	lcd.keyLeft.onShortPress = prevDigitOffset;
	lcd.keyUp.onShortPress = incDigitOffset;
	lcd.keyUp.onRepPress = incDigitOffset;
	lcd.keyDown.onShortPress = decDigitOffset;
	lcd.keyDown.onRepPress = decDigitOffset;
	lcd.keySelect.onShortPress = offsetCancel;
	lcd.keySelect.onLongPress = offsetSave;
}

void offsetCancel() {
	*offset = oldOffset;
	cancel();
}

void offsetSave() {
	saveSettings();
	save();
}

void showOffsetVaporRefresh() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	lcd.setCursor(9,0);
	lcd.print(dtostrf(Settings.VaporOffset, 6, 2, lineBuffer));
	lcd.setCursor(0,1);
	printVaporValues();
	lcd.setCursor(pos,0);
	lcd.cursor();
}

void showOffsetBoilerInit() {
	lcd.clear();
	lcd.noCursor();
	lcd.clearKeys();
	lcd.printP(msgBoilerOffset);
	lcd.setCursor(15,0);
	lcd.print('C');
	lcd.keyUp.onShortPress = showOffsetVaporInit;
	lcd.keyDown.onShortPress = showAlarmsInit;
	lcd.keySelect.onShortPress = offsetKeyRemap;
	ReturnPage = showOffsetBoilerInit;
	AutoPageSlowRefresh = nullptr;
	AutoPageFastRefresh = showOffsetBoilerRefresh;
	oldOffset = Settings.BoilerOffset;
	offset = &Settings.BoilerOffset;
}

void showOffsetBoilerRefresh() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	lcd.setCursor(9,0);
	lcd.print(dtostrf(Settings.BoilerOffset, 6, 2, lineBuffer));
	lcd.setCursor(0,1);
	printBoilerValues();
	lcd.setCursor(pos,0);
	lcd.cursor();
}

void nextDigitOffset() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 11:
		lcd.setCursor(13, 0);
		break;
	case 15:
		lcd.setCursor(9, 0);
		break;
	default:
		lcd.moveCursorRight();
		break;
	}
	lcd.cursor();
}

void prevDigitOffset() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 13:
		lcd.setCursor(11, 0);
		break;
	case 9:
		lcd.setCursor(15, 0);
		break;
	default:
		lcd.moveCursorLeft();
		break;
	}
	lcd.cursor();
}

void incDigitOffset() {
	uint8_t pos = lcd.getCursor();
	switch (pos) {
	case 9:
		*offset += 100;
		break;
	case 10:
		*offset += 10;
		break;
	case 11:
		*offset += 1;
		break;
	case 13:
		*offset += 0.1;
		break;
	case 14:
		*offset += 0.01;
		break;
	case 15:
		*offset = 0;
		break;
	default:
		break;
	}
}

void decDigitOffset() {
	uint8_t pos = lcd.getCursor();
	switch (pos) {
	case 9:
		*offset -= 100;
		break;
	case 10:
		*offset -= 10;
		break;
	case 11:
		*offset -= 1;
		break;
	case 13:
		*offset -= 0.1;
		break;
	case 14:
		*offset -= 0.01;
		break;
	case 15:
		*offset = 0;
		break;
	default:
		break;
	}
}

void showAlarmsInit() {
	lcd.clear();
	lcd.noCursor();
	sprintf(lineBuffer, "1 %.2hd%% 2 %.2hd%%  BLR", Settings.Alarm[0], Settings.Alarm[1]);
	lcd.print(lineBuffer);
	lcd.setCursor(0,1);
	sprintf(lineBuffer, "3 %.2hd%% 4 %.2hd%%  %.2hdC", Settings.Alarm[2], Settings.Alarm[3], Settings.AlarmWarmedUp);
	lcd.print(lineBuffer);
	lcd.clearKeys();
	lcd.keyUp.onShortPress = showOffsetBoilerInit;
	lcd.keyDown.onShortPress = showMainInit;
	lcd.keySelect.onShortPress = alarmsKeyRemap;
	ReturnPage =  showAlarmsInit;
	AutoPageSlowRefresh = nullptr;
	AutoPageFastRefresh = nullptr;
}

void alarmsKeyRemap() {
	lcd.setCursor(2,0);
	lcd.cursor();
	lcd.clearKeys();
	lcd.keyUp.onShortPress = increaseDigit;
	lcd.keyUp.onRepPress = increaseDigit;
	lcd.keyDown.onShortPress = decreaseDigit;
	lcd.keyDown.onRepPress = decreaseDigit;
	lcd.keyRight.onShortPress = nextDigitAlarm;
	lcd.keyRight.onRepPress = nextDigitAlarm;
	lcd.keyLeft.onShortPress = prevDigitAlarm;
	lcd.keyLeft.onRepPress = prevDigitAlarm;
	lcd.keySelect.onShortPress = cancel;
	lcd.keySelect.onLongPress = setAlarms;
}

void nextDigitAlarm() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 3:
		lcd.setCursor(8, 0);
		break;
	case 9:
		lcd.setCursor(2, 1);
		break;
	case 3 + 0x40:
		lcd.setCursor(8, 1);
		break;
	case 9 + 0x40:
		lcd.setCursor(13, 1);
		break;
	case 14 + 0x40:
		lcd.setCursor(2, 0);
		break;
	default:
		lcd.moveCursorRight();
		break;
	}
	lcd.cursor();
}

void prevDigitAlarm() {
	uint8_t pos = lcd.getCursor();
	lcd.noCursor();
	switch (pos) {
	case 2:
		lcd.setCursor(14, 1);
		break;
	case 8:
		lcd.setCursor(3, 0);
		break;
	case 2 + 0x40:
		lcd.setCursor(9, 0);
		break;
	case 8 + 0x40:
		lcd.setCursor(3, 1);
		break;
	case 13 + 0x40:
		lcd.setCursor(9, 1);
		break;
	default:
		lcd.moveCursorLeft();
		break;
	}
	lcd.cursor();
}

void setAlarms() {
	lcd.noCursor();
	uint8_t value;

	lcd.setCursor(2,0);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	Settings.Alarm[0] = value;

	lcd.setCursor(8,0);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	Settings.Alarm[1] = value;

	lcd.setCursor(2,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	Settings.Alarm[2] = value;

	lcd.setCursor(8,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	Settings.Alarm[3] = value;

	lcd.setCursor(13,1);
	value = (lcd.read() - 0x30) * 10;
	value += (lcd.read() - 0x30);
	Settings.AlarmWarmedUp = value;

	saveSettings();
	save();
}












