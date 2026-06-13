#include "qemu/osdep.h"
#include "hw/core/ptimer.h"
#include "hw/core/irq.h"
#include "qemu/log.h"
#include "hw/watchdog/g233_wdt.h"

#define WDT_CTRL  0x00
#define WDT_LOAD  0x04
#define WDT_VAL   0x08
#define WDT_SR    0x0C
#define WDT_KEY   0x10

#define WDT_CTRL_EN     (1u << 0)
#define WDT_CTRL_INTEN  (1u << 1)
#define WDT_CTRL_RSTEN  (1u << 2)
#define WDT_CTRL_LOCK   (1u << 3)

#define WDT_KEY_FEED 0x5A5A5A5A
#define WDT_KEY_LOCK 0x1ACCE551

static uint64_t g233_wdt_read(void *opaque, hwaddr addr, unsigned size)
{
    G233WDTState *s = G233_WATCHDOG(opaque);
    switch (addr) {
    case WDT_CTRL:
        return s->ctrl;
    case WDT_LOAD:
        return s->load;
    case WDT_VAL:
        return ptimer_get_count(s->timer);
    case WDT_SR:
        return s->sr;
    case WDT_KEY:
        return 0;  /* 只写，读返回0 */
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }
}

static void g233_wdt_tick(void *opaque)
{
    G233WDTState *s = G233_WATCHDOG(opaque);

    s->sr |= 1; /* TIMEOUT */

    if (s->ctrl & WDT_CTRL_INTEN) {
        qemu_set_irq(s->irq, 1);
    }
}

static void g233_wdt_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    G233WDTState *s = G233_WATCHDOG(opaque);

    switch (addr) {
    case WDT_CTRL:
        if (s->locked) {
            return; /* 锁定后CTRL只读，忽略写入 */
        }
        {
            bool old_en = s->ctrl & WDT_CTRL_EN;
            bool new_en = val & WDT_CTRL_EN;
            s->ctrl = val & (WDT_CTRL_EN | WDT_CTRL_INTEN | WDT_CTRL_RSTEN);
            if (s->locked) {
                s->ctrl |= WDT_CTRL_LOCK;
            }
            if (!old_en && new_en) {
                ptimer_transaction_begin(s->timer);
                ptimer_set_count(s->timer, s->load);
                ptimer_run(s->timer, 1);
                ptimer_transaction_commit(s->timer);
            } else if (old_en && !new_en) {
                ptimer_transaction_begin(s->timer);
                ptimer_stop(s->timer);
                ptimer_transaction_commit(s->timer);
            }
        }
        break;

    case WDT_LOAD:
        s->load = val;
        break;

    case WDT_VAL:
        break; /* 只读，忽略 */

    case WDT_SR:
        s->sr &= ~val; /* write-1-to-clear */
        if (!(s->sr & 1)) {
            qemu_set_irq(s->irq, 0); /* TIMEOUT清除后，IRQ拉低 */
        }
        break;

    case WDT_KEY:
        if (val == WDT_KEY_FEED) {
            ptimer_transaction_begin(s->timer);
            ptimer_set_count(s->timer, s->load);
            if (s->ctrl & WDT_CTRL_EN) {
                ptimer_run(s->timer, 1);
            }
            ptimer_transaction_commit(s->timer);
            s->sr &= ~1; /* 清除TIMEOUT */
            qemu_set_irq(s->irq, 0);
        } else if (val == WDT_KEY_LOCK) {
            s->locked = true;
            s->ctrl |= WDT_CTRL_LOCK;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read  = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_wdt_realize(DeviceState *dev, Error **errp)
{
    G233WDTState *s = G233_WATCHDOG(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_wdt_ops, s,
                          "g233-wdt", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->timer = ptimer_init(g233_wdt_tick, s, PTIMER_POLICY_LEGACY);
    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, 1000); /* 1KHz */
    ptimer_transaction_commit(s->timer);
}

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *s = G233_WATCHDOG(dev);

    s->ctrl = 0;
    s->load = 0xFFFF;
    s->sr = 0;
    s->locked = false;

    ptimer_transaction_begin(s->timer);
    ptimer_set_count(s->timer, s->load);
    ptimer_stop(s->timer);
    ptimer_transaction_commit(s->timer);

    qemu_set_irq(s->irq, 0);
}


static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize      = g233_wdt_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}

static const TypeInfo g233_wdt_info = {
    .name          = TYPE_G233_WATCHDOG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .class_init    = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}
type_init(g233_wdt_register_types)

