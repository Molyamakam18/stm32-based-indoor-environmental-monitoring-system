/*
 * STM32F103 (Blue Pill) - Indoor Environmental Monitor  -- LEAN BUILD for 32 KB flash
 * No Adafruit libraries, no floating point.
 *
 * Needs ONE library: "U8g2" by oliver (Library Manager). Wire/Serial come with the core.
 *
 * BMP280 (I2C 0x76/0x77) | SSD1306 OLED (I2C 0x3C, text mode) | MQ-135 AOUT on PA0 (via divider)
 * LEDs: PA1 green, PA2 yellow, PA3 red | Active buzzer via 2N2222 on PA4
 * Serial = USART1: PA9 (TX) -> ESP32 GPIO16 (RX2), PA10 (RX) <- ESP32 GPIO17 (optional)
 * Frame to ESP32 (unchanged):  T=27.4,P=1008,AQ=1520,S=NORMAL
 *
 * Tools menu: USB support = None, U(S)ART support = Enabled (generic 'Serial'),
 *             C Runtime Library = Newlib Nano, Optimize = Smallest (-Os)
 */
#include <Wire.h>
#include <U8x8lib.h>

#define PIN_MQ      PA0
#define PIN_LED_G   PA1
#define PIN_LED_Y   PA2
#define PIN_LED_R   PA3
#define PIN_BUZZER  PA4

// ---- Thresholds (integers). TUNE AQ values after measuring your clean-air baseline ----
const int32_t T_WARN_C100 = 3000;      // 30.00 C  (centi-degrees)
const int32_t T_CRIT_C100 = 3500;      // 35.00 C
const int     AQ_WARN     = 1500;      // raw ADC counts (0..4095)
const int     AQ_CRIT     = 2800;
const int32_t P_LOW_PA    = 98000;     // 980 hPa
const int32_t P_HIGH_PA   = 105000;    // 1050 hPa
const uint32_t WARMUP_MS  = 60000;     // gas channel ignored while MQ-135 heats up

enum Status { NORMAL = 0, WARNING = 1, CRITICAL = 2 };

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);

// ---------------- minimal BMP280 driver (integer maths, datasheet 32-bit formulas) ----------------
uint8_t  bmpAddr = 0;
uint16_t dig_T1, dig_P1;
int16_t  dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
int32_t  t_fine;

bool bmpRead(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(bmpAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)bmpAddr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
void bmpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(bmpAddr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}
bool bmpTryAddr(uint8_t a) {
  bmpAddr = a;
  uint8_t id;
  if (!bmpRead(0xD0, &id, 1)) return false;
  return (id == 0x58 || id == 0x60);            // BMP280 / BME280 (temp+pressure used only)
}
bool bmpInit() {
  if (!bmpTryAddr(0x76) && !bmpTryAddr(0x77)) return false;
  uint8_t c[24];
  if (!bmpRead(0x88, c, 24)) return false;
  dig_T1 = (uint16_t)(c[1] << 8 | c[0]);  dig_T2 = (int16_t)(c[3] << 8 | c[2]);  dig_T3 = (int16_t)(c[5] << 8 | c[4]);
  dig_P1 = (uint16_t)(c[7] << 8 | c[6]);  dig_P2 = (int16_t)(c[9] << 8 | c[8]);  dig_P3 = (int16_t)(c[11] << 8 | c[10]);
  dig_P4 = (int16_t)(c[13] << 8 | c[12]); dig_P5 = (int16_t)(c[15] << 8 | c[14]); dig_P6 = (int16_t)(c[17] << 8 | c[16]);
  dig_P7 = (int16_t)(c[19] << 8 | c[18]); dig_P8 = (int16_t)(c[21] << 8 | c[20]); dig_P9 = (int16_t)(c[23] << 8 | c[22]);
  bmpWrite(0xF5, 0xA0);                          // standby 1000 ms, filter off
  bmpWrite(0xF4, 0x27);                          // temp x1, pressure x1, normal mode
  return true;
}
// returns temperature in 0.01 C and pressure in Pa
bool bmpMeasure(int32_t &t100, int32_t &pPa) {
  uint8_t d[6];
  if (!bmpRead(0xF7, d, 6)) return false;
  int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
  int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);

  int32_t v1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
  int32_t v2 = (((((adc_T >> 4) - (int32_t)dig_T1) * ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
  t_fine = v1 + v2;
  t100 = (t_fine * 5 + 128) >> 8;

  v1 = (t_fine >> 1) - 64000;
  v2 = (((v1 >> 2) * (v1 >> 2)) >> 11) * (int32_t)dig_P6;
  v2 = v2 + ((v1 * (int32_t)dig_P5) << 1);
  v2 = (v2 >> 2) + ((int32_t)dig_P4 << 16);
  v1 = (((dig_P3 * (((v1 >> 2) * (v1 >> 2)) >> 13)) >> 3) + (((int32_t)dig_P2 * v1) >> 1)) >> 18;
  v1 = ((32768 + v1) * (int32_t)dig_P1) >> 15;
  if (v1 == 0) return false;
  uint32_t p = (((uint32_t)(((int32_t)1048576) - adc_P) - (v2 >> 12))) * 3125;
  if (p < 0x80000000UL) p = (p << 1) / (uint32_t)v1;
  else                  p = (p / (uint32_t)v1) * 2;
  v1 = ((int32_t)dig_P9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
  v2 = ((int32_t)(p >> 2) * (int32_t)dig_P8) >> 13;
  pPa = (int32_t)((int32_t)p + ((v1 + v2 + dig_P7) >> 4));
  return true;
}

// ---------------- helpers ----------------
bool bmpOk = false;

void printTenths(Print &out, int32_t c100) {     // 2740 -> "27.4"
  bool neg = c100 < 0;
  if (neg) c100 = -c100;
  int32_t v = (c100 + 5) / 10;
  if (neg) out.print('-');
  out.print((long)(v / 10));
  out.print('.');
  out.print((long)(v % 10));
}

Status evaluate(int32_t t100, int32_t pPa, int aq, bool warm) {
  bool gasCrit = warm && aq >= AQ_CRIT;
  bool gasWarn = warm && aq >= AQ_WARN;
  if (t100 >= T_CRIT_C100 || gasCrit) return CRITICAL;
  if (t100 >= T_WARN_C100 || gasWarn || pPa < P_LOW_PA || pPa > P_HIGH_PA) return WARNING;
  return NORMAL;
}

const char *statusText(Status s) {
  if (s == NORMAL) return "NORMAL";
  if (s == WARNING) return "WARNING";
  return "CRITICAL";
}

void setLeds(Status s) {
  digitalWrite(PIN_LED_G, s == NORMAL   ? HIGH : LOW);
  digitalWrite(PIN_LED_Y, s == WARNING  ? HIGH : LOW);
  digitalWrite(PIN_LED_R, s == CRITICAL ? HIGH : LOW);
}

void handleBuzzer(Status s) {                    // active buzzer: plain on/off, 500 ms beeps
  bool on = (s == CRITICAL) && ((millis() / 500) % 2 == 0);
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void drawScreen(int32_t t100, int32_t pPa, int aq, Status s, bool warm) {
  u8x8.clearLine(0); u8x8.setCursor(0, 0); u8x8.print(warm ? "Env Monitor" : "Warming up..");
  u8x8.clearLine(2); u8x8.setCursor(0, 2); u8x8.print("Temp: "); printTenths(u8x8, t100); u8x8.print(" C");
  u8x8.clearLine(3); u8x8.setCursor(0, 3); u8x8.print("Pres: "); u8x8.print((long)((pPa + 50) / 100)); u8x8.print(" hPa");
  u8x8.clearLine(4); u8x8.setCursor(0, 4); u8x8.print("AirQ: "); u8x8.print(aq);
  u8x8.clearLine(6); u8x8.setCursor(0, 6); u8x8.print(statusText(s));
}

// ---------------- main ----------------
void setup() {
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_Y, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_MQ, INPUT_ANALOG);
  analogReadResolution(12);

  Serial.begin(115200);                          // USART1 -> ESP32
  Wire.begin();                                  // I2C1: PB6 SCL, PB7 SDA

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  bmpOk = bmpInit();
  u8x8.setCursor(0, 1); u8x8.print("Starting...");
  u8x8.setCursor(0, 3); u8x8.print(bmpOk ? "BMP280: OK" : "BMP280: NOT FOUND");

  digitalWrite(PIN_LED_G, HIGH); delay(250);     // self-test
  digitalWrite(PIN_LED_Y, HIGH); delay(250);
  digitalWrite(PIN_LED_R, HIGH); delay(250);
  digitalWrite(PIN_BUZZER, HIGH); delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  setLeds(NORMAL);
  delay(1000);
  u8x8.clear();
}

void loop() {
  static uint32_t lastSample = 0;
  static Status status = NORMAL;

  if (millis() - lastSample >= 1000) {
    lastSample = millis();

    int32_t t100 = 2500, pPa = 101300;           // defaults if the BMP280 is missing
    if (bmpOk) bmpMeasure(t100, pPa);

    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(PIN_MQ);
    int aq = (int)(sum / 8);

    bool warm = millis() > WARMUP_MS;
    status = evaluate(t100, pPa, aq, warm);
    setLeds(status);
    drawScreen(t100, pPa, aq, status, warm);

    Serial.print("T=");   printTenths(Serial, t100);
    Serial.print(",P=");  Serial.print((long)((pPa + 50) / 100));
    Serial.print(",AQ="); Serial.print(aq);
    Serial.print(",S=");  Serial.println(statusText(status));
  }
  handleBuzzer(status);
}
