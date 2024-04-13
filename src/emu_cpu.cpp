#include "cerberus.h"
#include "emudevs/Z80.h"
#include "vrEmu6502.h"

fabgl::Z80 z80;
VrEmu6502 *m6502;

static uint8_t M6502ReadByte(uint16_t addr, bool isDbg) {
    return cpeek(addr);
}
static void M6502WriteByte(uint16_t addr, uint8_t value) {
    cpoke(addr, value);
}
static int  Z80ReadByteCallback(void * context, int addr) {
    return cpeek(addr);
}
static void Z80WriteByteCallback(void * context, int addr, int value) {
    cpoke(addr, value);
}
static int  Z80ReadWordCallback(void * context, int addr) {
    return cpeekW(addr);
}
static void Z80WriteWordCallback(void * context, int addr, int value) {
    cpokeW(addr, value);
}
static int  Z80ReadIOCallback(void * context, int addr) { return 0; }
static void Z80WriteIOCallback(void * context, int addr, int value) {}

void cpu_reset() {
    z80.reset();
    vrEmu6502Reset(m6502);
}

void init_cpus() {
    z80.setCallbacks(NULL, Z80ReadByteCallback,
            Z80WriteByteCallback, Z80ReadWordCallback,
            Z80WriteWordCallback, Z80ReadIOCallback,
            Z80WriteIOCallback);

    m6502 = vrEmu6502New(CPU_W65C02, M6502ReadByte, M6502WriteByte);

    cpu_reset();
}

void cpu_z80_nmi() {
    z80.NMI();
}

void cpu_6502_nmi() {
    vrEmu6502Nmi(m6502);
}

void cpu_clockcycles(int num_clocks) {
    if (cpurunning) {
        if (mode) {
            while (num_clocks > 0) {
                num_clocks -= z80.step();
            }
        } else {
            while (num_clocks-- > 0) {
                vrEmu6502Tick(m6502);
            }
        }
    }
}
