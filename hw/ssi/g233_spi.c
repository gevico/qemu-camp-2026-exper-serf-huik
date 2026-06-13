#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/core/qdev-properties.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/ssi/g233_spi.h"

#define SPI_CR1   0x00
#define SPI_CR2   0x04
#define SPI_SR    0x08
#define SPI_DR    0x0C

#define SPI_CR1_SPE     (1u << 0)
#define SPI_CR1_MSTR    (1u << 2)
#define SPI_CR1_ERRIE   (1u << 5)
#define SPI_CR1_RXNEIE  (1u << 6)
#define SPI_CR1_TXEIE   (1u << 7)

#define SPI_CR2_CS_SEL_MASK  0x3

#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_OVERRUN  (1u << 4)

static void g233_spi_update_irq(G233SPIState *s)
{
    bool level = false;

    if ((s->cr1 & SPI_CR1_TXEIE) &&
        (s->sr  & SPI_SR_TXE)) {
        level = true;
    }

    if ((s->cr1 & SPI_CR1_RXNEIE) &&
        (s->sr  & SPI_SR_RXNE)) {
        level = true;
    }

    if ((s->cr1 & SPI_CR1_ERRIE) &&
        (s->sr  & SPI_SR_OVERRUN)) {
        level = true;
    }

    qemu_set_irq(s->irq, level);
}

static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned size)
{
    G233SPIState *s = G233_SPI(opaque);

    switch (addr) {
    case SPI_CR1: return s->cr1;
    case SPI_CR2: return s->cr2;
    case SPI_SR:  return s->sr;
    case SPI_DR:
        s->sr &= ~SPI_SR_RXNE;  /* 读DR清除RXNE */
        g233_spi_update_irq(s);
        return s->dr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }
}

static void g233_spi_update_cs(G233SPIState *s)
{
    bool active = (s->cr1 & SPI_CR1_SPE) && (s->cr1 & SPI_CR1_MSTR);
    int sel = s->cr2 & SPI_CR2_CS_SEL_MASK;
    int i;

    for (i = 0; i < 4; i++) {
        if (s->cs_lines[i]) {
            qemu_set_irq(s->cs_lines[i], !(active && i == sel));
        }
    }
}


static void g233_spi_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    G233SPIState *s = G233_SPI(opaque);

    switch (addr) {
    case SPI_CR1:
        s->cr1 = val & 0xFF; /* 只保留低8位有效字段 */
        g233_spi_update_cs(s);
        g233_spi_update_irq(s);
        break;
    case SPI_CR2:
        s->cr2 = val & SPI_CR2_CS_SEL_MASK;
        g233_spi_update_cs(s);
        break;
    case SPI_SR:
        /* 仅OVERRUN(bit4)可写1清除 */
        s->sr &= ~(val & SPI_SR_OVERRUN);
        g233_spi_update_irq(s);
        break;
    case SPI_DR:
        if ((s->cr1 & SPI_CR1_SPE) && (s->cr1 & SPI_CR1_MSTR)) {
            if (s->sr & SPI_SR_RXNE) {
                s->sr |= SPI_SR_OVERRUN;
            }
            s->sr &= ~SPI_SR_TXE;
            s->dr = ssi_transfer(s->spi, (uint8_t)val);
            s->sr |= SPI_SR_RXNE;
            s->sr |= SPI_SR_TXE;
            g233_spi_update_irq(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);
    DeviceState *flash0, *flash1;

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_spi_ops, s,
                          "g233-spi", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);  /* PLIC IRQ5 */

    /* 创建SSI总线 */
    s->spi = ssi_create_bus(dev, "spi");

    /* CS0: W25X16 */
    flash0 = qdev_new("w25x16");
    qdev_prop_set_uint8(flash0, "cs", 0);
    ssi_realize_and_unref(flash0, s->spi, &error_fatal);
    s->cs_lines[0] = qdev_get_gpio_in_named(flash0, SSI_GPIO_CS, 0);

    /* CS1: W25X32 */
    flash1 = qdev_new("w25x32");
    qdev_prop_set_uint8(flash1, "cs", 1);
    ssi_realize_and_unref(flash1, s->spi, &error_fatal);
    s->cs_lines[1] = qdev_get_gpio_in_named(flash1, SSI_GPIO_CS, 0);

    /* CS2/CS3未连接外部设备 */
    s->cs_lines[2] = NULL;
    s->cs_lines[3] = NULL;
}

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    s->cr1 = 0;
    s->cr2 = 0;
    s->sr  = SPI_SR_TXE;  /* 复位值0x2 */
    s->dr  = 0;

    g233_spi_update_cs(s);  /* SPE=0 → 所有CS释放 */
    g233_spi_update_irq(s);
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
}

static const TypeInfo g233_spi_info = {
    .name           = TYPE_G233_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(G233SPIState),
    .class_init     = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}
type_init(g233_spi_register_types)