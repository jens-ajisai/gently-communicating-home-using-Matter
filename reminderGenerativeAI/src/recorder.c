#include "recorder.h"

#include <nrfx_pdm.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "persistence/persistence.h"

LOG_MODULE_REGISTER(audio_recorder, CONFIG_MAIN_LOG_LEVEL);

struct work_with_data onRecordingFinishedWork;

/* Milliseconds to wait for a block to be read. */
#define READ_TIMEOUT 2000

/* Audio sampling config */
#define AUDIO_CHANNEL_NUM           1
#define AUDIO_SAMPLING_FREQUENCY    8000
#define AUDIO_SAMPLES_PER_MS        (AUDIO_SAMPLING_FREQUENCY / 1000)
#define AUDIO_DSP_SAMPLE_LENGTH_MS  1500
#define AUDIO_DSP_SAMPLE_RESOLUTION (sizeof(short))
#define AUDIO_DSP_SAMPLE_BUFFER_SIZE \
  (AUDIO_SAMPLES_PER_MS * AUDIO_DSP_SAMPLE_LENGTH_MS * AUDIO_DSP_SAMPLE_RESOLUTION)
#define PDM_CLK_PIN                 43  // 32+11 = p1.11
#define PDM_DIN_PIN                 44  // 32+12 = p1.12
#define BUFFER_COUNT                4

K_MEM_SLAB_DEFINE_STATIC(mem_slab, AUDIO_DSP_SAMPLE_BUFFER_SIZE, BUFFER_COUNT, sizeof(void *));
K_MSGQ_DEFINE(rx_queue, sizeof(void *), 4, sizeof(void *));


// http://soundfile.sapp.org/doc/WaveFormat/
struct wav_hdr {
  uint8_t RIFF[4];
  uint32_t ChunkSize;
  uint8_t WAVE[4];
  uint8_t fmt[4];
  uint32_t Subchunk1Size;
  uint16_t AudioFormat;
  uint16_t NumOfChan;
  uint32_t SamplesPerSec;
  uint32_t bytesPerSec;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  uint8_t Subchunk2ID[4];
  uint32_t Subchunk2Size;
};

const struct wav_hdr WAV_DEFAULTS = {{'R', 'I', 'F', 'F'},
                                     0,
                                     {'W', 'A', 'V', 'E'},
                                     {'f', 'm', 't', ' '},
                                     16,
                                     1,
                                     AUDIO_CHANNEL_NUM,
                                     AUDIO_SAMPLING_FREQUENCY,
                                     AUDIO_SAMPLING_FREQUENCY * 2,
                                     2,
                                     8 * sizeof(short),
                                     {'d', 'a', 't', 'a'},
                                     0};


static void samples_callback(nrfx_pdm_evt_t const *p_evt) {
  if (p_evt->error != 0) {
    LOG_ERR("ERR: PDM handler error ocured");
    LOG_ERR("ERR: samples_callback error: %d, %d", p_evt->error, p_evt->buffer_requested);
    return;
  }

  if (p_evt->buffer_requested) {
    void *buffer;
    int err;

    err = k_mem_slab_alloc(&mem_slab, &buffer, K_NO_WAIT);

    if (err < 0) {
      LOG_ERR("Failed to allocate buffer: %d", err);
    } else {
      // size is 16-bit words
      err = nrfx_pdm_buffer_set(buffer, AUDIO_DSP_SAMPLE_BUFFER_SIZE / 2);
      if (err != NRFX_SUCCESS) {
        LOG_ERR("Failed to set buffer: 0x%08x", err);
      }
    }
  }

  if (p_evt->buffer_released != NULL) {
    k_msgq_put(&rx_queue, &p_evt->buffer_released, K_NO_WAIT);
  }
  
  uint32_t used = k_mem_slab_num_used_get(&mem_slab);
  LOG_INF("Allocated %d of %d bytes", used * AUDIO_DSP_SAMPLE_BUFFER_SIZE,
          BUFFER_COUNT * AUDIO_DSP_SAMPLE_BUFFER_SIZE);
}

static bool setup_nrf_pdm(nrfx_pdm_event_handler_t event_handler) {
  nrfx_err_t err;

  // PDM driver configuration
  nrfx_pdm_config_t config_pdm = NRFX_PDM_DEFAULT_CONFIG(PDM_CLK_PIN, PDM_DIN_PIN);
  if (AUDIO_SAMPLING_FREQUENCY == 8000) {
    // taken from firmware-nordic-thingy53/drivers/vm3011/vm3011.c l.346
    config_pdm.clock_freq = 0x04104000UL;
    config_pdm.ratio = NRF_PDM_RATIO_64X;
  } else if (AUDIO_SAMPLING_FREQUENCY == 16000) {
    config_pdm.clock_freq = NRF_PDM_FREQ_1280K;
    config_pdm.ratio = NRF_PDM_RATIO_80X;
  }
  config_pdm.edge = NRF_PDM_EDGE_LEFTRISING;
  config_pdm.gain_l = NRF_PDM_GAIN_MAXIMUM;
  config_pdm.gain_r = NRF_PDM_GAIN_MAXIMUM;
  // defaults below
  config_pdm.mode = NRF_PDM_MODE_MONO;
  config_pdm.mclksrc = PDM_MCLKCONFIG_SRC_PCLK32M;
  config_pdm.interrupt_priority = NRFX_PDM_DEFAULT_CONFIG_IRQ_PRIORITY;

  IRQ_DIRECT_CONNECT(PDM0_IRQn, IRQ_PRIO_LOWEST, nrfx_pdm_irq_handler, 0);

  err = nrfx_pdm_init(&config_pdm, event_handler);
  if (err != NRFX_SUCCESS) {
    LOG_ERR("Failed to init PDM");
    return false;
  } else {
    if (!irq_is_enabled(PDM0_IRQn)) {
      irq_enable(PDM0_IRQn);
    }
    LOG_DBG("PDM init ok");
    return true;
  }
}

int do_pdm_transfer() {
  int ret;

  nrfx_pdm_uninit();
  if (!setup_nrf_pdm(samples_callback)) {
    LOG_ERR("Error microphone init");
    return -1;
  }

  ret = nrfx_pdm_start();
  if (ret != NRFX_SUCCESS) {
    LOG_ERR("Error microphone start");
    return ret;
  }

  // path of temp file
  uint32_t uptime = (uint32_t)(k_uptime_get() / 1000);
  char path[32];
  snprintf(path, 32, "rec-%d", uptime);

  // skip the first frame (1.5s!) since there is a click sound in the first 500ms
  uint8_t skip = 1;
  // total size to update the wave hearder
  uint32_t total_size = 0;
  struct wav_hdr header = WAV_DEFAULTS;
  fs_appendData(path, &header, sizeof(header));

  for (int i = 0; true; ++i) {
    void *buffer;

    ret = k_msgq_get(&rx_queue, &buffer, SYS_TIMEOUT_MS(READ_TIMEOUT));

    if (ret != 0) {
      LOG_ERR("No audio data to be read. Finished");

      ret = nrfx_pdm_stop();
      if (ret != NRFX_SUCCESS) {
        LOG_ERR("ERR: PDM Could not stop PDM sampling, error = %d", ret);
      }      

      break;
    } else {
      LOG_DBG("Released buffer %p", buffer);
    }

    if (skip > 0) {
      skip--;
    } else {
      fs_appendData(path, buffer, AUDIO_DSP_SAMPLE_BUFFER_SIZE);
      total_size += AUDIO_DSP_SAMPLE_BUFFER_SIZE;
    }

    k_mem_slab_free(&mem_slab, buffer);

    // Check if the recording shall be stopped
    int rc = k_poll(recordButtonEvents, ARRAY_SIZE(recordButtonEvents), K_NO_WAIT);
    if (rc == 0 && recordButtonEvents[0].signal->result == BUTTON_RELEASED) {
      k_poll_signal_reset(&recordButtonSignal);

      ret = nrfx_pdm_stop();
      if (ret != NRFX_SUCCESS) {
        LOG_ERR("ERR: PDM Could not stop PDM sampling, error = %d", ret);
      }
    }
  }

  // update the wave header
  header.Subchunk2Size = total_size;
  header.ChunkSize = header.Subchunk2Size + 36;
  fs_overwriteData(path, &header, sizeof(header), 0);

  // schedule the transcription
  memcpy(onRecordingFinishedWork.path, path, sizeof(path));
  k_work_reschedule(&onRecordingFinishedWork.work, K_SECONDS(2));

  return ret;
}
