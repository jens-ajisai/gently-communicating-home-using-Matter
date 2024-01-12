#include "util.h"

#include <zephyr/sys/sys_heap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
#include <platform/Zephyr/SysHeapMalloc.h>

extern struct sys_heap _system_heap;
#endif

void print_sys_memory_stats(void) {
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
  int err;
  struct sys_memory_stats kernel_stats;

  err = sys_heap_runtime_stats_get(&_system_heap, &kernel_stats);
  if (err) {
    LOG_INF("heap: failed to read kernel heap statistics, error: %d", err);
  } else {
    LOG_INF("kernel heap statistics:");
    LOG_INF("free:           %6d", kernel_stats.free_bytes);
    LOG_INF("allocated:      %6d", kernel_stats.allocated_bytes);
    LOG_INF("max. allocated: %6d\n", kernel_stats.max_allocated_bytes);
  }

  chip::DeviceLayer::Malloc::Stats stats;
  CHIP_ERROR chipErr = chip::DeviceLayer::Malloc::GetStats(stats);
  if(chipErr == CHIP_NO_ERROR) {
    LOG_INF("Matter heap statistics:");
    LOG_INF("free:           %6d", stats.free);
    LOG_INF("allocated:      %6d", stats.used);
    LOG_INF("max. allocated: %6d\n", stats.maxUsed);
  }

}
#endif /* CONFIG_SYS_HEAP_RUNTIME_STATS */
