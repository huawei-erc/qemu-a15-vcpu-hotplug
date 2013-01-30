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

#ifndef HW_VCPU_HP_H
#define HW_VCPU_HP_H

extern DeviceState *vcpu_hp_dev;

void vcpu_hp_reset(void *opaque);
void vcpu_hp_req_set(DeviceState *dev, int cpu_idx);
void vcpu_hp_req_clear(DeviceState *dev, int cpu_idx);
int vcpu_hp_req_fire(DeviceState *dev);
int vcpu_hp_req_pending(DeviceState *dev);
int vcpu_hp_resp_is_set(DeviceState *dev, int cpu_idx);

#endif /* HW_VCPU_HP_H */
