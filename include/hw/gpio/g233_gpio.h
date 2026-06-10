#ifndef HW_GPIO_G233_GPIO_H
#define HW_GPIO_G233_GPIO_H

#include "hw/core/sysbus.h"

#define TYPE_G233_GPIO "g233-gpio"
typedef struct G233GPIOState G233GPIOState;
DECLARE_INSTANCE_CHECKER(G233GPIOState, G233_GPIO,
                         TYPE_G233_GPIO)

struct G233GPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;        
    qemu_irq irq;              

    /* 寄存器 */
    uint32_t dir;
    uint32_t out;
    uint32_t ie;
    uint32_t is;
    uint32_t trig;
    uint32_t pol;
};

#endif /* HW_GPIO_G233_GPIO_H */