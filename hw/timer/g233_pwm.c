#include "qemu/osdep.h"
#include "hw/core/ptimer.h"
#include "hw/core/irq.h"
#include "qemu/log.h"
#include "hw/timer/g233_pwm.h"

/* 全局寄存器 */
#define PWM_GLB         0x00

/* 通道寄存器基址计算 */
#define PWM_CH_BASE(n)      (0x10 + (n) * 0x10)
#define PWM_CH_CTRL(n)      (PWM_CH_BASE(n) + 0x00)
#define PWM_CH_PERIOD(n)    (PWM_CH_BASE(n) + 0x04)
#define PWM_CH_DUTY(n)      (PWM_CH_BASE(n) + 0x08)
#define PWM_CH_CNT(n)       (PWM_CH_BASE(n) + 0x0C)

/* PWM_CHn_CTRL 位字段 */
#define PWM_CTRL_EN     (1u << 0)
#define PWM_CTRL_POL    (1u << 1)
#define PWM_CTRL_INTIE  (1u << 2)

/* PWM_GLB 位字段 */
#define PWM_GLB_CH_EN(n)    (1u << (n))
#define PWM_GLB_CH_DONE(n)  (1u << (4 + (n)))

static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned size)
{
    G233PWMState *s = G233_PWM(opaque);

    if (addr == PWM_GLB) {
        uint32_t en_mirror = 0;
        int i;
        for (i = 0; i < 4; i++) {
            if (s->ch[i].ctrl & PWM_CTRL_EN) {
                en_mirror |= PWM_GLB_CH_EN(i);
            }
        }
        return s->glb_done | en_mirror;
    }

    if (addr >= 0x10 && addr < 0x10 + 4 * 0x10) {
        int n   = (addr - 0x10) / 0x10;
        int reg = (addr - 0x10) % 0x10;
        G233PWMChannel *ch = &s->ch[n];

        switch (reg) {
        case 0x00: return ch->ctrl;
        case 0x04: return ch->period;
        case 0x08: return ch->duty;
        case 0x0C:
            if (!(ch->ctrl & PWM_CTRL_EN) || ch->period == 0) {
                return 0;
            }
            return ch->period - ptimer_get_count(ch->timer);
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                  __func__, addr);
    return 0;
}

static void g233_pwm_update_irq(G233PWMState *s)
{
    int level = 0;
    int i;

    for (i = 0; i < 4; i++) {
        if ((s->glb_done & PWM_GLB_CH_DONE(i)) &&
            (s->ch[i].ctrl & PWM_CTRL_INTIE)) {
            level = 1;
        }
    }
    qemu_set_irq(s->irq, level);
}

static void g233_pwm_tick(void *opaque)
{
    G233PWMChannel *ch = (G233PWMChannel *)opaque;
    G233PWMState *s = ch->parent;
    int n = ch->index;

    /* ptimer是periodic模式，到0后会自动reload到limit(=period)，
     * 这里只需要处理"周期完成"这件事的副作用 */

    s->glb_done |= PWM_GLB_CH_DONE(n);
    g233_pwm_update_irq(s);
}

static void g233_pwm_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    G233PWMState *s = G233_PWM(opaque);

    if (addr == PWM_GLB) {
        /* bits[7:4] DONE: write-1-to-clear; bits[3:0] EN镜像只读，忽略 */
        s->glb_done &= ~(val & 0xF0);
        g233_pwm_update_irq(s);
        return;
    }

    if (addr >= 0x10 && addr < 0x10 + 4 * 0x10) {
        int n   = (addr - 0x10) / 0x10;
        int reg = (addr - 0x10) % 0x10;
        G233PWMChannel *ch = &s->ch[n];

        switch (reg) {
        case 0x00: { /* CTRL */
            bool old_en = ch->ctrl & PWM_CTRL_EN;
            bool new_en = val & PWM_CTRL_EN;

            ch->ctrl = val & (PWM_CTRL_EN | PWM_CTRL_POL | PWM_CTRL_INTIE);

            if (!old_en && new_en) {
                ptimer_transaction_begin(ch->timer);
                ptimer_set_limit(ch->timer, ch->period, 1);
                ptimer_run(ch->timer, 0);
                ptimer_transaction_commit(ch->timer);
            } else if (old_en && !new_en) {
                ptimer_transaction_begin(ch->timer);
                ptimer_stop(ch->timer);
                ptimer_transaction_commit(ch->timer);
            }
            g233_pwm_update_irq(s);
            break;
        }
        case 0x04: /* PERIOD */
            ch->period = val;
            if (ch->ctrl & PWM_CTRL_EN) {
                ptimer_transaction_begin(ch->timer);
                ptimer_set_limit(ch->timer, ch->period, 1);
                ptimer_transaction_commit(ch->timer);
            }
            break;
        case 0x08: /* DUTY */
            ch->duty = val;
            break;
        case 0x0C: /* CNT只读 */
            break;
        default:
            goto bad;
        }
        return;
    }

bad:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                  __func__, addr);
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_pwm_realize(DeviceState *dev, Error **errp)
{
    G233PWMState *s = G233_PWM(dev);
    int i;

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_pwm_ops, s,
                          "g233-pwm", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    for (i = 0; i < 4; i++) {
        s->ch[i].parent = s;
        s->ch[i].index = i;
        s->ch[i].timer = ptimer_init(g233_pwm_tick, &s->ch[i],
                                     PTIMER_POLICY_LEGACY);
        ptimer_transaction_begin(s->ch[i].timer);
        ptimer_set_freq(s->ch[i].timer, 1000); /* 1KHz */
        ptimer_transaction_commit(s->ch[i].timer);
    }
}

static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);
    int i;

    s->glb_done = 0;
    for (i = 0; i < 4; i++) {
        s->ch[i].ctrl = 0;
        s->ch[i].period = 0;
        s->ch[i].duty = 0;
        ptimer_transaction_begin(s->ch[i].timer);
        ptimer_stop(s->ch[i].timer);
        ptimer_set_count(s->ch[i].timer, 0);
        ptimer_transaction_commit(s->ch[i].timer);
    }
    qemu_set_irq(s->irq, 0);
}

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize      = g233_pwm_realize;
    dc->legacy_reset = g233_pwm_reset;
}

static const TypeInfo g233_pwm_info = {
    .name          = TYPE_G233_PWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .class_init    = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}
type_init(g233_pwm_register_types)