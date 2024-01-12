#pragma once

#include <stdint.h>

#define ACK {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00}
#define CMD_CODE 0xd4
#define RES_CODE 0xd5

#define CMD_T4T_READ 0xB0

#define KEY_STR_LEN (33)
#define BT_ADDR_LE_STR_LEN (30)
#define OOB_MESSAGE_LEN (BT_ADDR_LE_STR_LEN + KEY_STR_LEN + KEY_STR_LEN + 1)

#define CMD_DIAGNOSE 0x00
#define CMD_GET_FIRMWARE_VERSION 0x02
#define CMD_GET_GENERAL_STATUS 0x04
#define CMD_READ_REGISTER 0x06
#define CMD_WRITE_REGISTER 0x08
#define CMD_READ_GPIO 0x0C
#define CMD_SET_SERIAL_BAUDRATE 0x10
#define CMD_SET_PARAMETERS 0x12
#define CMD_POWER_DOWN 0x16
#define CMD_RF_CONFIGURATION 0x32
#define CMD_RF_REGULATION_TEST 0x58
#define CMD_RESET_MODE 0x18
#define CMD_CONTROL_LED 0x1C
#define CMD_IN_JUMP_FOR_DEP 0x56
#define CMD_IN_JUMP_FOR_PSL 0x46
#define CMD_IN_LIST_PASSIVE_TARGET 0x4A
#define CMD_IN_ATR 0x50
#define CMD_IN_PSL 0x4E
#define CMD_IN_DATA_EXCHANGE 0x40
#define CMD_IN_COMMUNICATE_THRU 0x42
#define CMD_IN_DESELECT 0x44
#define CMD_IN_RELEASE 0x52
#define CMD_IN_SELECT 0x54
#define CMD_TG_INIT_TARGET 0x8C
#define CMD_TG_SET_GENERAL_BYTES 0x92
#define CMD_TG_GET_DEP_DATA 0x86
#define CMD_TG_SET_DEP_DATA 0x8E
#define CMD_TG_SET_META_DEP_DATA 0x94
#define CMD_TG_GET_INITIATOR_COMMAND 0x88
#define CMD_TG_RESPONSE_TO_INITIATOR 0x90
#define CMD_TG_GET_TARGET_STATUS 0x8A
#define CMD_COMMUNICATE_THRU_EX 0xA0

#define RES_DIAGNOSE (0x00 + 1)
#define RES_GET_FIRMWARE_VERSION (0x02 + 1)
#define RES_GET_GENERAL_STATUS (0x04 + 1)
#define RES_READ_REGISTER (0x06 + 1)
#define RES_WRITE_REGISTER (0x08 + 1)
#define RES_READ_GPIO (0x0C + 1)
#define RES_SET_SERIAL_BAUDRATE (0x10 + 1)
#define RES_SET_PARAMETERS (0x12 + 1)
#define RES_POWER_DOWN (0x16 + 1)
#define RES_RF_CONFIGURATION (0x32 + 1)
#define RES_RF_REGULATION_TEST (0x58 + 1)
#define RES_RESET_MODE (0x18 + 1)
#define RES_CONTROL_LED (0x1C + 1)
#define RES_IN_JUMP_FOR_DEP (0x56 + 1)
#define RES_IN_JUMP_FOR_PSL (0x46 + 1)
#define RES_IN_LIST_PASSIVE_TARGET (0x4A + 1)
#define RES_IN_ATR (0x50 + 1)
#define RES_IN_PSL (0x4E + 1)
#define RES_IN_DATA_EXCHANGE (0x40 + 1)
#define RES_IN_COMMUNICATE_THRU (0x42 + 1)
#define RES_IN_DESELECT (0x44 + 1)
#define RES_IN_RELEASE (0x52 + 1)
#define RES_IN_SELECT (0x54 + 1)
#define RES_TG_INIT_TARGET (0x8C + 1)
#define RES_TG_SET_GENERAL_BYTES (0x92 + 1)
#define RES_TG_GET_DEP_DATA (0x86 + 1)
#define RES_TG_SET_DEP_DATA (0x8E + 1)
#define RES_TG_SET_META_DEP_DATA (0x94 + 1)
#define RES_TG_GET_INITIATOR_COMMAND (0x88 + 1)
#define RES_TG_RESPONSE_TO_INITIATOR (0x90 + 1)
#define RES_TG_GET_TARGET_STATUS (0x8A + 1)
#define RES_COMMUNICATE_THRU_EX (0xA0 + 1)


uint16_t reset(uint8_t *buf, uint16_t buf_len);
uint16_t getFirmwareVersion(uint8_t *buf, uint16_t buf_len);
uint16_t configureWaitTime(uint8_t *buf, uint16_t buf_len);
uint16_t listenPassiveTargetFelica(uint8_t *buf, uint16_t buf_len);
uint16_t listenPassiveTargetNfcTypeA(uint8_t *buf, uint16_t buf_len);
uint16_t listenPassiveTargetNfcTypeB(uint8_t *buf, uint16_t buf_len);
uint16_t readDataNfcTypeA(uint8_t *buf, uint16_t buf_len);
uint16_t rfOff(uint8_t *buf, uint16_t buf_len);

void parseNfcNdefLeOob(uint8_t *buf, uint16_t buf_len, char *buf_out, uint16_t buf_out_len);