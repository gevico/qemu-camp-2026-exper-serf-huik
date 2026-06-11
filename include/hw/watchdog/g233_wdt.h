#ifndef HW_WATCHDOG_G233_WDT_H
#define HW_WATCHDOG_G233_WDT_H

#include "hw/core/sysbus.h"
#include "hw/core/ptimer.h"

#define TYPE_G233_WATCHDOG "g233-watchdog"
OBJECT_DECLARE_SIMPLE_TYPE(G233WDTState, G233_WATCHDOG)

struct G233WDTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    ptimer_state *timer;

    /* 寄存器 */
    uint32_t ctrl;
    uint32_t load;
    uint32_t sr;
    bool locked;
};

#endif