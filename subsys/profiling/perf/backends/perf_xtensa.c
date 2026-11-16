/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>

#include <xtensa_backtrace.h>

/*
 * The perf tracer runs in the timer interrupt, and on the other CPUs in the
 * IPI. While an interrupt is handled, the interrupt C handlers
 * (arch/xtensa/core/vector_handlers.c) keep the stack it interrupted in the
 * CPU's irq_frame, so the sampled context is the one that interrupt left.
 * The backtrace code spills the register windows and walks from there.
 */
int arch_perf_current_stack_trace(uintptr_t *buf, size_t size)
{
	int *frame = arch_curr_cpu()->arch.irq_frame;

	if (frame == NULL) {
		return -EINVAL;
	}

	return xtensa_backtrace_collect(frame, buf, size);
}
