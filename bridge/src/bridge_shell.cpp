/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/shell/shell.h>

#include "app_task.h"
#include "bridge/oob_exchange_manager.h"

static int init(const struct shell *shell, size_t argc, char **argv) {
  AppTask::InitBridge();
  return 0;
}

static int remove_bond(const struct shell *shell, size_t argc, char **argv) {
  bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
  return 0;
}

static int cmd_oob_remote(const struct shell *sh, size_t argc, char *argv[]) {
  OobExchangeManager::Instance().SetRemoteOob(argv[1], argv[2], argv[3], argv[4]);
  return 0;
}

static int cmd_oob_local(const struct shell *sh, size_t argc, char *argv[]) {
  OobExchangeManager::Instance().GetLocalOob();
  return 0;
}

#include <zephyr/sys/reboot.h>
static int reboot(const struct shell *shell, size_t argc, char **argv) {
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_matter_bridge, SHELL_CMD(init, NULL, "Inits the bridge.\n", init),
    SHELL_CMD(remove_bond, NULL, "Remove bondings.\n", remove_bond),
    SHELL_CMD(reboot, NULL, "System cold reboot.\n", reboot),
    SHELL_CMD_ARG(set_remote_oob, NULL, " <oob rand> <oob confirm>", cmd_oob_remote, 5, 0),
    SHELL_CMD(get_local_oob, NULL, " <oob rand> <oob confirm>", cmd_oob_local),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(matter_bridge, &sub_matter_bridge, "Matter bridge commands", NULL);
