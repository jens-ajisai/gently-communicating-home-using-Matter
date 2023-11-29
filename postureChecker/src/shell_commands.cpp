#ifdef CONFIG_SHELL

#include <shell/shell.h>
#include <stdio.h>

#include "bt.h"
#include "persistence/persistence.h"
#include "sensors/sensorDataDefinition.h"

#if defined(CONFIG_MEMFAULT)
#include <memfault/core/trace_event.h>
#include <memfault/metrics/metrics.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(shell, CONFIG_MAIN_LOG_LEVEL);

static int echo(const struct shell *shell, size_t argc, char **argv) {
  shell_print(shell, "argc = %d", argc);
  for (size_t cnt = 0; cnt < argc; cnt++) {
    shell_print(shell, "  argv[%d] = %s", cnt, argv[cnt]);
  }

  return 0;
}

#include <sys/reboot.h>
static int reboot(const struct shell *shell, size_t argc, char **argv) {
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

static int pairing(const struct shell *shell, size_t argc, char **argv) {
  advertise_without_filter();
  return 0;
}

static int remove_bond(const struct shell *shell, size_t argc, char **argv) {
  remove_bonding();
  return 0;
}

#if defined(CONFIG_DATE_TIME)
#include <date_time.h>
#include <stdlib.h>

static int date_get(const struct shell *shell, size_t argc, char **argv) {
  int64_t t = 0;
  date_time_now(&t);
  t /= 1000;

  struct tm *tm_localtime;
  tm_localtime = localtime(&t);
  shell_print(shell, "%s unix time: %lli", asctime(tm_localtime), t);

  return 0;
}

static int date_set(const struct shell *shell, size_t argc, char **argv) {
  struct tm *tm_localtime;
  char *eptr;
  time_t t = strtoll(argv[1], &eptr, strlen(argv[1]));

  tm_localtime = localtime(&t);

  date_time_set(tm_localtime);
  shell_print(shell, "Set time to %s (%lli)", asctime(tm_localtime), t);
  return 0;
}
#endif

static int delete_file(const struct shell *shell, size_t argc, char **argv) {
  Persistence::Instance().init(false);
  Persistence::Instance().deleteFile(argv[1]);
  return 0;
}

#define DATA_BUF_SIZE CONFIG_ML_APP_EI_DATA_FORWARDER_BUF_SIZE

void fs_read_cb(void *data, uint16_t len_read, void *ctx) {
  static int idx = 0;
  struct posture_data *d = (struct posture_data *)data;
  static char buf[DATA_BUF_SIZE];

  snprintf(buf, DATA_BUF_SIZE, "%u,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", d->time,
           d->flex1, d->flex2, d->flex3, d->roll, d->pitch, d->yaw, d->accel_x, d->accel_y,
           d->accel_z, d->magn_x, d->magn_y, d->magn_z);

  LOG_INF("#%6d: %s", idx, buf);

  idx++;
}

static int dump_file(const struct shell *shell, size_t argc, char **argv) {
  static uint8_t buf[sizeof(struct posture_data)];
  Persistence::Instance().init(false);
  Persistence::Instance().readFile(argv[1], buf, sizeof(buf), fs_read_cb, NULL);
  return 0;
}

#if defined(CONFIG_MEMFAULT)
static int memfault_test(const struct shell *shell, size_t argc, char **argv) {
  memfault_metrics_heartbeat_timer_start(MEMFAULT_METRICS_KEY(testTimerMetric));
  memfault_metrics_heartbeat_add(MEMFAULT_METRICS_KEY(testMetric), 1);
  MEMFAULT_TRACE_EVENT_WITH_LOG(testTraceReason, "apply_config:");
  memfault_metrics_heartbeat_timer_stop(MEMFAULT_METRICS_KEY(testTimerMetric));
  memfault_metrics_heartbeat_debug_trigger();
  return 0;
}
#endif

static int cmd_oob_remote(const struct shell *sh, size_t argc, char *argv[]) {
  set_remote_oob(argv[1], argv[2], argv[3], argv[4]);
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_appCtrl, SHELL_CMD(echo, NULL, "Echo back the arguments", echo),
    SHELL_CMD(reboot, NULL, "System cold reboot", reboot),
    SHELL_CMD(pairing, NULL, "Start pairing mode", pairing),
    SHELL_CMD(remove_bond, NULL, "Start pairing mode", remove_bond),
    SHELL_CMD_ARG(delete_file, NULL, "Deletes the given file", delete_file, 2, 0),
    SHELL_CMD_ARG(dump_file, NULL, "Dumps the given file", dump_file, 2, 0),
#if defined(CONFIG_DATE_TIME)
    SHELL_CMD(date_get, NULL, "Get the date by asctime", date_get),
    SHELL_CMD_ARG(date_set, NULL, "Set the date by unix time format as argv", date_set, 2, 0),
#endif
#if defined(CONFIG_MEMFAULT)
    SHELL_CMD(memfault_test, NULL, "Calling some memfault stuff", memfault_test),
#endif
    SHELL_CMD_ARG(set_remote_oob, NULL, " <oob rand> <oob confirm>", cmd_oob_remote, 5, 0),

    SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(appCtrl, &sub_appCtrl, "Control posture checker", NULL);

#endif
