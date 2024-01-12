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
	shell_print(shell, " *** Please note that if being connected, this will crash and reboot. This is OK. *** ");  
  bt_unpair(BT_ID_DEFAULT, NULL);
  return 0;
}

static int cmd_oob_remote(const struct shell *shell, size_t argc, char *argv[]) {
  OobExchangeManager::Instance().SetRemoteOob(argv[1], argv[2], argv[3], argv[4]);
  return 0;
}

static int cmd_oob_local(const struct shell *shell, size_t argc, char *argv[]) {
  OobExchangeManager::Instance().GetLocalOob();
  return 0;
}

#include <zephyr/sys/reboot.h>
static int reboot(const struct shell *shell, size_t argc, char **argv) {
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

#include "reminders/reminders_app.h"
#include "display.h"
static int cmd_reminder_init(const struct shell *shell, size_t argc, char **argv) {
  initRemindersApp(display_updateTranscription, display_updateCompletions);
  return 0;
}

static int cmd_reminder_add(const struct shell *shell, size_t argc, char **argv) {
  addReminder(argv[1], argv[2], argc == 4 && strcmp(argv[3], "true") == 0);
  return 0;
}

static int cmd_reminder_delete(const struct shell *shell, size_t argc, char **argv) {
  deleteReminder(argv[1]);
  return 0;
}

static int cmd_reminder_check(const struct shell *shell, size_t argc, char **argv) {
  checkdue();
  return 0;
}

static int cmd_reminder_record(const struct shell *shell, size_t argc, char **argv) {
  startAiFlow();
  return 0;
}

static int cmd_reminder_print(const struct shell *shell, size_t argc, char **argv) {
  printReminders();
  return 0;
}

static int cmd_reminder_stopRecord(const struct shell *shell, size_t argc, char **argv) {
  stopRecording();
  return 0;
}

static int cmd_reminder_feedback_text(const struct shell *shell, size_t argc, char **argv) {
  display_updateTranscription(argv[1]);
  display_updateCompletions(argv[1]);
  return 0;
}

#include "reminders/reminder.h"
#include "reminders/ai/completions.h"
static int cmd_reminder_completions(const struct shell *shell, size_t argc, char **argv) {
  const char *request[3] = {reminder_util_currentDateTime(), reminder_printJson(), argv[1]};
  request_chat_completion(request);
  return 0;
}

#include "util.h"
static int memory_stats(const struct shell *shell, size_t argc, char **argv) {
  print_sys_memory_stats();
  return 0;
}

#include "reminders/persistence/persistence.h"
static int delete_file(const struct shell *shell, size_t argc, char **argv) {
  fs_init(false);
  fs_deleteFile(argv[1]);
  return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_matter_bridge, 
    SHELL_CMD(memory_stats, NULL, "Inits the bridge.", memory_stats),
    SHELL_CMD(init, NULL, "Inits the bridge.", init),
    SHELL_CMD(remove_bond, NULL, "Remove bondings.", remove_bond),
    SHELL_CMD(reboot, NULL, "System cold reboot.", reboot),
    SHELL_CMD_ARG(set_remote_oob, NULL, " <oob rand> <oob confirm>", cmd_oob_remote, 5, 0),
    SHELL_CMD(get_local_oob, NULL, " <oob rand> <oob confirm>", cmd_oob_local),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_reminders, 
    SHELL_CMD(init, NULL, "Init reminders", cmd_reminder_init),
    SHELL_CMD_ARG(add, NULL, "Add reminder. <name> <due in MM-DD hh:mm> [<daily>]", cmd_reminder_add, 3, 1),
    SHELL_CMD_ARG(delete, NULL, "Delete reminder. <name>", cmd_reminder_delete, 2, 0),
    SHELL_CMD(check, NULL, "Check reminders' due.", cmd_reminder_check),
    SHELL_CMD(print, NULL, "Print all reminders.", cmd_reminder_print),
    SHELL_CMD(record, NULL, "Start recording for AI.", cmd_reminder_record),
    SHELL_CMD(stopRecord, NULL, "Stop recording for AI.", cmd_reminder_stopRecord),
    SHELL_CMD_ARG(feedback_text, NULL, "Draw text to display.", cmd_reminder_feedback_text, 2, 0),
    SHELL_CMD_ARG(completions, NULL, "Print all reminders.", cmd_reminder_completions, 2, 0),
    SHELL_CMD_ARG(delete_file, NULL, "Deletes the given file", delete_file, 2, 0),    
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(matter_bridge, &sub_matter_bridge, "Matter bridge commands", NULL);
SHELL_CMD_REGISTER(reminders, &sub_reminders, "Reminders commands", NULL);
