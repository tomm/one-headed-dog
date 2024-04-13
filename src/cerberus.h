#pragma once

#include <Arduino.h>

// CAT firmware state
extern volatile bool mode;	/** false = 6502 mode, true = Z80 mode**/
extern volatile bool cpurunning;			/** true = CPU is running, CAT should not use the buses **/

// ram access
extern uint8_t cerb_ram[65536];
extern void cpoke(uint16_t addr, uint8_t val);
extern byte cpeek(unsigned int address);
extern unsigned int cpeekW(unsigned int address);
extern void cpokeW(unsigned int address, unsigned int data);

// cpu emulation
extern void cpu_reset();
extern void init_cpus();
extern void cpu_z80_nmi();
extern void cpu_6502_nmi();
extern void cpu_clockcycles(int num_clocks);
