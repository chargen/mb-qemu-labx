
/*
 * QEMU model of the LabX legacy ethernet core.
 *
 * Copyright (c) 2010 Lab X Technologies, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysbus.h"
#include "sysemu.h"
#include "net.h"

#define FIFO_RAM_BYTES 2048
#define LENGTH_FIFO_WORDS 16

struct labx_ethernet
{
    SysBusDevice busdev;
    qemu_irq hostIrq;
    qemu_irq fifoIrq;
    qemu_irq phyIrq;
    NICState *nic;
    NICConf conf;

    /* Device Configuration */
    uint32_t baseAddress;

    /* Values set by drivers */
    uint32_t hostRegs[0x10];
    uint32_t fifoRegs[0x10];

    /* Tx buffers */
    uint32_t* txBuffer;
    uint32_t txPushIndex;
    uint32_t txPopIndex;

    uint32_t* txLengthBuffer;
    uint32_t txLengthPushIndex;
    uint32_t txLengthPopIndex;

    /* Rx buffers */
    uint32_t* rxBuffer;
    uint32_t rxPushIndex;
    uint32_t rxPopIndex;

    uint32_t* rxLengthBuffer;
    uint32_t rxLengthPushIndex;
    uint32_t rxLengthPopIndex;
};

/*
 * Legacy ethernet registers
 */
static void update_host_irq(struct labx_ethernet *p)
{
    if ((p->hostRegs[0x03] & p->hostRegs[2]) != 0)
    {
        qemu_irq_raise(p->hostIrq);
    }
    else
    {
        qemu_irq_lower(p->hostIrq);
    }
}

static void mdio_xfer(struct labx_ethernet *p, int readWrite, int phyAddr, int regAddr)
{
    printf("MDIO %s: addr=%d, reg=%d\n", (readWrite) ? "READ" : "WRITE", phyAddr, regAddr);
    if (readWrite)
    {
        // TODO: PHY info
        p->hostRegs[0x01] = 0x0000FFFF;
    }
    p->hostRegs[0x03] |= 1;
    update_host_irq(p);
}

static uint32_t ethernet_regs_readl (void *opaque, target_phys_addr_t addr)
{
    struct labx_ethernet *p = opaque;

    uint32_t retval = 0;
   
    switch ((addr>>2) & 0x0F)
    {
    	case 0x00: // mdio control
        case 0x01: // mdio data
        case 0x02: // irq mask
        case 0x03: // irq flags
        case 0x04: // vlan mask
            retval = p->hostRegs[(addr>>2) & 0x0F];
            break;

        case 0x0F: // revision
            retval = 0x00000010;
            break;

        default:
            printf("labx-ethernet: Read of unknown register %08X\n", addr);
            break;
    }

    return retval;
}

static void ethernet_regs_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct labx_ethernet *p = opaque;

    switch ((addr>>2) & 0x0F)
    {
    	case 0x00: // mdio control
            p->hostRegs[0x00] = (value & 0x000007FF);
            mdio_xfer(p, (value >> 10) & 1, (value >> 5) & 0x1F, value & 0x1F);
            break;

        case 0x01: // mdio data
            p->hostRegs[0x01] = (value & 0x0000FFFF);
            break;

        case 0x02: // irq mask
            p->hostRegs[0x02] = (value & 0x00000003);
            update_host_irq(p);
            break;

        case 0x03: // irq flags
            p->hostRegs[0x03] &= ~(value & 0x00000003);
            update_host_irq(p);
            break;

        case 0x04: // vlan mask
            break;

        case 0x0F: // revision
            break;

        default:
            printf("labx-ethernet: Write of unknown register %08X = %08X\n", addr, value);
            break;
    }
}

static CPUReadMemoryFunc * const ethernet_regs_read[] = {
    NULL, NULL,
    &ethernet_regs_readl,
};

static CPUWriteMemoryFunc * const ethernet_regs_write[] = {
    NULL, NULL,
    &ethernet_regs_writel,
};


/*
 * MAC registers
 */
static uint32_t mac_regs_readl (void *opaque, target_phys_addr_t addr)
{
    //struct labx_ethernet *p = opaque;

    uint32_t retval = 0;
   
    switch ((addr>>2) & 0x0F)
    {
    	case 0x01: // host rx config
            break;

        case 0x02: // host tx config
            break;

        case 0x04: // host speed config
            break;

        case 0x05: // host mdio config
            break;

        default:
            printf("labx-ethernet: Read of unknown mac register %08X\n", addr);
            break;
    }

    return retval;
}

static void mac_regs_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    //struct labx_ethernet *p = opaque;

    switch ((addr>>2) & 0x0F)
    {
    	case 0x01: // host rx config
            break;

        case 0x02: // host tx config
            break;

        case 0x04: // host speed config
            break;

        case 0x05: // host mdio config
            break;

        default:
            printf("labx-ethernet: Write of unknown mac register %08X = %08X\n", addr, value);
            break;
    }
}

static CPUReadMemoryFunc * const mac_regs_read[] = {
    NULL, NULL,
    &mac_regs_readl,
};

static CPUWriteMemoryFunc * const mac_regs_write[] = {
    NULL, NULL,
    &mac_regs_writel,
};


/*
 * FIFO registers
 */

#define FIFO_INT_STATUS_ADDRESS   0x0
#define FIFO_INT_ENABLE_ADDRESS   0x1
#  define FIFO_INT_RPURE 0x80000000
#  define FIFO_INT_RPORE 0x40000000
#  define FIFO_INT_RPUE  0x20000000
#  define FIFO_INT_TPOE  0x10000000
#  define FIFO_INT_TC    0x08000000
#  define FIFO_INT_RC    0x04000000
#  define FIFO_INT_MASK  0xFC000000
#define FIFO_TX_RESET_ADDRESS     0x2
#  define FIFO_RESET_MAGIC 0xA5
#define FIFO_TX_VACANCY_ADDRESS   0x3
#define FIFO_TX_DATA_ADDRESS      0x4
#define FIFO_TX_LENGTH_ADDRESS    0x5
#define FIFO_RX_RESET_ADDRESS     0x6
#define FIFO_RX_OCCUPANCY_ADDRESS 0x7
#define FIFO_RX_DATA_ADDRESS      0x8
#define FIFO_RX_LENGTH_ADDRESS    0x9

static void update_fifo_irq(struct labx_ethernet *p)
{
    if ((p->fifoRegs[FIFO_INT_STATUS_ADDRESS] & p->fifoRegs[FIFO_INT_ENABLE_ADDRESS]) != 0)
    {
        qemu_irq_raise(p->fifoIrq);
    }
    else
    {
        qemu_irq_lower(p->fifoIrq);
    }
}

static void send_packet(struct labx_ethernet *p)
{
    while (p->txLengthPopIndex != p->txLengthPushIndex)
    {
	int i;
        uint32_t packetBuf[512];

        int length = p->txLengthBuffer[p->txLengthPopIndex];
        p->txLengthPopIndex = (p->txLengthPopIndex + 1) % LENGTH_FIFO_WORDS;

        for (i=0; i<((length+3)/4); i++)
        {
            packetBuf[i] = be32_to_cpu(p->txBuffer[p->txPopIndex]);
            p->txPopIndex = (p->txPopIndex + 1) % (FIFO_RAM_BYTES/4);
        }

        qemu_send_packet(&p->nic->nc, (void *)packetBuf, length);
    }

    p->fifoRegs[FIFO_INT_STATUS_ADDRESS] |= FIFO_INT_TC;
    update_fifo_irq(p);
}

static uint32_t fifo_regs_readl (void *opaque, target_phys_addr_t addr)
{
    struct labx_ethernet *p = opaque;

    uint32_t retval = 0;
   
    switch ((addr>>2) & 0x0F)
    {
    	case FIFO_INT_STATUS_ADDRESS:
        case FIFO_INT_ENABLE_ADDRESS:
        case FIFO_TX_RESET_ADDRESS:
            retval = p->fifoRegs[(addr>>2) & 0x0F];
            break;

        case FIFO_TX_VACANCY_ADDRESS:
            retval = (p->txPopIndex - p->txPushIndex) - 1;
            if ((int32_t)retval < 0)
            {
                retval += (FIFO_RAM_BYTES/4);
            }

            if (((p->txLengthPushIndex + 1) % LENGTH_FIFO_WORDS) == p->txLengthPopIndex)
            {
                // Full length fifo
                retval = 0;
            }
            break;

        case FIFO_TX_DATA_ADDRESS:
        case FIFO_TX_LENGTH_ADDRESS:
        case FIFO_RX_RESET_ADDRESS:
            retval = p->fifoRegs[(addr>>2) & 0x0F];
            break;

        case FIFO_RX_OCCUPANCY_ADDRESS:
            retval = p->rxPushIndex - p->rxPopIndex;
            if ((int32_t)retval < 0)
            {
                retval += (FIFO_RAM_BYTES/4);
            }
            break;

        case FIFO_RX_DATA_ADDRESS:
            retval = p->rxBuffer[p->rxPopIndex];
            if (p->rxPopIndex != p->rxPushIndex)
            {
                p->rxPopIndex = (p->rxPopIndex+1) % (FIFO_RAM_BYTES/4);
            }
            else
            {
                p->fifoRegs[FIFO_INT_STATUS_ADDRESS] |= FIFO_INT_RPURE;
                update_fifo_irq(p);
            }
            break;

        case FIFO_RX_LENGTH_ADDRESS:
            retval = p->rxLengthBuffer[p->rxLengthPopIndex];
            if (p->rxLengthPopIndex != p->rxLengthPushIndex)
            {
                p->rxLengthPopIndex = (p->rxLengthPopIndex+1) % LENGTH_FIFO_WORDS;
            }
            else
            {
                p->fifoRegs[FIFO_INT_STATUS_ADDRESS] |= FIFO_INT_RPURE;
                update_fifo_irq(p);
            }
            break;

        default:
            printf("labx-ethernet: Read of unknown fifo register %08X\n", addr);
            break;
    }

    // printf("FIFO REG READ %08X (%d) = %08X\n", addr, (addr>>2) & 0x0F, retval);

    return retval;
}

static void fifo_regs_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct labx_ethernet *p = opaque;

    // printf("FIFO REG WRITE %08X (%d) = %08X\n", addr, (addr>>2) & 0x0F, value);

    switch ((addr>>2) & 0x0F)
    {
    	case FIFO_INT_STATUS_ADDRESS:
            p->hostRegs[FIFO_INT_ENABLE_ADDRESS] &= ~(value & FIFO_INT_MASK);
            update_fifo_irq(p);
            break;

        case FIFO_INT_ENABLE_ADDRESS:
            p->fifoRegs[FIFO_INT_ENABLE_ADDRESS] = (value & FIFO_INT_MASK);
            update_fifo_irq(p);
            break;

        case FIFO_TX_RESET_ADDRESS:
            if (value == FIFO_RESET_MAGIC)
            {
                p->txPushIndex = 0;
                p->txPopIndex = 0;
                p->txLengthPushIndex = 0;
                p->txLengthPopIndex = 0;
            }
            break;

        case FIFO_TX_VACANCY_ADDRESS:
            break;

        case FIFO_TX_DATA_ADDRESS:
            if ((((p->txLengthPushIndex + 1) % LENGTH_FIFO_WORDS) == p->txLengthPopIndex) ||
                (((p->txPushIndex + 1) % (FIFO_RAM_BYTES/4)) == p->txPopIndex))
            {
                // Full length fifo or data fifo
                p->fifoRegs[FIFO_INT_STATUS_ADDRESS] |= FIFO_INT_TPOE;
                update_fifo_irq(p);
            }
            else
            {
                // Push back the data
                p->txBuffer[p->txPushIndex] = value;
                p->txPushIndex = (p->txPushIndex + 1) % (FIFO_RAM_BYTES/4);
            }
            break;

        case FIFO_TX_LENGTH_ADDRESS:
            if (((p->txLengthPushIndex + 1) % LENGTH_FIFO_WORDS) == p->txLengthPopIndex)
            {
                // Full length fifo
                p->fifoRegs[FIFO_INT_STATUS_ADDRESS] |= FIFO_INT_TPOE;
                update_fifo_irq(p);
            }
            else
            {
                // Push back the length
                p->txLengthBuffer[p->txLengthPushIndex] = value;
                p->txLengthPushIndex = (p->txLengthPushIndex + 1) % LENGTH_FIFO_WORDS;
                send_packet(p);
            }
            break;

        case FIFO_RX_RESET_ADDRESS:
            if (value == FIFO_RESET_MAGIC)
            {
                p->rxPushIndex = 0;
                p->rxPopIndex = 0;
                p->rxLengthPushIndex = 0;
                p->rxLengthPopIndex = 0;
            }
            break;

        case FIFO_RX_OCCUPANCY_ADDRESS:
            break;

        case FIFO_RX_DATA_ADDRESS:
            break;

        case FIFO_RX_LENGTH_ADDRESS:
            break;

        default:
            printf("labx-ethernet: Write of unknown fifo register %08X = %08X\n", addr, value);
            break;
    }
}

static CPUReadMemoryFunc * const fifo_regs_read[] = {
    NULL, NULL,
    &fifo_regs_readl,
};

static CPUWriteMemoryFunc * const fifo_regs_write[] = {
    NULL, NULL,
    &fifo_regs_writel,
};


static int eth_can_rx(VLANClientState *nc)
{
    //struct labx_ethernet *s = DO_UPCAST(NICState, nc, nc)->opaque;

    return 0;
}

static ssize_t eth_rx(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    // struct labx_ethernet *s = DO_UPCAST(NICState, nc, nc)->opaque;

    return -1;
}

static void eth_cleanup(VLANClientState *nc)
{
    struct labx_ethernet *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static NetClientInfo net_labx_ethernet_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
    .cleanup = eth_cleanup,
};

static int labx_ethernet_init(SysBusDevice *dev)
{
    struct labx_ethernet *p = FROM_SYSBUS(typeof (*p), dev);
    int ethernet_regs;
    int mac_regs;
    int fifo_regs;

    /* Initialize defaults */
    p->txBuffer = qemu_malloc(FIFO_RAM_BYTES);
    p->txLengthBuffer = qemu_malloc(LENGTH_FIFO_WORDS*4);
    p->rxBuffer = qemu_malloc(FIFO_RAM_BYTES);
    p->rxLengthBuffer = qemu_malloc(LENGTH_FIFO_WORDS*4);

    p->txPushIndex = 0;
    p->txPopIndex = 0;
    p->txLengthPushIndex = 0;
    p->txLengthPopIndex = 0;
    p->rxPushIndex = 0;
    p->rxPopIndex = 0;
    p->rxLengthPushIndex = 0;
    p->rxLengthPopIndex = 0;

    /* Set up memory regions */
    ethernet_regs = cpu_register_io_memory(ethernet_regs_read, ethernet_regs_write, p);
    mac_regs = cpu_register_io_memory(mac_regs_read, mac_regs_write, p);
    fifo_regs = cpu_register_io_memory(fifo_regs_read, fifo_regs_write, p);

    sysbus_init_mmio(dev, 0x10 * 4, ethernet_regs);
    sysbus_init_mmio(dev, 0x10 * 4, mac_regs);
    sysbus_init_mmio(dev, 0x10 * 4, fifo_regs);

    sysbus_mmio_map(dev, 0, p->baseAddress);
    sysbus_mmio_map(dev, 1, p->baseAddress + (1 << (10+2)));
    sysbus_mmio_map(dev, 2, p->baseAddress + (2 << (10+2)));

    /* Initialize the irqs */
    sysbus_init_irq(dev, &p->hostIrq);
    sysbus_init_irq(dev, &p->fifoIrq);
    sysbus_init_irq(dev, &p->phyIrq);

    /* Set up the NIC */
    qemu_macaddr_default_if_unset(&p->conf.macaddr);
    p->nic = qemu_new_nic(&net_labx_ethernet_info, &p->conf,
                          dev->qdev.info->name, dev->qdev.id, p);
    qemu_format_nic_info_str(&p->nic->nc, p->conf.macaddr.a);
    return 0;
}

static SysBusDeviceInfo labx_ethernet_info = {
    .init = labx_ethernet_init,
    .qdev.name  = "labx,ethernet",
    .qdev.size  = sizeof(struct labx_ethernet),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("baseAddress", struct labx_ethernet, baseAddress, 0),
        DEFINE_NIC_PROPERTIES(struct labx_ethernet, conf),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void labx_ethernet_register(void)
{
    sysbus_register_withprop(&labx_ethernet_info);
}

device_init(labx_ethernet_register)

