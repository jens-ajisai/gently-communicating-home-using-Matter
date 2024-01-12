#include "nfc_helper.h"

#include <stdio.h>
#include <string.h>

#include "logging.h"

static uint16_t command(uint8_t cmd_code, uint8_t *cmd_data, uint16_t cmd_data_len, uint8_t *buf,
                        uint16_t buf_len) {
  if (cmd_data_len > 253) {  // 0xFF - 2 for CMD_CODE and cmd_code
    return 0;
  }
  if (buf_len < (cmd_data_len + 9)) {
    return 0;
  }

  uint16_t data_sum = CMD_CODE + cmd_code;
  for (int i = 0; i < cmd_data_len; i++) {
    data_sum = data_sum + cmd_data[i];
  }
  uint8_t data_checksum = (uint8_t)(0x100 - (data_sum % 0x100));

  // Start of frame
  buf[0] = 0x00;               // preample
  buf[1] = 0x00;               // start of packet
  buf[2] = 0xff;               // start of packet
  buf[3] = cmd_data_len + 2;   // data length + 2 for CMD_CODE and cmd_code
  buf[4] = (0x0100 - buf[3]);  // length checksum
  buf[5] = CMD_CODE;           // command
  buf[6] = cmd_code;           // sub command

  if (cmd_data_len > 0) memcpy(buf + 7, cmd_data, cmd_data_len);

  buf[7 + cmd_data_len] = data_checksum;  // data checksum
  buf[7 + cmd_data_len + 1] = 0;

  return 7 + cmd_data_len + 2;
}

uint16_t reset(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x01};
  return command(CMD_RESET_MODE, data, sizeof(data), buf, buf_len);
}

uint16_t getFirmwareVersion(uint8_t *buf, uint16_t buf_len) {
  return command(CMD_GET_FIRMWARE_VERSION, NULL, 0, buf, buf_len);
}

uint16_t configureWaitTime(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x81, 0xb7};
  return command(CMD_RF_CONFIGURATION, data, sizeof(data), buf, buf_len);
}

uint16_t listenPassiveTargetFelica(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x01, 0x01, 0x00, 0xff, 0xff, 0x00, 0x00};
  return command(CMD_IN_LIST_PASSIVE_TARGET, data, sizeof(data), buf, buf_len);
}

uint16_t listenPassiveTargetNfcTypeA(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x01, 0x00};
  return command(CMD_IN_LIST_PASSIVE_TARGET, data, sizeof(data), buf, buf_len);
}

uint16_t listenPassiveTargetNfcTypeB(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x01, 0x03, 0x00};
  return command(CMD_IN_LIST_PASSIVE_TARGET, data, sizeof(data), buf, buf_len);
}

uint16_t readDataNfcTypeA(uint8_t *buf, uint16_t buf_len) {
    uint8_t data[] = {0x01, 0x00, CMD_T4T_READ, /* "page" */ 0x00, 0x00, /* size*/ 0xF1};
    return command(CMD_IN_DATA_EXCHANGE, data, sizeof(data), buf, buf_len);
}

uint16_t rfOff(uint8_t *buf, uint16_t buf_len) {
  uint8_t data[] = {0x01, 0x00};
  return command(CMD_RF_CONFIGURATION, data, sizeof(data), buf, buf_len);
}


// functions hex2char and bin2hex copied from zephyr/lib/os/hex.c
int hex2char(uint8_t x, char *c)
{
	if (x <= 9) {
		*c = x + '0';
	} else  if (x <= 15) {
		*c = x - 10 + 'a';
	} else {
		return -1;
	}

	return 0;
}

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen)
{
	if (hexlen < (buflen * 2 + 1)) {
		return 0;
	}

	for (size_t i = 0; i < buflen; i++) {
		if (hex2char(buf[i] >> 4, &hex[2 * i]) < 0) {
			return 0;
		}
		if (hex2char(buf[i] & 0xf, &hex[2 * i + 1]) < 0) {
			return 0;
		}
	}

	hex[2 * buflen] = '\0';
	return 2 * buflen;
}

void parseNfcNdefLeOob(uint8_t *buf, uint16_t buf_len, char *buf_out, uint16_t buf_out_len) {
  // 55:DF:EB:5A:DD:03 (random) f622b2bcfc33ed379187d66cf474f94b 294d321270731bb6329acc3f564ae967

  char address[30] = {0};
  char type[10] = {0};
  char confirm[33] = {0};
  char rand[33] = {0};

  if(buf_len < 0x89) return;

  if(buf[0x4F] == 0x01) {
    strncpy(type, "random", sizeof(type) - 1);
  }
  snprintf(address, sizeof(address) - 1, "%02X:%02X:%02X:%02X:%02X:%02X (%s)",
        buf[0x4E], buf[0x4D], buf[0x4C],
        buf[0x4B], buf[0x4A], buf[0x49], type);

  bin2hex(&buf[0x67], 0x10, confirm, sizeof(confirm));
  bin2hex(&buf[0x79], 0x10, rand, sizeof(rand));

  if(buf_out_len < (sizeof(address) + sizeof(confirm) + sizeof(rand))) {
    return;
  }

  snprintf(buf_out, buf_out_len, "%s %32s %32s", address, rand, confirm);
}

/*
// Log of the sender

[00:00:00.990,509] <inf> nfc_ndef_le_oob_rec_parser: NDEF LE OOB Record payload
[00:00:00.990,722] <inf> nfc_ndef_le_oob_rec_parser: .Device Address:.47:18:24:BC:0B:4C (random)
[00:00:00.990,753] <inf> nfc_ndef_le_oob_rec_parser: .Local Name:.Posture
[00:00:00.990,783] <inf> nfc_ndef_le_oob_rec_parser: .LE Role:.Only Peripheral Role supported
[00:00:00.990,783] <inf> nfc_ndef_le_oob_rec_parser: :..0x04
[00:00:00.990,844] <inf> nfc_ndef_le_oob_rec_parser: .TK Value:.Appearance:.0
[00:00:00.990,814] <inf> nfc_ndef_le_oob_rec_parser: .Flags
                                                     00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 |........ ........
[00:00:00.990,875] <inf> nfc_ndef_le_oob_rec_parser: .LE SC Confirm Value:
                                                     60 0d 4b 8b 62 7c 84 1c  9d fe 00 ab 5d 49 d5 3d |`.K.b|.. ....]I.=
[00:00:00.990,905] <inf> nfc_ndef_le_oob_rec_parser: .LE SC Random Value:
                                                     57 2c 27 0e 2c a8 c8 ef  dd 02 64 81 0c 59 97 7d |W,'.,... ..d..Y.}

Received data
  00 00 FF F6 0A D5 41 00 00 B1 81 02 00 00 00 0D
  48 73 15 C1 02 00 00 00 04 61 63 01 01 30 00 0A
  20 00 00 00 52 01 61 70 70 6C 69 63 61 74 69 6F
  6E 2F 76 6E 64 2E 62 6C 75 65 74 6F 6F 74 68 2E
  6C 65 2E 6F 6F 62 30 08 1B 4C 0B BC 24 18 47 01
  02 1C 00 11 10 00 00 00 00 00 00 00 00 00 00 00
  00 00 00 00 00 11 22 60 0D 4B 8B 62 7C 84 1C 9D
  FE 00 AB 5D 49 D5 3D 11 23 57 2C 27 0E 2C A8 C8
  EF DD 02 64 81 0C 59 97 7D 03 19 00 00 02 01 04
  08 09 50 6F 73 74 75 72 65 41 02 00 00 00 1A 54
  70 10 13 75 72 6E 3A 6E 66 63 3A 73 6E 3A 68 61
  6E 64 6F 76 65 72 00 2C 02 04 00 00 00 00 00 00


Analysis with
[ndef.handover.HandoverSelectRecord('1.5', None, ('active', '0')), 
ndef.bluetooth.BluetoothLowEnergyRecord(
 (0x1B, b'L\x0b\xbc$\x18G\x01'),
 (0x1C, b'\x00'),
 (0x10, b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'),
 (0x22, b'`\rK\x8bb|\x84\x1c\x9d\xfe\x00\xab]I\xd5='),
 (0x23, b"W,'\x0e,\xa8\xc8\xef\xdd\x02d\x81\x0cY\x97}"),
 (0x19, b'\x00\x00'),
 (0x01, b'\x04'),
 (0x09, b'Posture')), 
 ndef.record.Record('urn:nfc:wkt:Tp', '', bytearray(b'\x10\x13urn:nfc:sn:handover\x00,\x02\x04\x00'))]

NDEF Handover Select Record ID '' Version '1.5' Carrier Reference '0' Power State 'active' Auxiliary Data []
NDEF Bluetooth Low Energy Record ID '0' Attributes 0x01 0x09 0x10 0x19 0x1B 0x1C 0x22 0x23
NDEF Record TYPE 'urn:nfc:wkt:Tp' ID '' PAYLOAD 26 byte '101375726e3a6e66633a' ... 16 more


https://github.com/xueliu/nRF52/blob/master/nRF52_SDK_0.9.1_3639cc9/components/libraries/experimental_nfc/connection_handover/nfc_ble_pair_msg.c
https://nfc-forum.org/build/assigned-numbers
https://infocenter.nordicsemi.com/index.jsp?topic=%2Fsdk_nrf5_v16.0.0%2Fgroup__nfc__ac__rec.html
https://github.com/nfcpy/ndeflib/blob/2afb429d7f8cd98d9f673f4ea9f2cbbc8b5795c7/src/ndef/bluetooth.py#L592

00 00 FF        Preample
F6              Length
0A              Length Checksum (F6 + 0A = 100)
D5              Response Command
41              Response for InDataExchange
00              Status code: No error
00              ?? TLV block ?? Something NFC Forum Type ??
B1              ?? TLV block ?? Something NFC Forum Type ??
----
81              NDEF record header - TNF + Flags: MB=1b ME=0b CF=0b SR=0b IL=0b TNF=001b (Well-Known)
02              NDEF record header - Record Type Length = 2 octets
00 00 00 0D     NDEF record header - Payload Length = 13 octets
                NDEF record header - ID Length missing since it is optional (IL=0b)
48 73           NDEF record header - Record(Payload) Type = ‘Hs’ (Handover Select Record)
                NDEF record header - Payload ID missing since it is optional (IL=0b)
15              NDEF record payload - Connection Handover specification version = 1.5 (0x15 = 0001 0101 = 1 5)
C1              NDEF record header - TNF + Flags: MB=1b ME=1b CF=0b SR=0b IL=0b TNF=001b (Well-Known)
02              NDEF record header - Record Type Length = 2 octets
00 00 00 04     NDEF record header - Payload Length = 4 octets
                NDEF record header - ID Length missing since it is optional (IL=0b)
61 63           NDEF record header - Record(Payload) Type = ‘ac’ (Alternative Carrier Record)
                NDEF record header - Payload ID missing since it is optional (IL=0b)
01              NDEF record payload - Carrier Power State = 1 (active)
01              NDEF record payload - Carrier Data Reference Length = 1 octet
30              NDEF record payload - Carrier Data Reference = ‘0’
00              NDEF record payload - Auxiliary Data Reference Count: 0
0A              NDEF record header - TNF + Flags: MB=0b ME=0b CF=0b SR=0b IL=1b TNF=010b (MIME media-type)
20              NDEF record header - Record Type Length = 32 octets
00 00 00 52     NDEF record header - Payload Length = 82 octets
01              NDEF record header - Payload ID Length = 1 octet
61 70 70 6C 69 63 61 74
69 6F 6E 2F 76 6E 64 2E
62 6C 75 65 74 6F 6F 74
68 2E 6C 65 2E 6F 6F 62   NDEF record header - Record(Payload) Type = ‘application/vnd.bluetooth.ep.oob’:
30              NDEF record header - Payload ID = ‘0’
08              Length 8
1B              LE Bluetooth Device Address
4C 0B BC 24 18 47   BLE Address in reverse order.
01              Random Address
02              Length 2
1C              LE Role
00              Peripheral
11              Length 2
10              Security Manager TK Value
00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
11              Length 17
22              LE Secure Connections Confirmation Value
60 0D 4B 8B 62 7C 84 1C
9D FE 00 AB 5D 49 D5 3D
11              Length 17
23              LE Secure Connections Random Value
57 2C 27 0E 2C A8 C8 EF
DD 02 64 81 0C 59 97 7D
03              Length 3
19              Appearance
00 00           ??
02              Length 2
01              Flags
04              ??
08              Length 8
09              Complete Local Name
50 6F 73 74 75 72 65        Posture
41              NDEF record header - TNF + Flags: MB=0b ME=1b CF=0b SR=0b IL=0b TNF=001b (Well-Known)
02              NDEF record header - Record Type Length = 2 octets
00 00 00 1A     NDEF record header - Payload Length = 26 octets
54 70           NDEF record header - Record(Payload) Type = ‘Tp’ (Service Parameter)
10              ?? Uri code??
13              Length 19
75 72 6E 3A 6E 66 63 3A
73 6E 3A 68 61 6E 64 6F
76 65 72        urn:nfc:sn:handover
00 2C 02        ??
----
04              Data checksum
00              Postample

*/
