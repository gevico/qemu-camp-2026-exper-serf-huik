#ifndef HW_TIMER_G233_PWM_H
#define HW_TIMER_G233_PWM_H

#include "hw/core/sysbus.h"
#include "hw/core/ptimer.h"
#include "qom/object.h"

#define TYPE_G233_PWM "g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

typedef struct G233PWMChannel {
    ptimer_state *timer;
    uint32_t ctrl;    /* bit0=EN, bit1=POL, bit2=INTIE */
    uint32_t period;
    uint32_t duty;
    G233PWMState *parent;  /* 反向指针 */
    int index;              /* 通道号 0-3 */
} G233PWMChannel;

struct G233PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    G233PWMChannel ch[4];
    uint32_t glb_done;  /* PWM_GLB[7:4]，DONE位 */
};

#endif