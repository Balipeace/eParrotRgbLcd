/* Copyright (C) 2017 Edwin Croissant
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the version 3 GNU General Public License as
 * published by the Free Software Foundation.
 */

#ifndef MYENUMS_H_
#define MYENUMS_H_

/*
 * To use enumerated types as parameters for functions the
 * declaration must be done in a separate header file.
 */


/*----pin assignments ----*/

enum pins {
	pinBeeper = 2,	// Passive beeper use tone library
	pinLed = 13,
	pinBoiler = 7, 	// DS18B20 Only
	pinVapor = 8,	// ICP1 for optional SMT172
	pinCS = 9,		// SD card cs
};

/*----Recognizable names for the sensor types ----*/

enum sensorType {
	NoSensor = 0,
	smt172 = 1,
	DS18B20 = 2,
};

enum createStatus {
	noCard,
	fullCard,
	fileOk
};

enum alarmStatus {
	disabled,
	armed,
	triggered,
	acknowledged
};

#endif /* MYENUMS_H_ */
