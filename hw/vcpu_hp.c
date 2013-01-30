/* Paravirtualization VCPU Hotplug device
 *
 * Copyright (c) 2012 Huawei Technologies Duesseldorf GmbH
 * Written by Claudio Fontana
 *
 * This code is licensed under the GPLv2: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "sysbus.h"
#include "bitops.h"
#include "vcpu_hp.h"

#include "sysemu.h"

/* VCPU HP header bytes offsets */
enum vcpu_hp_header {
    VCPU_HP_HEADER_MASK_SZ = 0,
    VCPU_HP_HEADER_CTRL = 1,

    VCPU_HP_HEADER_RES2 = 2,
    VCPU_HP_HEADER_RES3 = 3,
    VCPU_HP_HEADER_RES4 = 4,
    VCPU_HP_HEADER_RES5 = 5,
    VCPU_HP_HEADER_RES6 = 6,
    VCPU_HP_HEADER_RES7 = 7,

    VCPU_HP_HEADER_N
};

/* VCPU HP control byte bit offsets */
/* note that a Hotplug is pending just before firing the IRQ,
   up until the hotplug is completed in the guest and confirmed.
   The Interrupt is Pending just until the guest reaches the ISR,
   at which point it will acknowledge by writing 0 to the IPR. */
enum vcpu_hp_ctrl {
    VCPU_HP_CTRL_IPR = 0, /* Interrupt Pending Register */
    VCPU_HP_CTRL_HPR = 1, /* Hotplug Pending Register */

    VCPU_HP_CTRL_RES2 = 2,
    VCPU_HP_CTRL_RES3 = 3,
    VCPU_HP_CTRL_RES4 = 4,
    VCPU_HP_CTRL_RES5 = 5,
    VCPU_HP_CTRL_RES6 = 6,
    VCPU_HP_CTRL_RES7 = 7,

    VCPU_HP_CTRL_N
};
/* CTRLreg helpers */
static int vcpu_hp_get_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    return *ctrl & (1 << reg) ? 1 : 0;
}

static void vcpu_hp_set_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    *ctrl |= (1 << reg);
}

static void vcpu_hp_clear_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    *ctrl &= ~(1 << reg);
}

DeviceState *vcpu_hp_dev;

/* binary MMIO interface for VCPU Hotplug */
/*
   offset (bytes)
     0            VCPU Mask size in bytes (VMS)
     1            Control Byte Registers (Creg)
     2-7          reserved

     8            VCPU Request Mask
     8+VMS        VCPU Response Mask
*/

/* Creg bits */
/*
 | 7   6   5   4   3   2 | 1 | 0
 |   reserved            |HPR|IPR
*/

struct vcpu_hp_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    int mask_sz;

    unsigned char *vcpu_mask_req;
    unsigned char *vcpu_mask_resp;

    unsigned char ctrl;
    qemu_irq irq;
};

static struct vcpu_hp_state *vcpu_hp_get_state(DeviceState *dev)
{
    if (!dev) {
        return NULL;
    }
    return FROM_SYSBUS(struct vcpu_hp_state, SYS_BUS_DEVICE(dev));
}

/* Mask byte/bit offset helpers */
static int vcpu_hp_get_mask_offset(struct vcpu_hp_state *s, int cpu_idx,
                                   unsigned int *off_byte,
                                   unsigned int *off_bit)
{
    *off_bit = cpu_idx % BITS_PER_BYTE;
    return (*off_byte = cpu_idx / BITS_PER_BYTE) < s->mask_sz;
}

/* add a cpu to the request online mask */
void vcpu_hp_req_set(DeviceState *dev, int cpu_idx)
{
    struct vcpu_hp_state *s; unsigned int off_byte, off_bit;
    if (!(s = vcpu_hp_get_state(dev))) {
        return;
    }
    if (!vcpu_hp_get_mask_offset(s, cpu_idx, &off_byte, &off_bit)) {
        return;
    }
    s->vcpu_mask_req[off_byte] |= 1 << off_bit;
}

/* test a cpu bit in the response */
int vcpu_hp_resp_is_set(DeviceState *dev, int cpu_idx)
{
    struct vcpu_hp_state *s; unsigned int off_byte, off_bit;
    if (!(s = vcpu_hp_get_state(dev))) {
        return 0;
    }
    if (!vcpu_hp_get_mask_offset(s, cpu_idx, &off_byte, &off_bit)) {
        return 0;
    }
    return !!(s->vcpu_mask_req[off_byte] & (1 << off_bit));
}

/* remove a cpu from the request online mask */
void vcpu_hp_req_clear(DeviceState *dev, int cpu_idx)
{
    struct vcpu_hp_state *s; unsigned int off_byte, off_bit;
    if (!(s = vcpu_hp_get_state(dev))) {
        return;
    }
    if (!vcpu_hp_get_mask_offset(s, cpu_idx, &off_byte, &off_bit)) {
        return;
    }
    s->vcpu_mask_req[off_byte] &= ~(1 << off_bit);
}

int vcpu_hp_req_fire(DeviceState *dev)
{
    struct vcpu_hp_state *s;
    if (!(s = vcpu_hp_get_state(dev))) {
        return 0;
    }
    if (vcpu_hp_req_pending(dev)) {
        fprintf(stderr, "vcpu_hp: %s.\n",
                "cannot fire, previous hotplug still pending");
        return 0;
    }

    fprintf(stderr, "vcpu_hp: %s.\n", "firing hotplug request");
    vcpu_hp_set_creg(&s->ctrl, VCPU_HP_CTRL_HPR);
    vcpu_hp_set_creg(&s->ctrl, VCPU_HP_CTRL_IPR);

    qemu_set_irq(s->irq, 1);
    return 1;
}

int vcpu_hp_req_pending(DeviceState *dev)
{
    struct vcpu_hp_state *s;
    if (!(s = vcpu_hp_get_state(dev))) {
        return 0;
    }
    return vcpu_hp_get_creg(&s->ctrl, VCPU_HP_CTRL_HPR);
}

void vcpu_hp_reset(void *opaque)
{
    struct vcpu_hp_state *s = opaque;
    memset(s->vcpu_mask_req, 0, s->mask_sz);
    memset(s->vcpu_mask_resp, 0, s->mask_sz);

    /* by default, the req is to have only CPU0 running */
    s->vcpu_mask_req[0] = 0x01;
    s->ctrl = 0x00;
}

static uint64_t vcpu_hp_read_header(struct vcpu_hp_state *s, hwaddr offset)
{
    switch (offset) {
    case VCPU_HP_HEADER_MASK_SZ:
        fprintf(stderr, "vcpu_hp: guest reading HEADER_MASK_SZ.\n");
        return s->mask_sz;

    case VCPU_HP_HEADER_CTRL:
        fprintf(stderr, "vcpu_hp: guest reading HEADER_CTRL.\n");
        return s->ctrl;

    default:
        break;
    }
    return 0;
}

static uint64_t vcpu_hp_read(void *opaque, hwaddr offset, unsigned size)
{
    struct vcpu_hp_state *s; unsigned char *mask;

    assert(size == 1);
    s = opaque;

    if (offset < VCPU_HP_HEADER_N) {
        return vcpu_hp_read_header(s, offset);
    }
    offset -= VCPU_HP_HEADER_N;

    if (offset < s->mask_sz) {
        mask = s->vcpu_mask_req;
        fprintf(stderr, "vcpu_hp: guest READ of vcpu_mask_req[%u].\n",
                (unsigned int)offset);

    } else if (offset < s->mask_sz * 2) {
        offset -= s->mask_sz;
        mask = s->vcpu_mask_resp;
        fprintf(stderr, "vcpu_hp: guest READ of vcpu_mask_resp[%u].\n",
                (unsigned int)offset);

    } else {
        /* outside of the allowed range */
        hw_error("vcpu_hp: guest wild READ.\n");
        return 0;
    }

    return mask[offset];
}

static void vcpu_hp_write_ctrl(struct vcpu_hp_state *s, uint64_t value)
{
    unsigned char newctrl; newctrl = value;

    if (vcpu_hp_get_creg(&s->ctrl, VCPU_HP_CTRL_IPR) &&
        !vcpu_hp_get_creg(&newctrl, VCPU_HP_CTRL_IPR)) {

        fprintf(stderr, "vcpu_hp: guest clearing IPR.\n");
        vcpu_hp_clear_creg(&s->ctrl, VCPU_HP_CTRL_IPR);
        qemu_set_irq(s->irq, 0);
    }

    if (vcpu_hp_get_creg(&s->ctrl, VCPU_HP_CTRL_HPR) &&
        !vcpu_hp_get_creg(&newctrl, VCPU_HP_CTRL_HPR)) {

        fprintf(stderr, "vcpu_hp: guest clearing HPR.\n");
        vcpu_hp_clear_creg(&s->ctrl, VCPU_HP_CTRL_HPR);
        qemu_vcpu_hp_request(); /* request handling of hotplug completion */
    }
}

static void vcpu_hp_write_header(struct vcpu_hp_state *s, hwaddr offset, uint64_t value)
{
    switch (offset) {
    case VCPU_HP_HEADER_MASK_SZ:
        break;
    case VCPU_HP_HEADER_CTRL:
        vcpu_hp_write_ctrl(s, value);
        return;
    }
    hw_error("vcpu_hp: guest wild WRITE of header.\n");
}

static void vcpu_hp_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    struct vcpu_hp_state *s; unsigned char *mask;

    assert(size == 1);
    s = opaque;

    if (offset < VCPU_HP_HEADER_N) {
        vcpu_hp_write_header(s, offset, value);
        return;
    }
    offset -= VCPU_HP_HEADER_N;

    if (offset < s->mask_sz) {
        /* this area is read-only for the guest! */
        hw_error("vcpu_hp: guest wild WRITE of vcpu_mask_req.\n");
        return;

    } else if (offset < s->mask_sz * 2) {
        offset -= s->mask_sz;
        mask = s->vcpu_mask_resp;
        fprintf(stderr, "vcpu_hp: guest WRITE of vcpu_mask_resp[%u].\n",
                (unsigned int)offset);

    } else {
        /* outside of the allowed range */
        hw_error("vcpu_hp: guest wild WRITE.\n");
        return;
    }

    mask[offset] = value;
}

static const MemoryRegionOps vcpu_hp_ops = {
    .read = vcpu_hp_read,
    .write = vcpu_hp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,

    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .valid.unaligned = TRUE,

    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static int vcpu_hp_init(SysBusDevice *dev)
{
    struct vcpu_hp_state *s = FROM_SYSBUS(struct vcpu_hp_state, dev);

    memory_region_init_io(&s->iomem, &vcpu_hp_ops, s, "vcpu_hp", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    vcpu_hp_dev = &dev->qdev;

    s->mask_sz = max_cpus / BITS_PER_BYTE + (max_cpus % BITS_PER_BYTE ? 1 : 0);
    s->vcpu_mask_req = g_malloc(s->mask_sz);
    s->vcpu_mask_resp = g_malloc(s->mask_sz);

    vcpu_hp_reset(s);
    qemu_register_reset(vcpu_hp_reset, s);

    /* XXX todo ???
       vmstate_register(&dev->qdev, -1, &vmstate_vcpu_hp, s); */

    return 0;
}

static void vcpu_hp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);
    sdc->init = vcpu_hp_init;
}

static TypeInfo vcpu_hp_info = {
    .name          = "vcpu_hp",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct vcpu_hp_state),
    .class_init    = vcpu_hp_class_init,
};

static void vcpu_hp_register_types(void)
{
    type_register_static(&vcpu_hp_info);
}

type_init(vcpu_hp_register_types)
