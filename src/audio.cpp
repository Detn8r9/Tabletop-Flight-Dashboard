// Minimal DAC-only ES8311 driver. Register values follow Espressif's
// esp_codec_dev es8311 driver for: slave mode, MCLK = 256 x 16 kHz, I2S
// Philips 16-bit. Pins from Waveshare's board config for this board.
#include "audio.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include "hw.h"

#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14
#define PIN_MCLK 16
#define PIN_BCLK 9
#define PIN_WS   45
#define PIN_DOUT 8
#define PIN_PA   46       // speaker amplifier enable
#define ES8311_ADDR 0x18
#define RATE 16000

static i2s_chan_handle_t tx = nullptr;
static QueueHandle_t q = nullptr;
static bool ready = false;

static bool wr(uint8_t reg, uint8_t val) {
  i2cLock();
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  bool ok = Wire.endTransmission() == 0;
  i2cUnlock();
  return ok;
}

static bool codecInit() {
  bool ok = wr(0x44, 0x08);
  ok = wr(0x44, 0x08);           // first write is sometimes dropped
  ok &= wr(0x01, 0x30);
  ok &= wr(0x02, 0x00);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x16, 0x24);
  ok &= wr(0x04, 0x10);
  ok &= wr(0x05, 0x00);
  ok &= wr(0x0B, 0x00);
  ok &= wr(0x0C, 0x00);
  ok &= wr(0x10, 0x1F);
  ok &= wr(0x11, 0x7F);
  ok &= wr(0x00, 0x80);          // slave mode
  ok &= wr(0x01, 0x3F);          // clocks on, MCLK from pin
  ok &= wr(0x06, 0x03);          // BCLK divider (4), not inverted
  ok &= wr(0x13, 0x10);
  ok &= wr(0x1B, 0x0A);
  ok &= wr(0x1C, 0x6A);
  ok &= wr(0x44, 0x08);
  // sample-rate coefficients for 4.096 MHz / 16 kHz
  ok &= wr(0x02, 0x00);
  ok &= wr(0x05, 0x00);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x04, 0x20);
  ok &= wr(0x07, 0x00);
  ok &= wr(0x08, 0xFF);
  ok &= wr(0x06, 0x03);
  // I2S, 16-bit; DAC on, ADC off
  ok &= wr(0x09, 0x0C);
  ok &= wr(0x0A, 0x4C);
  // start
  ok &= wr(0x00, 0x80);
  ok &= wr(0x01, 0x3F);
  ok &= wr(0x17, 0xBF);
  ok &= wr(0x0E, 0x02);
  ok &= wr(0x12, 0x00);
  ok &= wr(0x14, 0x1A);
  ok &= wr(0x0D, 0x01);
  ok &= wr(0x15, 0x40);
  ok &= wr(0x37, 0x08);
  ok &= wr(0x45, 0x00);
  ok &= wr(0x31, 0x00);          // unmute
  return ok;
}

static void setVolume(uint8_t v) {
  // 0xBF = 0 dB, 0.5 dB per step. Map 0..100 onto -50..0 dB.
  uint8_t reg = v == 0 ? 0 : (uint8_t)(0xBF - (100 - v));
  wr(0x32, reg);
}

static void playTone(float f, int ms, float amp) {
  const int n = RATE * ms / 1000;
  static int16_t buf[2 * 256];
  int done = 0;
  float phase = 0, step = 2 * PI * f / RATE;
  while (done < n) {
    int chunk = min(256, n - done);
    for (int i = 0; i < chunk; i++) {
      // short attack/release to avoid clicks
      int k = done + i;
      float env = min(1.0f, min(k, n - k) / (RATE * 0.008f));
      int16_t s = (int16_t)(sinf(phase) * amp * env * 32767);
      phase += step;
      if (phase > 2 * PI) phase -= 2 * PI;
      buf[2 * i] = s;
      buf[2 * i + 1] = s;
    }
    size_t wrote;
    i2s_channel_write(tx, buf, chunk * 4, &wrote, pdMS_TO_TICKS(500));
    done += chunk;
  }
}

static void silence(int ms) { playTone(0, ms, 0); }

static void audioTask(void *) {
  uint16_t msg;
  for (;;) {
    if (xQueueReceive(q, &msg, portMAX_DELAY) != pdTRUE) continue;
    uint8_t kind = msg >> 8, vol = msg & 0xFF;
    setVolume(vol);
    digitalWrite(PIN_PA, HIGH);
    i2s_channel_enable(tx);
    silence(30);
    if (kind == 2) {
      for (int i = 0; i < 3; i++) { playTone(1320, 140, 0.6f); silence(90); }
    } else if (kind == 3) {
      playTone(1568, 90, 0.35f);   // soft "ping": a plane is passing overhead
      silence(40);
      playTone(2093, 160, 0.25f);
    } else {
      playTone(988, 160, 0.5f);   // B5
      playTone(1319, 260, 0.5f);  // E6
    }
    silence(60);
    i2s_channel_disable(tx);
    digitalWrite(PIN_PA, LOW);
  }
}

void audioInit() {
  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, LOW);

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) return;
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = (gpio_num_t)PIN_MCLK;
  sc.gpio_cfg.bclk = (gpio_num_t)PIN_BCLK;
  sc.gpio_cfg.ws = (gpio_num_t)PIN_WS;
  sc.gpio_cfg.dout = (gpio_num_t)PIN_DOUT;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) return;

  // codec needs MCLK running while it is configured
  i2s_channel_enable(tx);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  ready = codecInit();
  i2s_channel_disable(tx);
  if (!ready) {
    log_w("ES8311 not responding; alerts will be silent");
    return;
  }
  q = xQueueCreate(4, sizeof(uint16_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
}

void audioChime(uint8_t kind, uint8_t volume) {
  if (!ready || !q) return;
  uint16_t msg = (uint16_t)(kind << 8) | volume;
  xQueueSend(q, &msg, 0);
}
