/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic.h>

/* Serializes every emulated compare-and-set; in internal memory, outside the range. */
static atomic_t emulated_lock;

atomic_val_t xtensa_cas(atomic_t *addr, atomic_val_t oldval, atomic_val_t newval)
{
	unsigned int key;
	atomic_val_t cur;

	if ((uintptr_t)addr - CONFIG_XTENSA_ATOMIC_EMULATED_START >=
	    CONFIG_XTENSA_ATOMIC_EMULATED_END - CONFIG_XTENSA_ATOMIC_EMULATED_START) {
		return xtensa_cas_s32c1i(addr, oldval, newval);
	}

	__ASSERT_NO_MSG((uintptr_t)&emulated_lock - CONFIG_XTENSA_ATOMIC_EMULATED_START >=
			CONFIG_XTENSA_ATOMIC_EMULATED_END - CONFIG_XTENSA_ATOMIC_EMULATED_START);
	key = arch_irq_lock();
	while (xtensa_cas_s32c1i(&emulated_lock, 0, 1) != 0) {
	}
	cur = *(volatile atomic_t *)addr;
	if (cur == oldval) {
		*(volatile atomic_t *)addr = newval;
	}
	/* Release the lock after the stores above. */
	__asm__ volatile("s32ri %0, %1, 0" : : "r"(0), "r"(&emulated_lock) : "memory");
	arch_irq_unlock(key);

	return cur;
}
