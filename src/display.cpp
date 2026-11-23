#include "display.h"
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <freertos/semphr.h>

// Board wiring (from Waveshare examples)
#define PIN_MOSI 12
#define PIN_SCK  11
#define PIN_DC   5
#define PIN_CS   40
#define PIN_RST  41

GFXcanvas1 *gfx = nullptr;

static esp_lcd_panel_io_handle_t io = nullptr;
static uint8_t *panelBuf = nullptr;       // panel-native packing, DMA RAM
static uint8_t *snapBuf = nullptr;        // canvas copy for the web preview
static SemaphoreHandle_t txDone = nullptr;
static SemaphoreHandle_t snapLock = nullptr;
static const int PANEL_BYTES = SCREEN_W * SCREEN_H / 8;

static bool IRAM_ATTR onTxDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(txDone, &woken);
  return woken == pdTRUE;
}

static void cmd(uint8_t c) { esp_lcd_panel_io_tx_param(io, c, nullptr, 0); }
static void cmd(uint8_t c, std::initializer_list<uint8_t> d) {
  esp_lcd_panel_io_tx_param(io, c, d.begin(), d.size());
}

void displayInit() {
  txDone = xSemaphoreCreateBinary();
  snapLock = xSemaphoreCreateMutex();

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = PANEL_BYTES + 64;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t ioc = {};
  ioc.dc_gpio_num = PIN_DC;
  ioc.cs_gpio_num = PIN_CS;
  ioc.pclk_hz = 10 * 1000 * 1000;
  ioc.lcd_cmd_bits = 8;
  ioc.lcd_param_bits = 8;
  ioc.spi_mode = 0;
  ioc.trans_queue_depth = 10;
  ioc.on_color_trans_done = onTxDone;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &ioc, &io));

  gpio_config_t g = {};
  g.mode = GPIO_MODE_OUTPUT;
  g.pin_bit_mask = 1ULL << PIN_RST;
  g.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&g);
  gpio_set_level((gpio_num_t)PIN_RST, 1); delay(50);
  gpio_set_level((gpio_num_t)PIN_RST, 0); delay(20);
  gpio_set_level((gpio_num_t)PIN_RST, 1); delay(50);

  // ST7305 init (Waveshare)
  cmd(0xD6, {0x17, 0x02});
  cmd(0xD1, {0x01});
  cmd(0xC0, {0x11, 0x04});
  cmd(0xC1, {0x69, 0x69, 0x69, 0x69});
  cmd(0xC2, {0x19, 0x19, 0x19, 0x19});
  cmd(0xC4, {0x4B, 0x4B, 0x4B, 0x4B});
  cmd(0xC5, {0x19, 0x19, 0x19, 0x19});
  cmd(0xD8, {0x80, 0xE9});
  cmd(0xB2, {0x02});
  cmd(0xB3, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45});
  cmd(0xB4, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45});
  cmd(0x62, {0x32, 0x03, 0x1F});
  cmd(0xB7, {0x13});
  cmd(0xB0, {0x64});
  cmd(0x11);
  delay(200);
  cmd(0xC9, {0x00});
  cmd(0x36, {0x48});
  cmd(0x3A, {0x11});
  cmd(0xB9, {0x20});
  cmd(0xB8, {0x29});
  cmd(0x21);
  cmd(0x2A, {0x12, 0x2A});
  cmd(0x2B, {0x00, 0xC7});
  cmd(0x35, {0x00});
  cmd(0xD0, {0xFF});
  cmd(0x38);
  cmd(0x29);

  panelBuf = (uint8_t *)heap_caps_malloc(PANEL_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  snapBuf = (uint8_t *)heap_caps_malloc(PANEL_BYTES, MALLOC_CAP_SPIRAM);
  assert(panelBuf && snapBuf);
  memset(snapBuf, 0, PANEL_BYTES);

  gfx = new GFXcanvas1(SCREEN_W, SCREEN_H);
  gfx->fillScreen(0);
  xSemaphoreGive(txDone);
}

// Panel packing (landscape): each byte covers a 2 (x) by 4 (y) block,
// counted from the bottom edge; bit set = white.
void displayPush() {
  const uint8_t *src = gfx->getBuffer();
  xSemaphoreTake(txDone, portMAX_DELAY);   // previous DMA finished
  const int rowBytes = SCREEN_W / 8;
  for (int bx = 0; bx < SCREEN_W / 2; bx++) {
    const int x0 = bx * 2;
    const int byteIdx = x0 >> 3;
    const int sh0 = 7 - (x0 & 7);            // bit of pixel x0 in canvas byte
    uint8_t *out = panelBuf + bx * (SCREEN_H / 4);
    for (int by = 0; by < SCREEN_H / 4; by++) {
      uint8_t b = 0;
      for (int ly = 0; ly < 4; ly++) {
        const int y = SCREEN_H - 1 - (by * 4 + ly);
        const uint8_t cb = src[y * rowBytes + byteIdx];
        // white where canvas has no ink
        if (!((cb >> sh0) & 1))       b |= 1 << (7 - (ly * 2));
        if (!((cb >> (sh0 - 1)) & 1)) b |= 1 << (7 - (ly * 2 + 1));
      }
      out[by] = b;
    }
  }
  cmd(0x2A, {0x12, 0x2A});
  cmd(0x2B, {0x00, 0xC7});
  esp_lcd_panel_io_tx_color(io, 0x2C, panelBuf, PANEL_BYTES);

  if (xSemaphoreTake(snapLock, 0) == pdTRUE) {
    memcpy(snapBuf, src, PANEL_BYTES);
    xSemaphoreGive(snapLock);
  }
}

void displayPower(bool on) {
  xSemaphoreTake(txDone, portMAX_DELAY);
  cmd(on ? 0x29 : 0x28);
  xSemaphoreGive(txDone);
}

void displaySnapshot(uint8_t *dst) {
  xSemaphoreTake(snapLock, portMAX_DELAY);
  memcpy(dst, snapBuf, PANEL_BYTES);
  xSemaphoreGive(snapLock);
}
