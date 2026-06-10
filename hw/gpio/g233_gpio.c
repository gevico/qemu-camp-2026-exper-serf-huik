#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/gpio/g233_gpio.h"
#include "qemu/log.h"

#define GPIO_DIR    0x00
#define GPIO_OUT    0x04
#define GPIO_IN     0x08
#define GPIO_IE     0x0C
#define GPIO_IS     0x10
#define GPIO_TRIG   0x14
#define GPIO_POL    0x18

static uint64_t g233_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    switch (addr) {
    case GPIO_DIR:  return s->dir;
    case GPIO_OUT:  return s->out;
    case GPIO_IN:   return s->out & s->dir;
    case GPIO_IE:   return s->ie;
    case GPIO_IS:   return s->is;
    case GPIO_TRIG: return s->trig;
    case GPIO_POL:  return s->pol;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }
}

static void g233_gpio_update(G233GPIOState *s)
{
    uint32_t pin_val = s->out & s->dir;  /* 当前输出引脚电平 */

    /* 电平触发：IS 跟随引脚电平实时计算 */
    uint32_t level_is = s->trig & (                  /* TRIG=1 的引脚 */
        (s->pol  &  pin_val) |   /* 高电平触发：POL=1 且引脚为高 */
        (~s->pol & ~pin_val)     /* 低电平触发：POL=0 且引脚为低 */
    );

    /* 将电平触发结果合并进 IS（边沿触发部分由 write 回调负责置位） */
    /* 先清掉所有电平触发引脚的旧 IS，再写入新值 */
    s->is = (s->is & ~s->trig) | (level_is & s->trig);

    /* 只有 IE 使能的引脚才汇聚到 PLIC */
    int level = (s->is & s->ie) != 0;
    qemu_set_irq(s->irq, level);
}

static void g233_gpio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    switch (addr) {
    case GPIO_DIR:
        s->dir = val;
        break;
    case GPIO_OUT: {
        uint32_t old_val = s->out & s->dir;
        s->out = val;
        uint32_t new_val = s->out & s->dir;
        /* 边沿触发：检测跳变，只处理 TRIG=0 的引脚 */
        uint32_t rising  = ~s->trig & ~old_val &  new_val & s->pol;
        uint32_t falling = ~s->trig &  old_val & ~new_val & ~s->pol;
        s->is |= (rising | falling) & s->ie;
        break;
    }
    case GPIO_IN:
        break;
    case GPIO_IE:
        s->ie = val;
        break;
    case GPIO_IS:
        s->is &= ~val;  /* write-1-to-clear */
        break;
    case GPIO_TRIG:
        s->trig = val;
        break;
    case GPIO_POL:
        s->pol = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }
    g233_gpio_update(s);  /* 每次写后重新计算 IS 和 PLIC 线 */
}

static const MemoryRegionOps g233_gpio_ops = {
    .read  = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);
    s->dir = s->out = s->ie = s->is = s->trig = s->pol = 0;
}

static void g233_gpio_realize(DeviceState *dev, Error **errp)
{
    G233GPIOState *s = G233_GPIO(dev);
    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_gpio_ops, s,
                          "g233-gpio", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_gpio_realize;
    dc->legacy_reset   = g233_gpio_reset;
}

static const TypeInfo g233_gpio_info = {
    .name          = TYPE_G233_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .class_init    = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}
type_init(g233_gpio_register_types)