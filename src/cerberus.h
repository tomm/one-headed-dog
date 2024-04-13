#pragma once
#include <Arduino.h>
extern volatile byte pos;						/** Position in edit line currently occupied by cursor **/
extern volatile bool mode;	/** false = 6502 mode, true = Z80 mode**/
extern volatile bool cpurunning;			/** true = CPU is running, CAT should not use the buses **/
extern volatile bool interruptFlag;		/** true = Triggered by interrupt **/
extern volatile bool fast;	/** true = 8 MHz CPU clock, false = 4 MHz CPU clock **/
extern volatile bool expflag;

extern uint8_t cerb_ram[65536];
extern void cpoke(uint16_t addr, uint8_t val);
extern byte cpeek(unsigned int address);
extern unsigned int cpeekW(unsigned int address);
extern void cpokeW(unsigned int address, unsigned int data);
