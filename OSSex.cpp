/* OSSex.cpp v0.4 - Library for controlling Arduino-based sex-toys
 * Written by Craig Durkin/Comingle
 * {♥} COMINGLE
*/


#include <Arduino.h>
#include <OSSex.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include "OneButton.h"

// Pre-instantiate as a Mod. Pre-instantiation is necessary for the
// timer2/timer4 interrupt to work. If using a different toy,
// call Toy.setID(<toy model>) in setup() of your sketch.
OSSex::OSSex() {
	setID(MOD);
}
OSSex Toy = OSSex();

// Set up the interrupt to trigger the update() function.
#if defined(__AVR_ATmega32U4__) // Lilypad USB / Mod
ISR(TIMER4_OVF_vect) {
	Toy.update();
};
#else
ISR(TIMER1_COMPA_vect){
    Toy.update();
};
#endif

// the real constructor. give it a device ID and it will set up your toy's pins and timers.
void OSSex::setID(int deviceId) {
	if (deviceId == 1) {
		// Mod
		device.outCount = 3;
		device.outPins[0] = 5;
		device.outPins[1] = 10;
		device.outPins[2] = 11;

		device.deviceId = 1;

		device.ledCount = 1;
		device.ledPins[0] = 13;

		// Inputs 2 and 3 remain unconnected in most models. use enableExtraInputs() to enable.
		device.inCount = 2;
#if defined(A9) // Lilypad USB / Mod
		device.inPins[0] = A7; // D-
		device.inPins[1] = A9; // D+
#endif
		device.inPins[2] = A2;
		device.inPins[3] = A3;

    // Pins for setting the Hacker Port mode
		device.muxPins[0] = 8;
		device.muxPins[1] = 12;
		pinMode(device.muxPins[0], OUTPUT);
		pinMode(device.muxPins[1], OUTPUT);
		setHackerPort(HACKER_PORT_AIN);

		device.buttons[0].pin = 4;

		// A0 is connected to battery voltage
		pinMode(A0, INPUT);

    } else if(deviceId == 2) {
        device.outCount = 2;
        device.outPins[0] = 5;
        device.outPins[1] = 6;
        
        device.deviceId = 2;
        
        device.ledCount = 1;
        device.ledPins[0] = 13;
        
        // Inputs 2 and 3 remain unconnected in most models. use enableExtraInputs() to enable.
        device.inCount = 2;
        device.inPins[2] = A2;
        device.inPins[3] = A3;
        
        device.buttons[0].pin = 4;

        
    } else {
		// Lilypad USB  / Alpha model
		device.outCount = 3;
		device.outPins[0] = 3;
		device.outPins[1] = 9;
		device.outPins[2] = 10;

		device.deviceId = 0;

		device.ledCount = 1;
		device.ledPins[0] = 13;

		device.inCount = 2;
		device.inPins[0] = A2; // D+
		device.inPins[1] = A3; // D-

		device.buttons[0].pin = 2;
	}
	device.bothWays = false;

	device.isLedMultiColor = false;

	device.buttons[0].button.setPin(device.buttons[0].pin);
	device.buttons[0].button.setActiveLow(true);

	for (int i = 0; i < device.outCount; i++) {
		pinMode(device.outPins[i], OUTPUT);
		if (device.bothWays) {
			pinMode(device.tuoPins[i], OUTPUT);
		}
	}
	for (int i = 0; i < device.inCount; i++) {
		pinMode(device.inPins[i], INPUT);
	}
	for (int i = 0; i < device.ledCount; i++) {
		pinMode(device.ledPins[i], OUTPUT);
	}
    noInterrupts();           // disable all interrupts

	// Start the interrupt timer (timer2/timer4)
	// Thanks for Noah at arduinomega.blogspot.com for clarifying this
	#if defined(__AVR_ATmega32U4__)
      _timer_start_mask = &TCCR4B;
      _timer_count = &TCNT4;
	  _timer_interrupt_flag = &TIFR4;
	  _timer_interrupt_mask_b = &TIMSK4;
	  _timer_init = TIMER4_INIT;
    
    *_timer_start_mask = 0x05;				// Timer PWM disable, prescale / 16: 00000101
        *_timer_count = _timer_init;			// Reset Timer Count
        *_timer_interrupt_flag = 0x00;			// Timer INT Flag Reg: Clear Timer Overflow Flag
        *_timer_interrupt_mask_b = 0x04;    // Timer INT Reg: Timer Overflow Interrupt Enable: 00000100
    #else
    TCCR1A = 0; // set entire TCCR1A register to 0
    TCCR1B = 0; // same for TCCR1B
    TCNT1  = 0; // initialize counter value to 0
    // set compare match register for 1000 Hz increments
    OCR1A = 15999; // = 16000000 / (1 * 1000) - 1 (must be <65536)
    // turn on CTC mode
    TCCR1B |= (1 << WGM12);
    // Set CS12, CS11 and CS10 bits for 1 prescaler
    TCCR1B |= (0 << CS12) | (0 << CS11) | (1 << CS10);
    // enable timer compare interrupt
    TIMSK1 |= (1 << OCIE1A);
    #endif

    interrupts();             // enable all interrupts

  _tickCount = 0;

  // Initial power and time scale is 1.0 (normal / 100% power and time).
	// Scale step of 0.1 increases/decreases power/time by 10%
	// with each call to increaseTime(), decreaseTime(), increasePower(), decreasePower()
 	_powerScale = 1.0;
 	_powerScaleStep = 0.1;
 	_timeScale = 1.0;
 	_timeScaleStep = 0.1;

	// set up pattern step queue as a 3 member ring buffer.
	for (int i = 0; i < 3; i++) {
		_memQueue[i] = new struct pattern;
	}

	_memQueue[0]->nextStep = _memQueue[1];
	_memQueue[1]->nextStep = _memQueue[2];
	_memQueue[2]->nextStep = _memQueue[0];
}


// Called by the timer interrupt to check if a change needs to be made to the pattern or update the button status.
// If a pattern is running, the _running flag will be true
void OSSex::update() {
	device.buttons[0].button.tick();
	if (_running) {
		_tickCount++;
		if (_tickCount > (_currentStep->duration * _timeScale)) {
  		if (_currentStep->nextStep == NULL || _currentStep->nextStep->duration == NULL) {
    		_running = false;
  		} else {
  			// set duration to NULL to flag current step as out-of-date and
				// then run the next step
				_currentStep->duration = NULL;
  			_currentStep = _currentStep->nextStep;

  			for (int i = 0; i < device.outCount; i++) {
  				if (_currentStep->power[i] >= 0) { // -1 value is "leave this motor alone"
	  					setOutput(i, _currentStep->power[i]);
	  				}
  			}
  		}
  		_tickCount = 0;
		} else if (!_currentStep->nextStep->duration && _patternCallback) {
			// if it's not time for the next step, go ahead and queue it up
			if (_patternCallback(_seq)) {
				_seq++;
				_currentStep->nextStep->power[0] = step[0];
				_currentStep->nextStep->power[1] = step[1];
				_currentStep->nextStep->power[2] = step[2];
				_currentStep->nextStep->duration = step[3];
			} else {
				_running = false;
			}
		}
	}

    // Hack alert -- start mask only needs to be initialized once, but wiring.c of the Arduino core
	// changes the mask back to 0x07 before setup() runs
	// So if running Toy.setID() from setup(), no problem. If preinsantiating as a Mod, problem.
#if defined(__AVR_ATmega32U4__)
	*_timer_start_mask = 0x05;
	*_timer_count = _timer_init;		//Reset timer after interrupt triggered
	*_timer_interrupt_flag = 0x00;		//Clear timer overflow flag
#endif
    
}


// Set an output to a particular power level. If outNumber is -1, set all outputs to powerLevel.
// outNumber of any other negative number or a number greater than or equal to the number of available outputs will be rolled over.
// Ex: in a 4 output device, you can access outputs 0, 1, 2, and 3.
// Specifying outNumber of -3 will map to output 3. Specifying an outNumber of 5 will map to output 1.
// powerLevel can be from 0..255 in devices that aren't bidirectional, and -255..255 in birdirectional devices.
// Negative powerLevel values are coerced to 0 in devices that aren't bidirectional.
// powerLevel of 0 turns the output off. Values greater than +/-255 get coerced to +/-255.

int OSSex::setOutput(int outNumber, int powerLevel) {
	int iterations = 1, constrainedPower;
	// set all outputs, starting at 0.
	if (outNumber == -1) {
		iterations = device.outCount;
		outNumber = 0;
	} else {
		outNumber = abs(outNumber) % device.outCount;
	}

	if (device.bothWays) {
		constrainedPower = constrain(powerLevel, -255, 255);
	} else {
		constrainedPower = constrain(powerLevel, 0, 255);
	}

	if (_powerScale * constrainedPower > 255) {
		_powerScale = 255/constrainedPower;
	} else if (_powerScale * constrainedPower < 0) {
		_powerScale = 0.0;
	}
	for (int i = 0; i < iterations; i++) {
		if (constrainedPower == 0) {
			analogWrite(device.outPins[outNumber], 0);
			if (device.bothWays) {
				analogWrite(device.tuoPins[outNumber], 0);
			}
		} else if (constrainedPower > 0) {
			analogWrite(device.outPins[outNumber], constrainedPower * _powerScale);
		} else {
			analogWrite(device.tuoPins[outNumber], constrainedPower * _powerScale);
		}
		outNumber = i+1;
	}

	return 1;
}


// Turn an LED on or off. lightLevel can be a value from 0-255. 0 turns the LED off.
// Accept html color codes (both "#50a6c2" and "midnight blue"?)
// Add serial (Stream object) feedback from function for diagnostics
//void OSSex::setLED(unsigned int lightLevel, ledNumber, colorCode) {}
int OSSex::setLED(int ledNumber, int powerLevel) {
	int constrainedPower;
	if (!device.ledCount) {
		return -1;
	}
	// sanitize ledNumber XXX -1 logic
	ledNumber %= device.ledCount;
	constrainedPower = constrain(powerLevel, 0, 255);
	analogWrite(device.ledPins[ledNumber], constrainedPower);

	return 1;
}

// Run preset pattern from an array of pattern steps. Supply runShortPattern()
// with an array of [motor0 power, motor1 power, motor2 power, duration] arrays
// and the number of steps in the pattern.
// This function will not return until the pattern is finished running.
int OSSex::runShortPattern(int* patSteps, size_t patternLength) {
	stop();

	if (!patternLength) return -1;

	// pull two steps at most
  int limit = 2 > patternLength ? patternLength : 2;
	for (int i = 0; i < limit; i++) {
		_memQueue[i]->power[0] = *(patSteps++);
		_memQueue[i]->power[1] = *(patSteps++);
		_memQueue[i]->power[2] = *(patSteps++);
		_memQueue[i]->duration = *(patSteps++);
	}

	patternLength -= limit;
	// _memQueue has old pattern steps in it that we're overwriting with this
	// new pattern. since we're only fetching two steps at most, we set duration
	//  of the the next step after that to NULL to flag it as out-of-date.
	_memQueue[limit]->duration = NULL;

	// position _currentStep at start of pattern, start the first step, and set things in motion
	_currentStep = _memQueue[0];
	for (int i = 0; i < device.outCount; i++) {
		if (_currentStep->power[i] >= 0) {
			setOutput(i, _currentStep->power[i]);
		}
	}
	_running = true;
	// Feed the pattern its next steps until it's done.
	while (_running) {
		if (_currentStep->nextStep->duration == NULL && patternLength) {
			// if our next step is out-of-date then queue it up
			patternLength--;
			_currentStep->nextStep->power[0] = *(patSteps++);
			_currentStep->nextStep->power[1] = *(patSteps++);
			_currentStep->nextStep->power[2] = *(patSteps++);
			_currentStep->nextStep->duration = *(patSteps++);
		}
	}
	return 1;
}


// Run a pattern from a callback function.
// The callback should set the Toy.step array with the appropriate values.
// Toy.step looks like: [motor0 power, motor1 power, motor2 power, duration]
// The pattern function should return a number greater than 0.
// runPattern() will return before the pattern is finished running.
int OSSex::runPattern(int (*callback)(int)) {
	stop();

	// get the first two steps of the sequence.
	// if we don't, some patterns with short first steps won't run well and
	// will have a race condition since the next step is queued while the
	// current one is running
	_patternCallback = callback;

	for (int i = 0; i < 2; i++) {
		if (!_patternCallback(i)) {
			return 0;
		}
		_memQueue[i]->power[0] = step[0];
		_memQueue[i]->power[1] = step[1];
		_memQueue[i]->power[2] = step[2];
	  _memQueue[i]->duration = step[3];
	}
	// since we already fetched steps 0 and 1
	_seq = 2;

	// flag the third step as out-of-date / needing to be fetched.
	_memQueue[2]->duration = NULL;

	_currentStep = _memQueue[0];
	for (int i = 0; i < device.outCount; i++) {
		if (_currentStep->power[i] >= 0) {
			setOutput(i, _currentStep->power[i]);
		}
	}

	_running = true;
	return 1;
}

// run a specific pattern from the queue
int OSSex::runPattern(unsigned int pos) {
  if (!_currentPattern) {
    return -1;
  }
  _currentPattern = _first;
  for (int i = 0; i < pos; i++) {
    _currentPattern = _currentPattern->nextPattern;
    if (_currentPattern == NULL) {
        return -2;
    }
  }
  return runPattern(_currentPattern->patternFunc);
}


// Return the queue number of the currently running pattern
// First pattern is 0, second is 1, etc.
int OSSex::getPattern() {
	if (!_currentPattern) {
    return -1;
  }
  int pos = 0;
  for (volatile patternList *stepper = _first; stepper != _currentPattern; stepper = stepper->nextPattern) {
	  if (stepper == NULL) {
      return -2;
    }
    pos++;
  }
  return pos;
}

// Set power scaling step value -- power scaling factor will change by step
// with each call of increasePower() or decreasePower()
void OSSex::setPowerScaleStep(float step) {
	_powerScaleStep = step;
}

// Set power scaling factor to powerScale
float OSSex::setPowerScaleFactor(float powerScale) {
	if (powerScale < 0.0) {
		_powerScale = 0.0;
	} else {
		_powerScale = powerScale;
	}
	return _powerScale;
}

// Return power scaling factor
float OSSex::getPowerScaleFactor() {
	return _powerScale;
}

float OSSex::increasePower() {
	_powerScale *= (1.0 + _powerScaleStep);
	return _powerScale;
}

float OSSex::decreasePower() {
	_powerScale *= (1.0 - _powerScaleStep);
	return _powerScale;
}

// Set time scaling step to "step" -- time scaling will change by step
// with each call of increaseTime() or decreaseTime()
void OSSex::setTimeScaleStep(float step) {
	_timeScaleStep = step;
}

// Set time scaling factor to timeScale
float OSSex::setTimeScaleFactor(float timeScale) {
	if (timeScale < 0.0) {
		_timeScale = 0.0;
	} else {
		_timeScale = timeScale;
	}
  return _timeScale;
}

float OSSex::increaseTime() {
	_timeScale *= (1.0 + _timeScaleStep);
	return _timeScale;
}

float OSSex::decreaseTime() {
	_timeScale *= (1.0 - _timeScaleStep);
	return _timeScale;
}

// Return time scaling factor
float OSSex::getTimeScaleFactor() {
	return _timeScale;
}

int OSSex::nextPattern() {
  if (!_currentPattern) {
    return -1;
  }

  if (_currentPattern->nextPattern == NULL) {
    _currentPattern = _first;
  } else {
    _currentPattern = _currentPattern->nextPattern;
  }
  runPattern(_currentPattern->patternFunc);
  return 1;
}

int OSSex::cyclePattern() {
	return nextPattern();
}

int OSSex::previousPattern() {
	volatile patternList *prev = _first;

	if (!_currentPattern) return -1;

	while (prev->nextPattern != _currentPattern && prev->nextPattern != NULL) {
		prev = prev->nextPattern;
	}
	_currentPattern = prev;
	runPattern(_currentPattern->patternFunc);
	return 1;
}

// Add a pattern function to the queue of vibration patterns
// Create queue if necessary
int OSSex::addPattern(int (*patternFunc)(int)) {
	int index = 0;
	volatile patternList *next;
	if (_first == NULL) {
		_first = new struct patternList;
		if (!_first) {
			return -1;
		}
		next = _first;
	} else {
		volatile patternList *iterator = _first;
		while (iterator->nextPattern != NULL) {
			iterator = iterator->nextPattern;
			index++;
		}

		iterator->nextPattern = new struct patternList;

		if (!iterator->nextPattern) {
			return -1;
		}
		next = iterator->nextPattern;
		index++;
	}
	next->patternFunc = patternFunc;

	next->nextPattern = NULL;
	_currentPattern = next;
	return index;
}

// stop all the motors and patterns, reset to beginning. this could be better-written.
void OSSex::stop() {
	_running = false;
	_powerScale = 1.0;
	_timeScale = 1.0;
	_seq = 0;
	setOutput(-1, 0);
	_patternCallback = NULL;
	step[0] = step[1] = step[2] = -1;
	step[3] = 0;
}

// Set hacker port multiplexer for reading certain types of inputs.
// Options are the HACKER_PORT_XXX cases in OSSex.h
// or below in the 'case' statements
int OSSex::setHackerPort(unsigned int flag) {
	byte pin0, pin1;

	if (device.deviceId < 1) {
		return -1;
	}

	switch (flag) {
		// Hacker Port analog in / PWM out
		case HACKER_PORT_AIN:
			pin0 = LOW;
			pin1 = LOW;
#if defined(A9) // Lilypad USB / Mod
			device.HP0 = A7;
			device.HP1 = A9;
#endif
            break;
		// Hacker Port I2C host
		case HACKER_PORT_I2C:
			pin0 = HIGH;
			pin1 = LOW;
			device.HP0 = 2;
			device.HP1 = 3;
			break;
		// Hacker Port software serial
		case HACKER_PORT_SERIAL:
			pin0 = LOW;
			pin1 = HIGH;
			device.HP0 = 15;	// RX
			device.HP1 = 14;	// TX
			break;
		default:
			return -1;
	}

	digitalWrite(device.muxPins[0], pin0);
	digitalWrite(device.muxPins[1], pin1);

	return 0;
}

unsigned int OSSex::getHackerPort() {
	uint8_t pin0 = digitalRead(device.muxPins[0]);
	uint8_t pin1 = digitalRead(device.muxPins[1]);
	if (pin0 == HIGH) {
		return HACKER_PORT_I2C;
	} else {
		if (pin1 == HIGH) {
			return HACKER_PORT_SERIAL;
		} else {
			return HACKER_PORT_AIN;
		}
	}
}

// Read input channel
unsigned int OSSex::getInput(int inNumber) {
	inNumber = abs(inNumber) % device.inCount;
	return analogRead(device.inPins[inNumber]);
}

// enables the 2 extra analog inputs present on the Dilduino. These are unconnected
// on the Mod.
void OSSex::enableExtraInputs(bool flag) {
	if (device.deviceId != 1) return;
	if (flag) {
	  device.inCount = 4;
	} else {
		device.inCount = 2;
	}
}

void OSSex::attachClick(void (*callback)()) {
	device.buttons[0].button.attachClick(callback);
}

void OSSex::attachDoubleClick(void (*callback)()) {
	device.buttons[0].button.attachDoubleClick(callback);
}

void OSSex::attachLongPressStart(void (*callback)()) {
	device.buttons[0].button.attachLongPressStart(callback);
}

void OSSex::attachLongPressStop(void (*callback)()) {
	device.buttons[0].button.attachLongPressStop(callback);
}

void OSSex::attachDuringLongPress(void (*callback)()) {
	device.buttons[0].button.attachDuringLongPress(callback);
}
