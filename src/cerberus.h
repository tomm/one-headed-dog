#pragma once

#include <stdint.h>

// CAT firmware state
extern volatile bool mode;	/** false = 6502 mode, true = Z80 mode**/
extern volatile bool cpurunning;			/** true = CPU is running, CAT should not use the buses **/

// ram access
extern uint8_t cerb_ram[65536];
static inline void cpoke(uint16_t addr, uint8_t val) { cerb_ram[addr] = val; }
static inline uint8_t cpeek(uint16_t address) { return cerb_ram[address]; }
extern unsigned int cpeekW(unsigned int address);
extern void cpokeW(unsigned int address, unsigned int data);

// cpu emulation
extern void cpu_reset();
extern void init_cpus();
extern void cpu_z80_nmi();
extern void cpu_6502_nmi();
extern void cpu_clockcycles(int num_clocks);
