#include "cerberus.h"
#include "emudevs/Z80.h"
#include "emudevs/MOS6502.h"

fabgl::Z80 z80;
fabgl::MOS6502 m6502;

int M6502Page1ReadByte(void * context, int addr) {
    return cpeek(addr+0x100);
}
void M6502Page1WriteByte(void * context, int addr, int value) {
    cpoke(addr+0x100, value);
}
int  Z80ReadByteCallback(void * context, int addr) {
    return cpeek(addr);
}
void Z80WriteByteCallback(void * context, int addr, int value) {
    cpoke(addr, value);
}
int  Z80ReadWordCallback(void * context, int addr) {
    return cpeekW(addr);
}
void Z80WriteWordCallback(void * context, int addr, int value) {
    cpokeW(addr, value);
}
int  Z80ReadIOCallback(void * context, int addr) { return 0; }
void Z80WriteIOCallback(void * context, int addr, int value) {}

void init_cpus() {
    z80.setCallbacks(NULL, Z80ReadByteCallback,
            Z80WriteByteCallback, Z80ReadWordCallback,
            Z80WriteWordCallback, Z80ReadIOCallback,
            Z80WriteIOCallback);
    z80.reset();

    m6502.setCallbacks(NULL,
            Z80ReadByteCallback,
            Z80WriteByteCallback,
            Z80ReadByteCallback,
            Z80WriteByteCallback,
            M6502Page1ReadByte,
            M6502Page1WriteByte);
    m6502.reset();
}

void cpu_clockcycles(int num_clocks) {
    if (cpurunning) {
        if (mode) {
            while (num_clocks > 0) {
                num_clocks -= z80.step();
            }
        } else {
            while (num_clocks > 0) {
                num_clocks -= m6502.step();
            }
        }
    }
}
