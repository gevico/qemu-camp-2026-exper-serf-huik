#ifndef HW_SSI_G233_SPI_H
#define HW_SSI_G233_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"

#define TYPE_G233_SPI "g233-spi"
typedef struct G233SPIState G233SPIState;
DECLARE_INSTANCE_CHECKER(G233SPIState, G233_SPI, TYPE_G233_SPI)

struct G233SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;          /* PLIC IRQ5, 错误中断 */
    qemu_irq cs_lines[4];  /* 4路CS输出 */
    SSIBus *spi;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint8_t  dr;            /* 接收数据，读时返回 */
};

#endif