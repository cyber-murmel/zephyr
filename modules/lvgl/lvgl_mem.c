/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lvgl_mem.h"
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/sys_heap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(lvgl, CONFIG_LV_Z_LOG_LEVEL);

#ifdef CONFIG_LV_Z_MEMORY_POOL_CUSTOM_SECTION
#define HEAP_MEM_ATTRIBUTES Z_GENERIC_SECTION(.lvgl_heap) __aligned(8)
#elif defined(CONFIG_LV_Z_MEMORY_POOL_ZEPHYR_REGION)
#define HEAP_MEM_ATTRIBUTES \
	__attribute__((section(CONFIG_LV_Z_MEMORY_POOL_ZEPHYR_REGION_NAME))) __aligned(8)
#else
#define HEAP_MEM_ATTRIBUTES __aligned(8)
#endif /* CONFIG_LV_Z_MEMORY_POOL_CUSTOM_SECTION */
static char lvgl_heap_mem[CONFIG_LV_Z_MEM_POOL_SIZE] HEAP_MEM_ATTRIBUTES;

static struct sys_heap lvgl_heap;
static struct k_spinlock lvgl_heap_lock;

void lv_mem_init(void)
{
#ifdef CONFIG_LV_Z_MEM_POOL_SYS_HEAP
	lvgl_heap_init();
#endif /* CONFIG_LV_Z_MEM_POOL_SYS_HEAP */
}

void lv_mem_deinit(void)
{
	/* Reinitializing the heap clears all allocations, no action needed */
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
	memset(mon_p, 0, sizeof(lv_mem_monitor_t));

#if CONFIG_LV_Z_MEM_POOL_SYS_HEAP
	struct sys_memory_stats stats;

	lvgl_heap_stats(&stats);
	mon_p->used_pct =
		(stats.allocated_bytes * 100) / (stats.allocated_bytes + stats.free_bytes);
	mon_p->max_used = stats.max_allocated_bytes;
#else
	LOG_WRN_ONCE("Memory statistics only supported for CONFIG_LV_Z_MEM_POOL_SYS_HEAP");
#endif /* CONFIG_LV_Z_MEM_POOL_SYS_HEAP */
}

lv_result_t lv_mem_test_core(void)
{
	/* Not supported for now */
	return LV_RESULT_OK;
}

void * lv_mem_add_pool(void * mem, size_t bytes)
{
    /*Not supported*/
    ARG_UNUSED(mem);
    ARG_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(void * pool)
{
    /*Not supported*/
    ARG_UNUSED(pool);
    return;
}

void *lvgl_malloc(size_t size)
{
	k_spinlock_key_t key;
	void *ret;

	key = k_spin_lock(&lvgl_heap_lock);
	ret = sys_heap_alloc(&lvgl_heap, size);
	k_spin_unlock(&lvgl_heap_lock, key);

	return ret;
}

void *lvgl_realloc(void *ptr, size_t size)
{
	k_spinlock_key_t key;
	void *ret;

	key = k_spin_lock(&lvgl_heap_lock);
	ret = sys_heap_realloc(&lvgl_heap, ptr, size);
	k_spin_unlock(&lvgl_heap_lock, key);

	return ret;
}

void lvgl_free(void *ptr)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lvgl_heap_lock);
	sys_heap_free(&lvgl_heap, ptr);
	k_spin_unlock(&lvgl_heap_lock, key);
}

void lvgl_print_heap_info(bool dump_chunks)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lvgl_heap_lock);
	sys_heap_print_info(&lvgl_heap, dump_chunks);
	k_spin_unlock(&lvgl_heap_lock, key);
}

void lvgl_heap_stats(struct sys_memory_stats *stats)
{
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	k_spinlock_key_t key;

	key = k_spin_lock(&lvgl_heap_lock);
	sys_heap_runtime_stats_get(&lvgl_heap, stats);
	k_spin_unlock(&lvgl_heap_lock, key);
#else
	ARG_UNUSED(stats);
	LOG_WRN_ONCE("Enable CONFIG_SYS_HEAP_RUNTIME_STATS to use the mem monitor feature");
#endif /* CONFIG_SYS_HEAP_RUNTIME_STATS */
}

void lvgl_heap_init(void)
{
	sys_heap_init(&lvgl_heap, &lvgl_heap_mem[0], CONFIG_LV_Z_MEM_POOL_SIZE);
}
