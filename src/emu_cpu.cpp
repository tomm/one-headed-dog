#include "cerberus.h"
#include "emudevs/Z80.h"
#include "fake6502.h"

fabgl::Z80 z80;
fake6502_context m6502;

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
    fake6502_reset(&m6502);
}

void init_cpus() {
    z80.setCallbacks(0, Z80ReadByteCallback,
            Z80WriteByteCallback, Z80ReadWordCallback,
            Z80WriteWordCallback, Z80ReadIOCallback,
            Z80WriteIOCallback);

    cpu_reset();
}

void cpu_z80_nmi() {
    z80.NMI();
}

void cpu_6502_nmi() {
    fake6502_nmi(&m6502);
}

void cpu_clockcycles(int num_clocks) {
    if (cpurunning) {
        if (mode) {
            while (num_clocks > 0) {
                num_clocks -= z80.step();
            }
        } else {
            m6502.emu.clockticks = 0;
            while (m6502.emu.clockticks < num_clocks) {
                fake6502_step(&m6502);
            }
        }
    }
}
