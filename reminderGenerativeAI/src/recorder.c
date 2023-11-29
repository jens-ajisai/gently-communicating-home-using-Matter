#include "recorder.h"

#include <stdio.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "persistence/persistence.h"
LOG_MODULE_REGISTER(audio_recorder, CONFIG_MAIN_LOG_LEVEL);

struct work_with_data onRecordingFinishedWork;

/* Milliseconds to wait for a block to be read. */
#define READ_TIMEOUT 1000

#define SAMPLE_RATE 16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define NUM_CHANNEL 1

/* Size of a block for 500ms of audio data. */
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
  (BYTES_PER_SAMPLE * (_sample_rate / 2) * _number_of_channels)

/* Driver will allocate blocks from this slab to receive audio data into them.
 * Application, after getting a given block from the driver and processing its
 * data, needs to free that block.
 */
#define ACTUAL_BLOCK_SIZE BLOCK_SIZE(SAMPLE_RATE, 1)
#define BLOCK_COUNT 8
K_MEM_SLAB_DEFINE_STATIC(mem_slab, ACTUAL_BLOCK_SIZE, BLOCK_COUNT, 4);


const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

struct pcm_stream_cfg stream = {.pcm_width = SAMPLE_BIT_WIDTH,
                                .mem_slab = &mem_slab,
                                .pcm_rate = SAMPLE_RATE,
                                .block_size = BLOCK_SIZE(SAMPLE_RATE, NUM_CHANNEL)};
struct dmic_cfg cfg = {
    .io =
        {
            /* These fields can be used to limit the PDM clock
             * configurations that the driver is allowed to use
             * to those supported by the microphone.
             */
            .min_pdm_clk_freq = 1000000,
            .max_pdm_clk_freq = 3500000,
            .min_pdm_clk_dc = 40,
            .max_pdm_clk_dc = 60,
        },
    .streams = &stream,
    .channel =
        {
            .req_num_streams = 1,
            .req_num_chan = NUM_CHANNEL,
        },
};


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
                                     1,
                                     16000,
                                     16000 * 2,
                                     2,
                                     16,
                                     {'d', 'a', 't', 'a'},
                                     0};


int do_pdm_transfer() {
  int ret;

  LOG_INF("PCM output rate: %u, channels: %u", cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

  ret = dmic_configure(dmic_dev, &cfg);
  if (ret < 0) {
    LOG_ERR("Failed to configure the driver: %d", ret);
    return ret;
  }

  ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
  if (ret < 0) {
    LOG_ERR("START trigger failed: %d", ret);
    return ret;
  }

  uint32_t uptime = (uint32_t)(k_uptime_get() / 1000);
  char path[32];
  snprintf(path, 32, "rec-%d", uptime);

  uint8_t skip = 1;
  uint32_t total_size = 0;
  struct wav_hdr header = WAV_DEFAULTS;

  fs_appendData(path, &header, sizeof(header));

  for (int i = 0; true; ++i) {
    void *buffer;
    uint32_t size;
    int ret;

    ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
    if (ret < 0) {
      LOG_ERR("%d - read failed: %d", i, ret);
      return ret;
    }

    LOG_INF("%d - got buffer %p of %u bytes", i, buffer, size);

    // Klick sound at the beginning?
    // Very low gain!
    if (skip > 0) {
      skip--;
    } else {
      fs_appendData(path, buffer, size);
      total_size += size;
    }

    k_mem_slab_free(&mem_slab, &buffer);
    int rc = k_poll(recordButtonEvents, ARRAY_SIZE(recordButtonEvents), K_NO_WAIT);
    if (rc == 0 && recordButtonEvents[0].signal->result == BUTTON_RELEASED) {
      k_poll_signal_reset(&recordButtonSignal);
      break;
    }
  }

  header.Subchunk2Size = total_size;
  header.ChunkSize = header.Subchunk2Size + 36;
  fs_overwriteData(path, &header, sizeof(header), 0);

  ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
  if (ret < 0) {
    LOG_ERR("STOP trigger failed: %d", ret);
    return ret;
  }

  memcpy(onRecordingFinishedWork.path, path, sizeof(path));
  k_work_submit(&onRecordingFinishedWork.work);

  return ret;
}

void recorder_init() {
  if (!device_is_ready(dmic_dev)) {
    LOG_ERR("%s is not ready", dmic_dev->name);
    return;
  }

  // Cannot add this to the initializer since it is not constant.
  cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
}