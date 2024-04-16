#include "Z80.h"
#include "cerberus.h"
#include "fake6502.h"

Z80 z80;
fake6502_context m6502;

void cpu_reset()
{
    z80.reset();
    fake6502_reset(&m6502);
}

void init_cpus()
{
    // set z80 context to self, to use in m_readIO debug routine
    z80.setCallbacks(&z80);
    cpu_reset();
}

void cpu_z80_nmi()
{
    z80.NMI();
}

void cpu_6502_nmi()
{
    fake6502_nmi(&m6502);
}

void cpu_clockcycles(int num_clocks)
{
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
