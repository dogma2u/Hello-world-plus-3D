#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cmath>
#include <cstdint>
#include <string.h>
#include "font8x8.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/regs/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define SDA_PIN 0
#define SCL_PIN 1
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
#define I2C_CLOCK_HZ 2000000
#define OLED_I2C i2c0
#define OLED_I2C_DMA_CH 5
#define OLED_FB_BYTES (OLED_WIDTH * OLED_HEIGHT / 8)

static uint16_t oled_dma_ping[1 + OLED_FB_BYTES];
static uint16_t oled_dma_pong[1 + OLED_FB_BYTES];
static volatile bool oled_dma_busy = false;
static uint8_t oled_dma_slot = 0;

static inline void oled_i2c_wait_idle() {
  while (OLED_I2C->hw->status & I2C_IC_STATUS_MST_ACTIVITY_BITS) {
    tight_loop_contents();
  }
  while (OLED_I2C->hw->txflr) {
    tight_loop_contents();
  }
}

static void oled_cmd1(uint8_t c) {
  oled_dma_wait_done();
  oled_i2c_wait_idle();
  uint8_t buf[2] = {0x00, c};
  i2c_write_blocking(OLED_I2C, OLED_ADDRESS, buf, 2, false);
}

static void oled_i2c_bus_init() {
  gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(SDA_PIN);
  gpio_pull_up(SCL_PIN);
  i2c_init(OLED_I2C, I2C_CLOCK_HZ);
  oled_i2c_wait_idle();
  OLED_I2C->hw->enable = 0;
  OLED_I2C->hw->tar = OLED_ADDRESS;
  OLED_I2C->hw->enable = 1;
}

static void oled_dma_hw_init() {
  dma_channel_abort(OLED_I2C_DMA_CH);
  dma_channel_config cfg = dma_channel_get_default_config(OLED_I2C_DMA_CH);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
  channel_config_set_dreq(&cfg, i2c_get_dreq(OLED_I2C, true));
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  dma_channel_configure(
      OLED_I2C_DMA_CH,
      &cfg,
      &OLED_I2C->hw->data_cmd,
      oled_dma_ping,
      0,
      false);
}

static void oled_dma_pack(uint16_t *dst, const uint8_t *src, size_t len) {
  dst[0] = 0x40u | (1u << I2C_IC_DATA_CMD_RESTART_LSB);
  uint16_t *out = dst + 1;
  size_t i = 0;
  for (; i + 7 < len; i += 8) {
    out[i] = src[i];
    out[i + 1] = src[i + 1];
    out[i + 2] = src[i + 2];
    out[i + 3] = src[i + 3];
    out[i + 4] = src[i + 4];
    out[i + 5] = src[i + 5];
    out[i + 6] = src[i + 6];
    out[i + 7] = src[i + 7];
  }
  for (; i < len; i++) {
    out[i] = src[i];
  }
  dst[len] = ((uint16_t)src[len - 1]) | (1u << I2C_IC_DATA_CMD_STOP_LSB);
}

static void oled_dma_wait_done() {
  if (!oled_dma_busy) {
    return;
  }
  dma_channel_wait_for_finish_blocking(OLED_I2C_DMA_CH);
  oled_i2c_wait_idle();
  oled_dma_busy = false;
}

static void oled_dma_submit(const uint8_t *framebuffer, size_t len) {
  oled_dma_wait_done();

  uint16_t *dst = oled_dma_slot ? oled_dma_pong : oled_dma_ping;
  oled_dma_slot ^= 1;
  oled_dma_pack(dst, framebuffer, len);

  dma_channel_set_read_addr(OLED_I2C_DMA_CH, dst, false);
  dma_channel_set_trans_count(OLED_I2C_DMA_CH, len + 1, true);
  oled_dma_busy = true;
}

// Wire object is only passed to Adafruit ctor; all I2C goes through SDK + DMA above.
class SSD1306_I2C_DMA : public Adafruit_SSD1306 {
public:
  SSD1306_I2C_DMA(int8_t w, int8_t h, int8_t reset)
      : Adafruit_SSD1306(w, h, &Wire, reset) {}

  bool begin(uint8_t vccstate = SSD1306_SWITCHCAPVCC, uint8_t addr = 0x3C, bool reset = true, bool periphBegin = false) {
    (void)vccstate;
    (void)reset;
    (void)periphBegin;

    if (!buffer) {
      buffer = (uint8_t *)malloc(WIDTH * ((HEIGHT + 7) / 8));
      if (!buffer) {
        return false;
      }
    }
    memset(buffer, 0, WIDTH * ((HEIGHT + 7) / 8));

    oled_i2c_bus_init();
    oled_dma_hw_init();

    oled_cmd1(SSD1306_DISPLAYOFF);
    oled_cmd1(SSD1306_SETDISPLAYCLOCKDIV);
    oled_cmd1(0x80);
    oled_cmd1(SSD1306_SETMULTIPLEX);
    oled_cmd1(0x3F);
    oled_cmd1(SSD1306_SETDISPLAYOFFSET);
    oled_cmd1(0x0);
    oled_cmd1(SSD1306_SETSTARTLINE | 0x0);
    oled_cmd1(SSD1306_CHARGEPUMP);
    oled_cmd1(0x14);
    oled_cmd1(SSD1306_MEMORYMODE);
    oled_cmd1(0x00);
    oled_cmd1(SSD1306_SEGREMAP | 0x1);
    oled_cmd1(SSD1306_COMSCANDEC);
    oled_cmd1(SSD1306_SETCOMPINS);
    oled_cmd1(0x12);
    oled_cmd1(SSD1306_SETCONTRAST);
    oled_cmd1(0xCF);
    oled_cmd1(SSD1306_SETPRECHARGE);
    oled_cmd1(0xF1);
    oled_cmd1(SSD1306_SETVCOMDETECT);
    oled_cmd1(0x40);
    oled_cmd1(SSD1306_DISPLAYALLON_RESUME);
    oled_cmd1(SSD1306_NORMALDISPLAY);
    oled_cmd1(SSD1306_DISPLAYON);

    oled_cmd1(SSD1306_COLUMNADDR);
    oled_cmd1(0);
    oled_cmd1(WIDTH - 1);
    oled_cmd1(SSD1306_PAGEADDR);
    oled_cmd1(0);
    oled_cmd1((HEIGHT / 8) - 1);

    return true;
  }

  void display() {
    oled_dma_submit(buffer, OLED_FB_BYTES);
  }

  void displayWait() {
    oled_dma_wait_done();
  }

  void invertDisplay(bool inv) {
    oled_cmd1(inv ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
  }

  void dim(bool dim_val) {
    oled_cmd1(SSD1306_SETCONTRAST);
    oled_cmd1(dim_val ? 0x00 : 0xCF);
  }

  void ssd1306_command(uint8_t c) {
    oled_cmd1(c);
  }
};

SSD1306_I2C_DMA oled(OLED_WIDTH, OLED_HEIGHT, OLED_RESET);

volatile float lissa_phase = 0.0f;
volatile float cube_angle = 0.0f;

void setup1() {
  //vreg_set_voltage(VREG_VOLTAGE_1_30);
  set_sys_clock_khz(200000, true);
}

void loop1() {
  lissa_phase += 0.05f;
  if (lissa_phase > 6.28318f) lissa_phase -= 6.28318f;
  cube_angle += 0.015f;
  if (cube_angle > 6.28318f) cube_angle -= 6.28318f;
  delay(5);
}

void drawCustomChar(Adafruit_SSD1306 &display, int16_t x, int16_t y, char c, uint16_t color = SSD1306_WHITE) {
  if (c < 32 || c > 126) return;
  display.drawBitmap(x, y, font8x8_data[c - 32], 8, 8, color);
}

void drawCustomText(Adafruit_SSD1306 &display, int16_t x, int16_t y, const String &text, uint16_t color = SSD1306_WHITE) {
  int16_t cx = x;
  for (size_t i = 0; i < text.length(); i++) {
    drawCustomChar(display, cx, y, text[i], color);
    cx += 8;
  }
}

void grid(int width = OLED_WIDTH, int height = OLED_HEIGHT, uint16_t color = SSD1306_WHITE) {
  for (int d = 0; d < width; d++) {
    oled.drawPixel(d, 0, color);
    oled.drawPixel(d, (height / 2) - 1, color);
    oled.drawPixel(d, height - 1, color);
  }
  for (int d = 1; d < height; d++) {
    oled.drawPixel(0, d, color);
    oled.drawPixel(width / 4, d, color);
    oled.drawPixel(width / 2, d, color);
    oled.drawPixel((width / 4) * 3, d, color);
    oled.drawPixel(width - 1, d, color);
  }
}

void LissajousFrame(float phase, uint16_t color = SSD1306_WHITE, float Ax = 0.48f, float Ay = 0.48f, int a = 1, int b = 2) {
  float cx = OLED_WIDTH / 2.0f, cy = OLED_HEIGHT / 2.0f, ampX = Ax * OLED_WIDTH, ampY = Ay * OLED_HEIGHT;
  oled.clearDisplay();
  for (int d = 0; d < 360; d += 2) {
    float t = d * 0.017453292f;
    int ix = cx + ampX * sin(a * t);
    int iy = cy + ampY * sin(b * t + phase);
    if (ix >= 0 && ix < OLED_WIDTH && iy >= 0 && iy < OLED_HEIGHT) oled.drawPixel(ix, iy, color);
  }
  oled.display();
}

void grid_num() {
  int p[4] = {13, 46, 76, 108};
  for (int i = 0; i < 4; i++) {
    drawCustomText(oled, p[i], 13, String(i + 1));
    drawCustomText(oled, p[i], 45, String(i + 5));
  }
}

void flash_screen(int t = 4, float d = 0.5) {
  for (int f = 0; f < t; f++) {
    oled.invertDisplay(true);
    delay(d * 1000);
    oled.invertDisplay(false);
    delay(d * 1000);
  }
}

void clear_screen(String col = "black") {
  col.toLowerCase();
  oled.fillScreen(col == "white" ? SSD1306_WHITE : SSD1306_BLACK);
  oled.display();
}

void screen_bright(String dir = "down", float sp = 0.1) {
  dir.toLowerCase();
  int st = dir == "up" ? 0 : 255, en = dir == "up" ? 255 : 0, stp = dir == "up" ? 1 : -1;
  for (int v = st; v != en; v += stp) {
    oled.ssd1306_command(SSD1306_SETCONTRAST);
    oled.ssd1306_command(v);
    oled.display();
    delay(sp * 10);
  }
}

void hello_world() {
  int step = OLED_HEIGHT / 8;
  for (int l = 0; l < OLED_HEIGHT; l += step)
    drawCustomText(oled, 0, l, "Hello, World " + String((l / 8) + 1) + "!");
  oled.display();
}

void drawObject3D(const float (*verts)[3], int vcount, const int (*edges)[2], int ecount, float angleX, float angleY, float angleZ = 0.0f) {
  oled.clearDisplay();
  const float cx = OLED_WIDTH / 2.0f, cy = OLED_HEIGHT / 2.0f, zd = 60.0f;
  float sx = sin(angleX), cx1 = cos(angleX), sy = sin(angleY), cy1 = cos(angleY), sz = sin(angleZ), cz = cos(angleZ);
  static int px[300], py[300];
  int lim = vcount > 300 ? 300 : vcount;
  for (int i = 0; i < lim; i++) {
    float x = verts[i][0], y = verts[i][1], z = verts[i][2];
    float x0 = x * cz - y * sz;
    float y0 = x * sz + y * cz;
    float y1 = y0 * cx1 - z * sx;
    float z1 = y0 * sx + z * cx1;
    float x2 = x0 * cy1 + z1 * sy;
    float z2 = -x0 * sy + z1 * cy1;
    float f = zd / (zd + z2);
    px[i] = x2 * f + cx;
    py[i] = y1 * f + cy;
  }
  for (int e = 0; e < ecount; e++) {
    int a = edges[e][0], b = edges[e][1];
    if (a < lim && b < lim) oled.drawLine(px[a], py[a], px[b], py[b], SSD1306_WHITE);
  }
  oled.display();
}

static void smiley_project(float x, float y, float z, float angleX, float angleY, float &px, float &py, float &zcam) {
  const float cx = 64.0f, cy = 32.0f, zd = 60.0f;
  float sx = sin(angleX), cx1 = cos(angleX);
  float sy = sin(angleY), cy1 = cos(angleY);
  float y1 = y * cx1 - z * sx;
  float z1 = y * sx + z * cx1;
  float x2 = x * cy1 + z1 * sy;
  float z2 = -x * sy + z1 * cy1;
  float f = zd / (zd + z2);
  px = x2 * f + cx;
  py = cy - y1 * f;
  zcam = z2;
}

static void smiley_point(float x, float y, float z, float roll, float ax, float ay, float &px, float &py, float &zcam) {
  float cr = cos(roll), sr = sin(roll);
  float xr = x * cr - y * sr;
  float yr = x * sr + y * cr;
  smiley_project(xr, yr, z, ax, ay, px, py, zcam);
}

static void smiley_fill_oval3d(float cx, float cy, float cz, float rx, float ry, float roll, float ax, float ay, uint16_t col) {
  const float step = 0.35f;
  for (float u = -1.0f; u <= 1.0f; u += step) {
    for (float v = -1.0f; v <= 1.0f; v += step) {
      if (u * u + v * v <= 1.0f) {
        float px, py, zc;
        smiley_point(cx + u * rx, cy + v * ry, cz, roll, ax, ay, px, py, zc);
        if (zc > 0.0f) {
          int ix = (int)px, iy = (int)py;
          if (ix >= 0 && ix < OLED_WIDTH && iy >= 0 && iy < OLED_HEIGHT) {
            oled.drawPixel(ix, iy, col);
          }
        }
      }
    }
  }
}

void drawSmiley3D(float t, float scale) {
  const float R = 27.0f * scale;
  const float S = R / 22.0f;
  const float faceZ = 19.0f * S;
  const float maxTilt = 60.0f * 0.017453292f;
  const float ax = sin(t) * maxTilt;
  const float ay = sin(t * 0.85f) * maxTilt;
  const float roll = sin(t * 0.5f) * (float)(2.0 * M_PI);

  oled.clearDisplay();

  // Solid sphere body (white = yellow on the button)
  for (int i = 0; i <= 24; i++) {
    float phi = (i / 24.0f) * (float)M_PI;
    for (int j = 0; j < 32; j++) {
      float th = (j / 32.0f) * 2.0f * (float)M_PI;
      float x = R * sin(phi) * cos(th);
      float y = R * cos(phi);
      float z = R * sin(phi) * sin(th);
      float px, py, zc;
      smiley_point(x, y, z, roll, ax, ay, px, py, zc);
      if (zc > -8.0f * S) {
        int ix = (int)px, iy = (int)py;
        for (int ddy = -1; ddy <= 1; ddy++) {
          for (int ddx = -1; ddx <= 1; ddx++) {
            int xx = ix + ddx, yy = iy + ddy;
            if (xx >= 0 && xx < OLED_WIDTH && yy >= 0 && yy < OLED_HEIGHT) {
              oled.drawPixel(xx, yy, SSD1306_WHITE);
            }
          }
        }
      }
    }
  }

  // Thick smile arc (black)
  for (int deg = 205; deg <= 335; deg += 2) {
    float rad = deg * 0.017453292f;
    for (int k = 0; k < 4; k++) {
      float rr = (12.0f + k * 0.85f) * S;
      float x = rr * cos(rad);
      float y = -7.0f * S + rr * sin(rad);
      float px, py, zc;
      smiley_point(x, y, faceZ, roll, ax, ay, px, py, zc);
      if (zc > 0.0f) {
        int ix = (int)px, iy = (int)py;
        oled.drawPixel(ix, iy, SSD1306_BLACK);
        if (ix + 1 < OLED_WIDTH) oled.drawPixel(ix + 1, iy, SSD1306_BLACK);
      }
    }
  }

  // Smile corner ticks (black)
  const float tickDeg[2] = {208.0f, 332.0f};
  for (int k = 0; k < 2; k++) {
    float rad = tickDeg[k] * 0.017453292f;
    float bx = 13.5f * S * cos(rad);
    float by = -7.0f * S + 13.5f * S * sin(rad);
    float px0, py0, z0, px1, py1, z1;
    smiley_point(bx, by, faceZ, roll, ax, ay, px0, py0, z0);
    smiley_point(bx + (k == 0 ? -3.0f : 3.0f) * S, by + 2.5f * S, faceZ, roll, ax, ay, px1, py1, z1);
    if (z0 > 0.0f && z1 > 0.0f) {
      oled.drawLine((int)px0, (int)py0, (int)px1, (int)py1, SSD1306_BLACK);
    }
  }

  // Oval eyes (black) — drawn in face space so they roll with the head
  const float eyeX[2] = {-7.0f * S, 7.0f * S};
  for (int e = 0; e < 2; e++) {
    smiley_fill_oval3d(eyeX[e], 7.0f * S, faceZ, 2.8f * S, 5.2f * S, roll, ax, ay, SSD1306_BLACK);
  }

  // Gloss highlight upper-right (white)
  smiley_fill_oval3d(8.0f * S, 11.0f * S, 16.0f * S, 4.0f * S, 2.5f * S, roll, ax, ay, SSD1306_WHITE);

  oled.display();
}

void drawTorusWireframe(float ang) {
  const int U = 24, V = 12;
  const float R = 18.0f, r = 8.0f;
  static float verts[288][3];
  static int edges[576][2];
  static bool built = false;
  if (!built) {
    int v = 0;
    for (int i = 0; i < U; i++) {
      float u = (i / (float)U) * 2 * M_PI;
      for (int j = 0; j < V; j++) {
        float vv = (j / (float)V) * 2 * M_PI;
        verts[v][0] = (R + r * cos(vv)) * cos(u);
        verts[v][1] = (R + r * cos(vv)) * sin(u);
        verts[v][2] = r * sin(vv);
        v++;
      }
    }
    int e = 0;
    for (int i = 0; i < U; i++) {
      for (int j = 0; j < V; j++) {
        int a = i * V + j, b = ((i + 1) % U) * V + j, c = i * V + (j + 1) % V;
        edges[e][0] = a;
        edges[e][1] = b;
        e++;
        edges[e][0] = a;
        edges[e][1] = c;
        e++;
      }
    }
    built = true;
  }
  drawObject3D(verts, U * V, edges, U * V * 2, ang, ang * 0.7f);
}

void drawTeapotWireframe(float ang) {
  const int RADIAL = 16;
  const float prof[7][2] = {{0, -16}, {8, -14}, {13, -8}, {13, 4}, {10, 10}, {5, 12}, {0, 12}};
  static float verts[130][3];
  static int edges[260][2];
  static int vcount, ecount;
  static bool built = false;
  if (!built) {
    int v = 0;
    verts[v][0] = 0;
    verts[v][1] = prof[0][1];
    verts[v][2] = 0;
    v++;
    for (int p = 1; p < 6; p++) {
      float pr = prof[p][0], py = prof[p][1];
      for (int r = 0; r < RADIAL; r++) {
        float th = (r / (float)RADIAL) * 2 * M_PI;
        verts[v][0] = pr * cos(th);
        verts[v][1] = py;
        verts[v][2] = pr * sin(th);
        v++;
      }
    }
    verts[v][0] = 0;
    verts[v][1] = prof[6][1];
    verts[v][2] = 0;
    v++;
    vcount = v;
    int ec = 0;
    int base = 1;
    for (int r = 0; r < RADIAL; r++) {
      int b0 = base + r, b1 = base + (r + 1) % RADIAL;
      edges[ec][0] = 0;
      edges[ec][1] = b0;
      ec++;
      edges[ec][0] = b0;
      edges[ec][1] = b1;
      ec++;
    }
    for (int p = 0; p < 4; p++) {
      int curr = 1 + p * RADIAL, nxt = curr + RADIAL;
      for (int r = 0; r < RADIAL; r++) {
        int c0 = curr + r, c1 = curr + (r + 1) % RADIAL, n0 = nxt + r;
        edges[ec][0] = c0;
        edges[ec][1] = n0;
        ec++;
        edges[ec][0] = c0;
        edges[ec][1] = c1;
        ec++;
      }
    }
    int lastRing = 1 + 4 * RADIAL;
    int topIdx = v - 1;
    for (int r = 0; r < RADIAL; r++) {
      int c0 = lastRing + r, c1 = lastRing + (r + 1) % RADIAL;
      edges[ec][0] = c0;
      edges[ec][1] = c1;
      ec++;
      edges[ec][0] = c0;
      edges[ec][1] = topIdx;
      ec++;
    }
    ecount = ec;
    built = true;
  }
  drawObject3D(verts, vcount, edges, ecount, ang, ang * 0.7f);
}

void drawStarfighterWireframe(float ang, float scale = 1.0f) {
  static float base[21][3] = {
      {22, 0, 0}, {12, 2, -3}, {12, -2, -3}, {12, 2, 3}, {12, -2, 3}, {-4, 0, -5},
      {-4, 0, 5}, {-18, 0, 0}, {-4, 3, 0}, {-4, -3, 0}, {0, 0, 12}, {-8, 0, 14},
      {-14, 0, 9}, {0, 0, -12}, {-8, 0, -14}, {-14, 0, -9}, {-14, 4, 0},
      {-20, 6, 0}, {-20, 0, 0}, {-14, -4, 0}, {-20, -6, 0}};
  float verts[21][3];
  for (int i = 0; i < 21; i++) {
    verts[i][0] = base[i][0] * scale;
    verts[i][1] = base[i][1] * scale;
    verts[i][2] = base[i][2] * scale;
  }
  static int edges[40][2] = {
      {0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {3, 4}, {1, 3}, {2, 4}, {1, 5}, {2, 5},
      {3, 6}, {4, 6}, {5, 7}, {6, 7}, {5, 8}, {6, 8}, {5, 9}, {6, 9}, {8, 7}, {9, 7},
      {5, 10}, {10, 11}, {11, 12}, {12, 5}, {5, 11}, {5, 13}, {13, 14}, {14, 15},
      {15, 5}, {5, 14}, {7, 16}, {16, 17}, {17, 18}, {18, 7}, {7, 19}, {19, 20}, {20, 18}};
  drawObject3D(verts, 21, edges, 37, ang * 0.9f, ang, ang * 1.2f);
}

void drawEnterpriseWireframe(float ang, float scale = 1.0f) {
  const int SEG = 16;
  static float verts[180][3];
  static int edges[380][2];
  static int vcount = 0, ecount = 0;
  static bool built = false;
  if (!built) {
    int v = 0, e = 0;
    float scx = 16.0f, outer = 14.0f, inner = 6.0f, topY = 1.3f, botY = -1.3f;
    int topOuter = v;
    for (int i = 0; i < SEG; i++) {
      float th = (i / (float)SEG) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * outer;
      verts[v][1] = topY;
      verts[v][2] = sin(th) * outer;
      v++;
    }
    int topInner = v;
    for (int i = 0; i < SEG; i++) {
      float th = (i / (float)SEG) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * inner;
      verts[v][1] = topY + 0.4f;
      verts[v][2] = sin(th) * inner;
      v++;
    }
    int botOuter = v;
    for (int i = 0; i < SEG; i++) {
      float th = (i / (float)SEG) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * outer;
      verts[v][1] = botY;
      verts[v][2] = sin(th) * outer;
      v++;
    }
    int botInner = v;
    for (int i = 0; i < SEG; i++) {
      float th = (i / (float)SEG) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * inner;
      verts[v][1] = botY - 0.4f;
      verts[v][2] = sin(th) * inner;
      v++;
    }
    int bridgeRim = v;
    for (int i = 0; i < SEG; i++) {
      float th = (i / (float)SEG) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * 2.2f;
      verts[v][1] = topY + 1.2f;
      verts[v][2] = sin(th) * 2.2f;
      v++;
    }
    int bridgeTop = v;
    verts[v][0] = scx;
    verts[v][1] = topY + 2.3f;
    verts[v][2] = 0;
    v++;
    int sensorRim = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = scx + cos(th) * 2.0f;
      verts[v][1] = botY - 1.0f;
      verts[v][2] = sin(th) * 2.0f;
      v++;
    }
    int sensorBot = v;
    verts[v][0] = scx;
    verts[v][1] = botY - 2.0f;
    verts[v][2] = 0;
    v++;
    int secFT = v;
    verts[v][0] = 6;
    verts[v][1] = -4.5f;
    verts[v][2] = 0;
    v++;
    int secFB = v;
    verts[v][0] = 6;
    verts[v][1] = -7.5f;
    verts[v][2] = 0;
    v++;
    int secRT = v;
    verts[v][0] = -14;
    verts[v][1] = -4.5f;
    verts[v][2] = 0;
    v++;
    int secRB = v;
    verts[v][0] = -14;
    verts[v][1] = -8.0f;
    verts[v][2] = 0;
    v++;
    int defFront = v;
    verts[v][0] = -1;
    verts[v][1] = -6.0f;
    verts[v][2] = 0;
    v++;
    int defRim = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = 2 + cos(th) * 0.8f;
      verts[v][1] = -6.0f + sin(th) * 1.6f;
      verts[v][2] = 0;
      v++;
    }
    int nacL_F = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = 5 + cos(th) * 1.0f;
      verts[v][1] = -3.2f + sin(th) * 1.0f;
      verts[v][2] = 9.5f;
      v++;
    }
    int nacL_R = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = -18 + cos(th) * 1.0f;
      verts[v][1] = -3.2f + sin(th) * 1.0f;
      verts[v][2] = 9.5f;
      v++;
    }
    int nacR_F = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = 5 + cos(th) * 1.0f;
      verts[v][1] = -3.2f + sin(th) * 1.0f;
      verts[v][2] = -9.5f;
      v++;
    }
    int nacR_R = v;
    for (int i = 0; i < 8; i++) {
      float th = (i / 8.0f) * 2 * M_PI;
      verts[v][0] = -18 + cos(th) * 1.0f;
      verts[v][1] = -3.2f + sin(th) * 1.0f;
      verts[v][2] = -9.5f;
      v++;
    }
    int nacL_Bus = v;
    verts[v][0] = 7.5f;
    verts[v][1] = -3.2f;
    verts[v][2] = 9.5f;
    v++;
    int nacR_Bus = v;
    verts[v][0] = 7.5f;
    verts[v][1] = -3.2f;
    verts[v][2] = -9.5f;
    v++;
    int pylonL_H = v;
    verts[v][0] = -7;
    verts[v][1] = -5.0f;
    verts[v][2] = 2.5f;
    v++;
    int pylonL_N = v;
    verts[v][0] = -9;
    verts[v][1] = -3.8f;
    verts[v][2] = 8.0f;
    v++;
    int pylonR_H = v;
    verts[v][0] = -7;
    verts[v][1] = -5.0f;
    verts[v][2] = -2.5f;
    v++;
    int pylonR_N = v;
    verts[v][0] = -9;
    verts[v][1] = -3.8f;
    verts[v][2] = -8.0f;
    v++;
    for (int i = 0; i < SEG; i++) {
      int n = (i + 1) % SEG;
      edges[e][0] = topOuter + i;
      edges[e][1] = topOuter + n;
      e++;
      edges[e][0] = botOuter + i;
      edges[e][1] = botOuter + n;
      e++;
    }
    for (int i = 0; i < SEG; i++) {
      int n = (i + 1) % SEG;
      edges[e][0] = topInner + i;
      edges[e][1] = topInner + n;
      e++;
      edges[e][0] = botInner + i;
      edges[e][1] = botInner + n;
      e++;
    }
    for (int i = 0; i < SEG; i++) {
      edges[e][0] = topOuter + i;
      edges[e][1] = topInner + i;
      e++;
      edges[e][0] = botOuter + i;
      edges[e][1] = botInner + i;
      e++;
      edges[e][0] = topOuter + i;
      edges[e][1] = botOuter + i;
      e++;
    }
    for (int i = 0; i < SEG; i += 2) {
      edges[e][0] = topOuter + i;
      edges[e][1] = botOuter + (i + 8) % SEG;
      e++;
    }
    for (int i = 0; i < SEG; i++) {
      int n = (i + 1) % SEG;
      edges[e][0] = bridgeRim + i;
      edges[e][1] = bridgeRim + n;
      e++;
      edges[e][0] = bridgeRim + i;
      edges[e][1] = bridgeTop;
      e++;
      edges[e][0] = bridgeRim + i;
      edges[e][1] = topInner + i;
      e++;
    }
    for (int i = 0; i < 8; i++) {
      int n = (i + 1) % 8;
      edges[e][0] = sensorRim + i;
      edges[e][1] = sensorRim + n;
      e++;
      edges[e][0] = sensorRim + i;
      edges[e][1] = sensorBot;
      e++;
    }
    edges[e][0] = secFT;
    edges[e][1] = secRT;
    e++;
    edges[e][0] = secFB;
    edges[e][1] = secRB;
    e++;
    edges[e][0] = secFT;
    edges[e][1] = secFB;
    e++;
    edges[e][0] = secRT;
    edges[e][1] = secRB;
    e++;
    edges[e][0] = secFT;
    edges[e][1] = defFront;
    e++;
    edges[e][0] = secFB;
    edges[e][1] = defFront;
    e++;
    for (int i = 0; i < 8; i++) {
      int n = (i + 1) % 8;
      edges[e][0] = defRim + i;
      edges[e][1] = defRim + n;
      e++;
      edges[e][0] = defRim + i;
      edges[e][1] = defFront;
      e++;
    }
    edges[e][0] = topInner + 12;
    edges[e][1] = secFT;
    e++;
    edges[e][0] = topInner + 4;
    edges[e][1] = secFT;
    e++;
    edges[e][0] = botInner + 12;
    edges[e][1] = secFB;
    e++;
    for (int i = 0; i < 8; i++) {
      int n = (i + 1) % 8;
      edges[e][0] = nacL_F + i;
      edges[e][1] = nacL_F + n;
      e++;
      edges[e][0] = nacL_R + i;
      edges[e][1] = nacL_R + n;
      e++;
      edges[e][0] = nacL_F + i;
      edges[e][1] = nacL_R + i;
      e++;
    }
    for (int i = 0; i < 8; i++) {
      int n = (i + 1) % 8;
      edges[e][0] = nacR_F + i;
      edges[e][1] = nacR_F + n;
      e++;
      edges[e][0] = nacR_R + i;
      edges[e][1] = nacR_R + n;
      e++;
      edges[e][0] = nacR_F + i;
      edges[e][1] = nacR_R + i;
      e++;
    }
    for (int i = 0; i < 8; i++) {
      edges[e][0] = nacL_F + i;
      edges[e][1] = nacL_Bus;
      e++;
      edges[e][0] = nacR_F + i;
      edges[e][1] = nacR_Bus;
      e++;
    }
    edges[e][0] = secRT;
    edges[e][1] = pylonL_H;
    e++;
    edges[e][0] = pylonL_H;
    edges[e][1] = pylonL_N;
    e++;
    edges[e][0] = pylonL_N;
    edges[e][1] = nacL_F + 2;
    e++;
    edges[e][0] = pylonL_N;
    edges[e][1] = nacL_R + 2;
    e++;
    edges[e][0] = secRT;
    edges[e][1] = pylonR_H;
    e++;
    edges[e][0] = pylonR_H;
    edges[e][1] = pylonR_N;
    e++;
    edges[e][0] = pylonR_N;
    edges[e][1] = nacR_F + 2;
    e++;
    edges[e][0] = pylonR_N;
    edges[e][1] = nacR_R + 2;
    e++;
    vcount = v;
    ecount = e;
    built = true;
  }
  float v2[180][3];
  for (int i = 0; i < vcount; i++) {
    v2[i][0] = verts[i][0] * scale;
    v2[i][1] = verts[i][1] * scale;
    v2[i][2] = verts[i][2] * scale;
  }
  drawObject3D(v2, vcount, edges, ecount, ang * 0.9f, ang, ang * 0.2f);
}

void drawCube3D(float ang) {
  const float s = 20.0f;
  float v[8][3] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, s},
                   {s, -s, s}, {s, s, s}, {-s, s, s}};
  int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  drawObject3D(v, 8, e, 12, ang, ang * 0.7f);
}

void drawPyramid3D(float ang) {
  const float s = 22.0f;
  float v[5][3] = {{-s, -s, -s}, {s, -s, -s}, {s, -s, s}, {-s, -s, s}, {0, s, 0}};
  int e[8][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}};
  drawObject3D(v, 5, e, 8, ang, ang * 0.7f);
}

void drawOctahedron3D(float ang) {
  const float s = 20.0f;
  float v[6][3] = {{0, s, 0}, {0, -s, 0}, {s, 0, 0}, {-s, 0, 0}, {0, 0, s}, {0, 0, -s}};
  int e[12][2] = {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {2, 4}, {4, 3}, {3, 5}, {5, 2}};
  drawObject3D(v, 6, e, 12, ang, ang * 0.7f);
}

void drawSphere3D(float ang) {
  const int rings = 6, segs = 12;
  const float r = 22.0f;
  static float verts[72][3];
  static int edges[144][2];
  static bool built = false;
  static int vc, ec;
  if (!built) {
    vc = 0;
    for (int i = 0; i < rings; i++) {
      float phi = (i / (float)(rings - 1)) * M_PI;
      for (int j = 0; j < segs; j++) {
        float th = (j / (float)segs) * 2 * M_PI;
        verts[vc][0] = r * sin(phi) * cos(th);
        verts[vc][1] = r * cos(phi);
        verts[vc][2] = r * sin(phi) * sin(th);
        vc++;
      }
    }
    ec = 0;
    for (int i = 0; i < rings; i++) {
      for (int j = 0; j < segs; j++) {
        int a = i * segs + j, b = i * segs + ((j + 1) % segs);
        edges[ec][0] = a;
        edges[ec][1] = b;
        ec++;
        if (i < rings - 1) {
          int c = (i + 1) * segs + j;
          edges[ec][0] = a;
          edges[ec][1] = c;
          ec++;
        }
      }
    }
    built = true;
  }
  drawObject3D(verts, vc, edges, ec, ang, ang * 0.7f);
}

void setup() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
    for (;;)
      ;
  oled.clearDisplay();
  oled.display();
  oled.displayWait();
}

void loop() {
  oled.dim(false);
  clear_screen("black");
  hello_world();
  delay(2000);
  clear_screen("black");
  grid();
  oled.display();
  delay(1500);
  clear_screen("black");
  grid();
  grid_num();
  oled.display();
  delay(1500);
  flash_screen(4, 0.3f);
  clear_screen("black");
  uint32_t s = millis();
  while (millis() - s < 2500) {
    LissajousFrame(lissa_phase);
    delay(5);
  }
  s = millis();
  while (millis() - s < 8000) {
    uint32_t elapsed = millis() - s;
    float a = millis() * 0.002f;
    float scale = 1.0f;
    if (elapsed >= 1000 && elapsed < 1500) {
      scale = 1.0f + ((elapsed - 1000) / 500.0f) * 0.5f;
    } else if (elapsed >= 1500 && elapsed < 2000) {
      scale = 1.5f - ((elapsed - 1500) / 500.0f) * 1.0f;
    } else if (elapsed >= 2000 && elapsed < 2250) {
      scale = 0.5f + ((elapsed - 2000) / 250.0f) * 0.5f;
    }
    drawSmiley3D(a, scale);
    delay(20);
  }
  clear_screen("black");
  s = millis();
  while (millis() - s < 2000) {
    float a = millis() * 0.003f;
    drawCube3D(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 4000) {
    float a = millis() * 0.003f;
    drawPyramid3D(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 4000) {
    float a = millis() * 0.003f;
    drawOctahedron3D(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 4000) {
    float a = millis() * 0.003f;
    drawSphere3D(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 4000) {
    float a = millis() * 0.003f;
    drawTorusWireframe(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 3000) {
    float a = millis() * 0.003f;
    drawTeapotWireframe(a);
    delay(20);
  }
  s = millis();
  while (millis() - s < 8000) {
    float a = millis() * 0.002f;
    drawStarfighterWireframe(a, 2.0f);
    delay(20);
  }
  s = millis();
  while (millis() - s < 8000) {
    float a = millis() * 0.0022f;
    drawEnterpriseWireframe(a, 1.5f);
    delay(15);
  }
  clear_screen("black");
  oled.displayWait();
}
