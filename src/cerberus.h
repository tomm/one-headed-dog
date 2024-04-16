#pragma once

#include <stdint.h>

#define DEBUG
void debug_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

// XXX don't change this. it breaks autoloadBinaryFilename
#ifdef PLATFORM_SDL
#define SDCARD_MOUNT_PATH "."
#else /* PLATFORM_FABGL */
#define SDCARD_MOUNT_PATH "/sd"
#endif

// main.cpp
extern int readKey();

// CAT firmware state
extern void cpuInterrupt(void);
extern void cat_loop();
extern void cat_setup();
extern volatile bool fast; /** true = 8 MHz CPU clock, false = 4 MHz CPU clock **/
extern volatile bool mode; /** false = 6502 mode, true = Z80 mode**/
extern volatile bool cpurunning; /** true = CPU is running, CAT should not use the buses **/

// ram access
extern uint8_t cerb_ram[65536];
static inline void cpoke(uint16_t addr, uint8_t val) { cerb_ram[addr] = val; }
static inline uint8_t cpeek(uint16_t address) { return cerb_ram[address]; }
static inline unsigned int cpeekW(unsigned int address)
{
    return (cpeek(address) | (cpeek(address + 1) << 8));
}
static inline void cpokeW(unsigned int address, unsigned int data)
{
    cpoke(address, data & 0xFF);
    cpoke(address + 1, (data >> 8) & 0xFF);
}

// cpu emulation
extern void cpu_reset();
extern void init_cpus();
extern void cpu_z80_nmi();
extern void cpu_6502_nmi();
extern void cpu_clockcycles(int num_clocks);

// PS2 defines
#define PS2_ESC 27
#define PS2_F12 1
#define PS2_ENTER 13
// yeah these really are the same in PS2Keyboard.h
#define PS2_DELETE 127
#define PS2_BACKSPACE 127
#define PS2_UPARROW 11
#define PS2_LEFTARROW 8
#define PS2_DOWNARROW 10
#define PS2_RIGHTARROW 21
