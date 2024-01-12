#include <nfc/ndef/ch.h>
#include <nfc/ndef/ch_msg.h>
#include <nfc/ndef/le_oob_rec.h>
#include <nfc/ndef/le_oob_rec_parser.h>
#include <nfc/ndef/msg.h>
#include <nfc/t4t/ndef_file.h>
#include <nfc/tnep/ch.h>
#include <nfc/tnep/tag.h>
#include <nfc_t4t_lib.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nfc, CONFIG_MAIN_LOG_LEVEL);

#define K_POOL_EVENTS_CNT (NFC_TNEP_EVENTS_NUMBER + 1)

#define NFC_NDEF_LE_OOB_REC_PARSER_BUFF_SIZE 150
#define NFC_TNEP_BUFFER_SIZE 1024

// defined in bt.c
extern struct bt_le_oob oob_local;
extern struct bt_le_oob oob_remote;

static uint8_t tk_value[NFC_NDEF_LE_OOB_REC_TK_LEN];
static uint8_t remote_tk_value[NFC_NDEF_LE_OOB_REC_TK_LEN];

static struct k_poll_event events[K_POOL_EVENTS_CNT];
static uint8_t tnep_buffer[NFC_TNEP_BUFFER_SIZE];
static uint8_t tnep_swap_buffer[NFC_TNEP_BUFFER_SIZE];

static bool use_remote_tk;

static void nfc_callback(void *context, nfc_t4t_event_t event, const uint8_t *data,
                         size_t data_length, uint32_t flags) {
  ARG_UNUSED(context);
  ARG_UNUSED(data);
  ARG_UNUSED(flags);

  switch (event) {
    case NFC_T4T_EVENT_FIELD_ON:
      LOG_INF("NFC_T4T_EVENT_FIELD_ON");    
      nfc_tnep_tag_on_selected();
      break;

    case NFC_T4T_EVENT_FIELD_OFF:
      LOG_INF("NFC_T4T_EVENT_FIELD_OFF");    
      nfc_tnep_tag_on_selected();
      break;

    case NFC_T4T_EVENT_NDEF_READ:
      LOG_INF("NFC_T4T_EVENT_NDEF_READ");
      break;

    case NFC_T4T_EVENT_NDEF_UPDATED:
      LOG_INF("NFC_T4T_EVENT_NDEF_UPDATED");
      if (data_length > 0) {
        nfc_tnep_tag_rx_msg_indicate(nfc_t4t_ndef_file_msg_get(data), data_length);
      }

    default:
      break;
  }
}

static int check_oob_carrier(const struct nfc_tnep_ch_record *ch_record,
                             const struct nfc_ndef_record_desc **oob_data) {
  const struct nfc_ndef_ch_ac_rec *ac_rec = NULL;

  LOG_INF("ASSUME THIS IS NOT CALLED!");
  
  for (size_t i = 0; i < ch_record->count; i++) {
    if (nfc_ndef_le_oob_rec_check(ch_record->carrier[i])) {
      *oob_data = ch_record->carrier[i];
    }
  }

  if (!oob_data) {
    LOG_ERR("Connection Handover Requester not supporting OOB BLE");
    return -EINVAL;
  }

  /* Look for the corresponding Alternative Carrier Record. */
  for (size_t i = 0; i < ch_record->count; i++) {
    if (((*oob_data)->id_length == ch_record->ac[i].carrier_data_ref.length) &&
        (memcmp((*oob_data)->id, ch_record->ac[i].carrier_data_ref.data, (*oob_data)->id_length) ==
         0)) {
      ac_rec = &ch_record->ac[i];
    }
  }

  if (!ac_rec) {
    LOG_ERR("No Alternative Carrier Record for OOB LE carrier");
    return -EINVAL;
  }

  /* Check carrier state */
  if ((ac_rec->cps != NFC_AC_CPS_ACTIVE) && (ac_rec->cps != NFC_AC_CPS_ACTIVATING)) {
    LOG_ERR("LE OBB Carrier inactive");
    return -EINVAL;
  }

  return 0;
}

// Only static handover
// https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/nfc/tnep/ch.html
// Means oob_le_data_handle, carrier_prepare, tnep_ch_request_received will not be called.

// To be merged with set_remote_oob
static int oob_le_data_handle(const struct nfc_ndef_record_desc *rec, bool request) {
  int err;
  const struct nfc_ndef_le_oob_rec_payload_desc *oob;
  uint8_t desc_buf[NFC_NDEF_LE_OOB_REC_PARSER_BUFF_SIZE];
  uint32_t desc_buf_len = sizeof(desc_buf);

  LOG_INF("ASSUME THIS IS NOT CALLED!");

  err = nfc_ndef_le_oob_rec_parse(rec, desc_buf, &desc_buf_len);
  if (err) {
    LOG_ERR("Error during NDEF LE OOB Record parsing, err: %d.", err);
  }

  oob = (struct nfc_ndef_le_oob_rec_payload_desc *)desc_buf;

  nfc_ndef_le_oob_rec_printout(oob);

  if ((*oob->le_role != NFC_NDEF_LE_OOB_REC_LE_ROLE_CENTRAL_ONLY) &&
      (*oob->le_role != NFC_NDEF_LE_OOB_REC_LE_ROLE_CENTRAL_PREFFERED)) {
    LOG_ERR("Unsupported Device LE Role");
    return -EINVAL;
  }

  if (oob->le_sc_data) {
    bt_le_oob_set_sc_flag(true);
    oob_remote.le_sc_data = *oob->le_sc_data;
    bt_addr_le_copy(&oob_remote.addr, oob->addr);
  }

  if (oob->tk_value) {
    bt_le_oob_set_legacy_flag(true);
    memcpy(remote_tk_value, oob->tk_value, sizeof(remote_tk_value));
    use_remote_tk = request;
  }

  return 0;
}

static int carrier_prepare(void) {
  static struct nfc_ndef_le_oob_rec_payload_desc rec_payload;

  NFC_NDEF_LE_OOB_RECORD_DESC_DEF(oob_rec, '0', &rec_payload);
  NFC_NDEF_CH_AC_RECORD_DESC_DEF(oob_ac, NFC_AC_CPS_ACTIVE, 1, "0", 0);

  LOG_INF("ASSUME THIS IS NOT CALLED!");

  memset(&rec_payload, 0, sizeof(rec_payload));

  rec_payload.addr = &oob_local.addr;
  rec_payload.le_sc_data = &oob_local.le_sc_data;
  rec_payload.tk_value = tk_value;
  rec_payload.local_name = bt_get_name();
  rec_payload.le_role = NFC_NDEF_LE_OOB_REC_LE_ROLE(NFC_NDEF_LE_OOB_REC_LE_ROLE_PERIPH_ONLY);
  rec_payload.appearance = NFC_NDEF_LE_OOB_REC_APPEARANCE(CONFIG_BT_DEVICE_APPEARANCE);
  rec_payload.flags = NFC_NDEF_LE_OOB_REC_FLAGS(BT_LE_AD_NO_BREDR);

  LOG_INF("following my oob data");
  nfc_ndef_le_oob_rec_printout(&rec_payload);

  return nfc_tnep_ch_carrier_set(&NFC_NDEF_CH_AC_RECORD_DESC(oob_ac),
                                 &NFC_NDEF_LE_OOB_RECORD_DESC(oob_rec), 1);
}

static int tnep_ch_request_received(const struct nfc_tnep_ch_request *ch_req) {
  int err;
  const struct nfc_ndef_record_desc *oob_data = NULL;

  LOG_INF("ASSUME THIS IS NOT CALLED!");

  if (!ch_req->ch_record.count) {
    return -EINVAL;
  }

  err = check_oob_carrier(&ch_req->ch_record, &oob_data);
  if (err) {
    return err;
  }

  err = oob_le_data_handle(oob_data, true);
  if (err) {
    return err;
  }

  return carrier_prepare();
}

static int tnep_initial_msg_encode(struct nfc_ndef_msg_desc *msg) {
  LOG_INF("enter tnep_initial_msg_encode");
  int err;
  struct nfc_ndef_ch_msg_records ch_records;
  static struct nfc_ndef_le_oob_rec_payload_desc rec_payload;

  NFC_NDEF_LE_OOB_RECORD_DESC_DEF(oob_rec, '0', &rec_payload);
  NFC_NDEF_CH_AC_RECORD_DESC_DEF(oob_ac, NFC_AC_CPS_ACTIVE, 1, "0", 0);
  NFC_NDEF_CH_HS_RECORD_DESC_DEF(hs_rec, NFC_NDEF_CH_MSG_MAJOR_VER, NFC_NDEF_CH_MSG_MINOR_VER, 1);

  memset(&rec_payload, 0, sizeof(rec_payload));

  rec_payload.addr = &oob_local.addr;
  rec_payload.le_sc_data = &oob_local.le_sc_data;
  rec_payload.tk_value = tk_value;
  rec_payload.local_name = bt_get_name();
  rec_payload.le_role = NFC_NDEF_LE_OOB_REC_LE_ROLE(NFC_NDEF_LE_OOB_REC_LE_ROLE_PERIPH_ONLY);
  rec_payload.appearance = NFC_NDEF_LE_OOB_REC_APPEARANCE(CONFIG_BT_DEVICE_APPEARANCE);
  rec_payload.flags = NFC_NDEF_LE_OOB_REC_FLAGS(BT_LE_AD_NO_BREDR);

  LOG_INF("following my oob data");
  nfc_ndef_le_oob_rec_printout(&rec_payload);

  ch_records.ac = &NFC_NDEF_CH_AC_RECORD_DESC(oob_ac);
  ch_records.carrier = &NFC_NDEF_LE_OOB_RECORD_DESC(oob_rec);
  ch_records.cnt = 1;

  err = nfc_ndef_ch_msg_hs_create(msg, &NFC_NDEF_CH_RECORD_DESC(hs_rec), &ch_records);
  if (err) {
    return err;
  }

  return nfc_tnep_initial_msg_encode(msg, NULL, 0);
}
static struct nfc_tnep_ch_cb ch_cb = {.request_msg_recv = tnep_ch_request_received};

static bool initialized = false;

#include <sys/reboot.h>
void nfc_init(void) {
  int err;

  if(initialized) {
    // workaround: reboot
    LOG_ERR("Cannot re-initialize TNEP protocol. Apply workaround reboot.");
    k_sleep(K_SECONDS(1));
    sys_reboot(SYS_REBOOT_COLD);
    return; 
  }

  /* TNEP init */
  err = nfc_tnep_tag_tx_msg_buffer_register(tnep_buffer, tnep_swap_buffer, sizeof(tnep_buffer));
  if (err) {
    LOG_ERR("Cannot register tnep buffer, err: %d", err);
    return;
  }

  err = nfc_tnep_tag_init(events, NFC_TNEP_EVENTS_NUMBER, nfc_t4t_ndef_rwpayload_set);
  if (err) {
    LOG_ERR("Cannot initialize TNEP protocol, err: %d", err);
    return;
  }

  /* Set up NFC */
  err = nfc_t4t_setup(nfc_callback, NULL);
  if (err) {
    LOG_ERR("Cannot setup NFC T4T library!");
    return;
  }

  err = nfc_tnep_tag_initial_msg_create(2, tnep_initial_msg_encode);
  if (err) {
    LOG_ERR("Cannot create initial TNEP message, err: %d", err);
  }

  err = nfc_tnep_ch_service_init(&ch_cb);
  if (err) {
    LOG_ERR("TNEP CH Service init error: %d", err);
    return;
  }

  /* Start sensing NFC field */
  err = nfc_t4t_emulation_start();
  if (err) {
    LOG_ERR("Cannot start emulation!");
    return;
  }

  initialized = true;

  LOG_INF("NFC configuration done");
}
