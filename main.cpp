/*
  ============================================================
  CENTRO DE CONTROL DE FOTOPERIODO - ESP32-S3 / PRUEBA DE INTERFAZ
  ============================================================
  Portado desde el controlador original v4.0.

  Conserva:
    - Fotoperiodo LUZ/OSC
    - VEG/FLOR
    - Días VEG/FLOR
    - RTC DS3231 + NTP
    - Historial 24 h
    - Persistencia en microSD
    - Barra de progreso
    - Salvapantallas nocturno
    - Caritas originales del Weedagotchi
    - ESP-NOW con ESP32 principal y ESP32-C3
    - Control de relés del ESP32 principal

  Adaptación al VIEWE UEDX24320028E-WB-A / familia mostrada:
    - Pantalla/touch mediante ESP32_Display_Panel + LVGL
    - Touch capacitivo CHSC6540
    - SD por SPI de la ranura integrada
    - DS3231 externo en GPIO6/7
    - NO se usa GPIO4 para relé: GPIO4 pertenece al touch
    - El relé de iluminación se ordena al ESP32 principal por ESP-NOW

  IMPORTANTE:
    El archivo lvgl_v8_port.h de tu proyecto debe seguir siendo el
    que ya te funciona con la pantalla. No se usa lcd->setMirror().
  ============================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <math.h>
#include <pgmspace.h>
#include <string.h>

using namespace esp_panel::board;

// ============================================================
// MACs
// ============================================================

static uint8_t MAC_PRINCIPAL[] = {
    0x00, 0x4B, 0x12, 0x3D, 0x19, 0xFC
};

static uint8_t MAC_C3[] = {
    0xAC, 0xA7, 0x04, 0xB8, 0x0C, 0xAC
};

static uint8_t MAC_CALENDARIO[] = {
    0xE0, 0x72, 0xA1, 0xE7, 0x7C, 0x8C
};

// ============================================================
// GPIO EXTERNOS / PERIFERICOS
// ============================================================

// DS3231 EXTERNO.
// Se evita GPIO1/2/3/4 porque pertenecen al touch de la placa.
#define RTC_SDA 6
#define RTC_SCL 7

// SD integrada de la placa.
// El fabricante documenta estas cuatro líneas para la ranura.
#define SD_CS   15
#define SD_SCK  18
#define SD_MISO 16
#define SD_MOSI 17

// Buzzer integrado, si se desea usarlo.
#define BUZZER_PIN 38

// LED RGB integrado: WS2812 en GPIO0.
// Se deja reservado; no es necesario para el controlador.
#define RGB_LED_PIN 0

// ============================================================
// COLORES
// ============================================================

#define C_BG       lv_color_hex(0x000000)
#define C_WHITE    lv_color_hex(0xFFFFFF)
#define C_GRAY     lv_color_hex(0x526066)
#define C_GRAY2    lv_color_hex(0x20272B)
#define C_GREEN    lv_color_hex(0x07E000)
#define C_LIME     lv_color_hex(0x4FE000)
#define C_AMBER    lv_color_hex(0xFFE000)
#define C_RED      lv_color_hex(0xFF3030)
#define C_RED_DARK lv_color_hex(0x600000)
#define C_BLUE     lv_color_hex(0x2040FF)
#define C_CYAN     lv_color_hex(0x00FFFF)
#define C_PURPLE   lv_color_hex(0xB000FF)
#define C_DARKFACE lv_color_hex(0x032000)

// ============================================================
// SPRITES ORIGINALES
// ============================================================

static const uint8_t PROGMEM happyFace_bmp[] = {
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70,
    0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0,
    0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF,
    0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80,
    0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF,
    0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80,
    0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF,
    0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF,
    0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F,
    0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00,
    0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F,
    0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00,
    0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F,
    0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00,
    0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x3F, 0xE0, 0x0F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xE0,
    0x0F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xE0, 0x0F, 0xF8, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03,
    0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t PROGMEM happyFace_outline_bmp[] = {
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70,
    0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE,
    0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF,
    0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE,
    0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF,
    0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0,
    0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF,
    0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F,
    0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00
};

static const uint8_t PROGMEM angryFace_bmp[] = {
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70,
    0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0,
    0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF,
    0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80,
    0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF,
    0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80,
    0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF,
    0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF,
    0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F,
    0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00,
    0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F,
    0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00,
    0xFF, 0xF0, 0x3F, 0x03, 0x81, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x03,
    0x81, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x03, 0x81, 0xF8, 0x1F, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00,
    0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF,
    0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xF8, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03,
    0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t PROGMEM angryFace_outline_bmp[] = {
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70,
    0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE,
    0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF,
    0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE,
    0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF,
    0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0,
    0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF,
    0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F,
    0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00
};

static const uint8_t PROGMEM sadFace_bmp[] = {
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70,
    0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0,
    0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF,
    0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80,
    0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF,
    0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80,
    0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF,
    0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF,
    0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F,
    0xF1, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0x1F, 0xF1, 0xFF, 0xE0, 0x00,
    0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F,
    0xF1, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x1F, 0xF1, 0xFF, 0x00, 0x00,
    0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F,
    0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00,
    0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x3F, 0x1F, 0xF1, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x1F,
    0xF1, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x1F, 0xF1, 0xF8, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03,
    0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t PROGMEM sadFace_outline_bmp[] = {
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70,
    0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE,
    0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF,
    0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE,
    0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF,
    0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0,
    0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF,
    0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F,
    0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00
};

static const uint8_t PROGMEM deadFace_bmp[] = {
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x70,
    0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xF0, 0x1F, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xF0,
    0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF,
    0xFE, 0x00, 0x1F, 0x80, 0x03, 0xF0, 0x00, 0xFF, 0xFE, 0x00, 0x1F, 0x80,
    0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF,
    0xFE, 0x00, 0xFF, 0x80, 0x03, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0x80,
    0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF,
    0xFE, 0x07, 0xFF, 0x80, 0x03, 0xFF, 0xC0, 0xFF, 0xFE, 0x07, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF,
    0xFE, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0xFF, 0xFE, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x01, 0xF8, 0xE3, 0x8E, 0x3F, 0x00, 0x00, 0x00, 0x01, 0xF8, 0xE3,
    0x8E, 0x3F, 0x00, 0x00, 0x00, 0x01, 0xF8, 0xE3, 0x8E, 0x3F, 0x00, 0x00,
    0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F,
    0xF1, 0xF8, 0x1F, 0xFE, 0xFF, 0xF0, 0x3F, 0x1F, 0xF1, 0xF8, 0x1F, 0xFE,
    0x1F, 0xFF, 0xF8, 0xE3, 0x8E, 0x3F, 0xFF, 0xF0, 0x1F, 0xFF, 0xF8, 0xE3,
    0x8E, 0x3F, 0xFF, 0xF0, 0x1F, 0xFF, 0xF8, 0xE3, 0x8E, 0x3F, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x38, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00,
    0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x38, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03,
    0x80, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x03, 0x80, 0x3F, 0xFC, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t PROGMEM deadFace_outline_bmp[] = {
    0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF,
    0xFE, 0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x70,
    0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF,
    0xFE, 0x00, 0x03, 0xFE, 0xFF, 0x80, 0x00, 0xFF, 0xFE, 0x00, 0x03, 0xFE,
    0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF,
    0xFF, 0xC0, 0x1F, 0xFE, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xC0, 0x1F, 0xFE,
    0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF,
    0xFF, 0xC0, 0xFF, 0xF0, 0x1F, 0xFE, 0x07, 0xFF, 0xFF, 0xC0, 0xFF, 0xF0,
    0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF,
    0xFF, 0xC7, 0xFF, 0xF0, 0x1F, 0xFF, 0xC7, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F,
    0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xF8, 0x1F, 0xF0, 0x3F, 0xFC, 0x00
};

#define SPRITE_W 63
#define SPRITE_H 57

// ============================================================
// ESTRUCTURAS ESP-NOW
// ============================================================

// Datos que el ESP32 principal ya utiliza.
typedef struct {
    float temperature;
    float humidity;
    float tds;
    float vpd;
    bool relay1;
    bool relay2;
    bool humidifier;
} SensorData;

// Datos que envía el ESP32-C3.
typedef struct {
    uint8_t deviceID;
    float soil1;
    float soil2;
    float co2;
} SoilData;

// Ordenes del S3 -> ESP32 principal.
typedef struct {
    bool relay1;
    bool relay2;
    bool humidifier;
} ControlData;

// Estructuras del controlador original, conservadas para
// mantener compatibilidad con otros nodos que las utilicen.
typedef struct {
    int lightHours;
    int darkHours;
    int daysVeg;
    int daysFlower;
    bool isVegetative;
    bool inLightMode;
    float progressPercent;
    int hour;
    int minute;
    int second;
    float vpd;
} GrowData;

typedef struct {
    float soil1;
    float soil2;
    float co2;
} LegacySoilData;

// ============================================================
// OBJETOS
// ============================================================

// ============================================================
// MODO PRUEBA DE INTERFAZ
// ============================================================
// Esta versión NO necesita RTC, SD ni sensores conectados.
// Se usa un reloj simulado basado en la hora de compilación,
// únicamente para poder probar toda la interfaz táctil.

class MockDateTime {
public:
    time_t epoch;

    MockDateTime() : epoch(0) {}
    explicit MockDateTime(time_t e) : epoch(e) {}

    template <typename A, typename B>
    MockDateTime(A, B) {
        struct tm t = {};
        // Para la prueba no necesitamos parsear __DATE__/__TIME__.
        // El reloj arranca en una fecha válida fija.
        t.tm_year = 126;  // 2026
        t.tm_mon  = 8;    // septiembre
        t.tm_mday = 4;
        t.tm_hour = 12;
        t.tm_min  = 0;
        t.tm_sec  = 0;
        epoch = mktime(&t);
    }

    MockDateTime(int y, int mo, int d, int h, int mi, int sec) {
        struct tm t = {};
        t.tm_year = y - 1900;
        t.tm_mon  = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = sec;
        epoch = mktime(&t);
    }

    int year() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_year + 1900;
    }
    int month() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_mon + 1;
    }
    int day() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_mday;
    }
    int hour() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_hour;
    }
    int minute() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_min;
    }
    int second() const {
        struct tm t;
        localtime_r(&epoch, &t);
        return t.tm_sec;
    }
    time_t unixtime() const {
        return epoch;
    }
};

class MockRTC {
public:
    time_t baseEpoch = 0;
    unsigned long baseMillis = 0;
    bool initialized = false;

    bool begin() {
        struct tm t = {};
        t.tm_year = 126;
        t.tm_mon  = 8;
        t.tm_mday = 4;
        t.tm_hour = 12;
        t.tm_min = 0;
        t.tm_sec = 0;
        baseEpoch = mktime(&t);
        baseMillis = millis();
        initialized = true;
        return true;
    }

    bool lostPower() const {
        return false;
    }

    MockDateTime now() const {
        return MockDateTime(baseEpoch +
                            (time_t)((millis() - baseMillis) / 1000UL));
    }

    template <typename T>
    void adjust(const T&) {
        // No-op en modo prueba.
    }
};

MockRTC rtc;
Board *board = nullptr;

// ============================================================
// VARIABLES FOTOPERIODO
// ============================================================

int lightHours = 12;
int darkHours = 12;

int daysVeg = 0;
int daysFlower = 0;

bool isVegetative = true;
bool inLightMode = true;
bool showGraph = false;

time_t cycleStartEpoch = 0;
bool rtcAnchored = false;
bool rtcReady = false;
bool rtcNeedsNtpSync = false;

double photoSecondsElapsed = 0.0;

float history24[24] = {0};

// ============================================================
// DATOS REMOTOS
// ============================================================

SensorData mainData = {};
SoilData c3Data = {};

float remoteTemp = 0.0f;
float remoteHum = 0.0f;
float remoteTDS = 0.0f;
float remoteVPD = 0.0f;

float remoteSoil1 = 0.0f;
float remoteSoil2 = 0.0f;
float remoteCO2 = 400.0f;

bool remoteRelay1 = false;
bool remoteRelay2 = false;
bool remoteHumidifier = false;

unsigned long lastMainReceive = 0;
unsigned long lastC3Receive = 0;

// ============================================================
// CONTROL DE SALIDA
// ============================================================

bool relay1Command = false;
bool relay2Command = false;
bool humidifierCommand = false;

// ============================================================
// SD / GUARDADO
// ============================================================

bool sdReady = false;
bool stateDirty = false;
bool historyDirty = false;

const char *STATE_PATH = "/estado.txt";
const char *HISTORY_PATH = "/history.txt";

unsigned long lastAutoSave = 0;
unsigned long lastStateChangeMs = 0;
unsigned long lastStateSaveAttempt = 0;
unsigned long lastHistorySaveAttempt = 0;
unsigned long lastHistoryUpdate = 0;

const unsigned long AUTOSAVE_INTERVAL_MS = 300000UL;
const unsigned long SAVE_AFTER_TOUCH_RELEASE_MS = 1500UL;
const unsigned long SD_SAVE_RETRY_MS = 10000UL;

// ============================================================
// NTP / RTC
// ============================================================

const unsigned long NTP_SYNC_INTERVAL_MS = 21600000UL;
unsigned long lastNtpSync = 0;
const time_t VALID_EPOCH = 1700000000L;

// ============================================================
// LVGL
// ============================================================

lv_obj_t *screenMain = nullptr;
lv_obj_t *screenGraph = nullptr;

lv_obj_t *labelClock = nullptr;
lv_obj_t *labelCycle = nullptr;

lv_obj_t *labelLight = nullptr;
lv_obj_t *labelDark = nullptr;
lv_obj_t *labelStage = nullptr;

lv_obj_t *labelDaysVeg = nullptr;
lv_obj_t *labelDaysFlower = nullptr;

lv_obj_t *labelPhase = nullptr;
lv_obj_t *labelRemaining = nullptr;
lv_obj_t *labelPercent = nullptr;

lv_obj_t *labelTemp = nullptr;
lv_obj_t *labelHum = nullptr;
lv_obj_t *labelTDS = nullptr;
lv_obj_t *labelCO2 = nullptr;
lv_obj_t *labelVPD = nullptr;

lv_obj_t *labelSoil1 = nullptr;
lv_obj_t *labelSoil2 = nullptr;
lv_obj_t *soilBar1 = nullptr;
lv_obj_t *soilBar2 = nullptr;
static bool progressDragging = false;
// Evita que los eventos PRESSING repetidos sumen más de un día cuando el
// usuario mantiene el dedo al final de la barra durante la fase oscura.
static bool progressCompletedCycleThisTouch = false;

lv_obj_t *connectionDotMain = nullptr;
lv_obj_t *connectionDotC3 = nullptr;

lv_obj_t *progressBar = nullptr;
lv_obj_t *faceCanvas = nullptr;

lv_obj_t *btnLightMinus = nullptr;
lv_obj_t *btnLightPlus = nullptr;
lv_obj_t *btnDarkMinus = nullptr;
lv_obj_t *btnDarkPlus = nullptr;

lv_obj_t *btnVegMinus = nullptr;
lv_obj_t *btnVegPlus = nullptr;
lv_obj_t *btnFlowerMinus = nullptr;
lv_obj_t *btnFlowerPlus = nullptr;

lv_obj_t *btnStage = nullptr;
lv_obj_t *btnSkip = nullptr;
lv_obj_t *btnGraph = nullptr;

lv_obj_t *btnRelay1 = nullptr;
lv_obj_t *btnRelay2 = nullptr;
lv_obj_t *btnHumidifier = nullptr;

// Buffer reutilizable para las caritas.
static lv_color_t faceBuffer[64 * 58];

// ============================================================
// TIEMPO
// ============================================================

static bool getLocalTimeNoBlock(struct tm *ti) {
    if (!ti) return false;

    MockDateTime dt = rtc.now();

    memset(ti, 0, sizeof(struct tm));
    ti->tm_year = dt.year() - 1900;
    ti->tm_mon  = dt.month() - 1;
    ti->tm_mday = dt.day();
    ti->tm_hour = dt.hour();
    ti->tm_min  = dt.minute();
    ti->tm_sec  = dt.second();
    ti->tm_isdst = 0;

    return true;
}

static time_t getRtcEpoch() {
    if (!rtcReady) return 0;
    return (time_t)rtc.now().unixtime();
}

static void initializeRtcFromCompileTime() {
    if (!rtcReady) return;

    // Modo prueba: no hay RTClib ni DS3231 físico.
    MockDateTime now = rtc.now();

    if (now.year() < 2020 || now.year() > 2099) {
        rtcNeedsNtpSync = false;
        Serial.println("[RTC] reloj simulado activo");
    }
}

static bool syncRtcFromNtp(bool force = false) {
    (void)force;
    return false;
}

// ============================================================
// VPD
// ============================================================

static float calculateVPD(float temperatureC, float rh) {
    if (temperatureC <= -40.0f) return 0.0f;

    rh = constrain(rh, 0.0f, 100.0f);

    float svp = 0.6108f *
                expf((17.27f * temperatureC) /
                     (temperatureC + 237.3f));

    float avp = svp * (rh / 100.0f);

    return max(0.0f, svp - avp);
}

static lv_color_t vpdColor(float vpd) {
    if (vpd >= 0.11f && vpd <= 0.39f) return C_RED;
    if (vpd >= 0.40f && vpd <= 0.80f) return C_GREEN;
    if (vpd >= 0.81f && vpd <= 1.20f) return C_BLUE;
    if (vpd >= 1.21f && vpd <= 1.60f) return C_PURPLE;
    if (vpd >= 1.61f && vpd <= 4.81f) return lv_color_hex(0xFF2020);
    return C_WHITE;
}

// ============================================================
// SD
// ============================================================

static void deselectSpiDevices() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
}

static bool initSdCard() {
    deselectSpiDevices();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI);

    Serial.printf("[SD] %s\n", sdReady ? "lista" : "no disponible");
    return sdReady;
}

static String readFile(const char *path) {
    if (!sdReady) return "";

    File file = SD.open(path, FILE_READ);
    if (!file) return "";

    String content;
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    return content;
}

static bool writeTextFile(const char *path, const String &content) {
    if (!sdReady) return false;

    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;

    size_t written = file.print(content);
    file.close();
    return written == content.length();
}

static bool loadState() {
    String content = readFile(STATE_PATH);
    if (content.length() == 0) return false;

    int loadedLight = lightHours;
    int loadedDark = darkHours;
    int loadedVeg = daysVeg;
    int loadedFlower = daysFlower;
    int loadedStage = isVegetative ? 1 : 0;
    int loadedLightMode = inLightMode ? 1 : 0;
    double loadedElapsed = photoSecondsElapsed;
    long long loadedCycleStart = (long long)cycleStartEpoch;

    int parsed = sscanf(
        content.c_str(),
        "version=1\nlightHours=%d\ndarkHours=%d\ndaysVeg=%d\ndaysFlower=%d\nisVegetative=%d\ninLightMode=%d\nphotoSecondsElapsed=%lf\ncycleStartEpoch=%lld",
        &loadedLight,
        &loadedDark,
        &loadedVeg,
        &loadedFlower,
        &loadedStage,
        &loadedLightMode,
        &loadedElapsed,
        &loadedCycleStart
    );

    if (parsed != 8) {
        Serial.println("[SD] estado invalido");
        return false;
    }

    lightHours = constrain(loadedLight, 1, 24);
    darkHours = constrain(loadedDark, 1, 24);
    daysVeg = max(0, loadedVeg);
    daysFlower = max(0, loadedFlower);
    isVegetative = loadedStage != 0;

    double totalSecs = (lightHours + darkHours) * 3600.0;
    photoSecondsElapsed = constrain(loadedElapsed, 0.0, totalSecs - 0.001);
    inLightMode = photoSecondsElapsed < lightHours * 3600.0;
    cycleStartEpoch = (time_t)loadedCycleStart;
    rtcAnchored = false;

    Serial.println("[SD] estado recuperado");
    return true;
}

static bool saveState() {
    if (!sdReady) return false;

    String content;
    content.reserve(256);
    content += "version=1\n";
    content += "lightHours=" + String(lightHours) + "\n";
    content += "darkHours=" + String(darkHours) + "\n";
    content += "daysVeg=" + String(daysVeg) + "\n";
    content += "daysFlower=" + String(daysFlower) + "\n";
    content += "isVegetative=" + String(isVegetative ? 1 : 0) + "\n";
    content += "inLightMode=" + String(inLightMode ? 1 : 0) + "\n";
    content += "photoSecondsElapsed=" + String(photoSecondsElapsed, 3) + "\n";
    char epochBuffer[32];
    snprintf(epochBuffer, sizeof(epochBuffer), "%lld", (long long)cycleStartEpoch);
    content += "cycleStartEpoch=" + String(epochBuffer) + "\n";

    const char *tempPath = "/estado.tmp";
    SD.remove(tempPath);
    if (!writeTextFile(tempPath, content)) return false;

    SD.remove(STATE_PATH);
    if (!SD.rename(tempPath, STATE_PATH)) {
        SD.remove(tempPath);
        return false;
    }

    stateDirty = false;
    lastAutoSave = millis();
    return true;
}

// ============================================================
// ESP-NOW
// ============================================================

static void addPeer(uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
        return;
    }

    esp_now_peer_info_t peer = {};

    memcpy(
        peer.peer_addr,
        mac,
        6
    );

    peer.channel = 0;
    peer.encrypt = false;

    esp_err_t result =
        esp_now_add_peer(&peer);

    if (result == ESP_OK) {
        Serial.println("[ESP-NOW] peer agregado");
    } else {
        Serial.printf(
            "[ESP-NOW] error peer: %d\n",
            result
        );
    }
}

void OnDataSent(
    const uint8_t *mac_addr,
    esp_now_send_status_t status
) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESP-NOW] TX OK");
    } else {
        Serial.println("[ESP-NOW] TX ERROR");
    }
}

void OnDataRecv(
    const esp_now_recv_info_t *info,
    const uint8_t *data,
    int len
) {
    if (!info || !data) return;

    if (memcmp(info->src_addr, MAC_PRINCIPAL, 6) == 0) {

        if (len == sizeof(SensorData)) {

            memcpy(
                &mainData,
                data,
                sizeof(mainData)
            );

            remoteTemp = mainData.temperature;
            remoteHum = mainData.humidity;
            remoteTDS = mainData.tds;

            remoteVPD = calculateVPD(
                remoteTemp,
                remoteHum
            );

            remoteRelay1 = mainData.relay1;
            remoteRelay2 = mainData.relay2;
            remoteHumidifier = mainData.humidifier;

            lastMainReceive = millis();

            return;
        }
    }

    if (memcmp(info->src_addr, MAC_C3, 6) == 0) {

        if (len == sizeof(SoilData)) {

            memcpy(
                &c3Data,
                data,
                sizeof(c3Data)
            );

            remoteSoil1 =
                constrain(
                    c3Data.soil1,
                    0.0f,
                    100.0f
                );

            remoteSoil2 =
                constrain(
                    c3Data.soil2,
                    0.0f,
                    100.0f
                );

            remoteCO2 =
                max(
                    0.0f,
                    c3Data.co2
                );

            lastC3Receive = millis();

            return;
        }

        // Compatibilidad con el formato viejo del C3.
        if (len == sizeof(LegacySoilData)) {

            LegacySoilData oldData;

            memcpy(
                &oldData,
                data,
                sizeof(oldData)
            );

            remoteSoil1 =
                constrain(
                    oldData.soil1,
                    0.0f,
                    100.0f
                );

            remoteSoil2 =
                constrain(
                    oldData.soil2,
                    0.0f,
                    100.0f
                );

            remoteCO2 =
                max(
                    0.0f,
                    oldData.co2
                );

            lastC3Receive = millis();

            return;
        }
    }
}

static void sendControl() {
    ControlData command;

    command.relay1 = relay1Command;
    command.relay2 = relay2Command;
    command.humidifier = humidifierCommand;

    esp_err_t result =
        esp_now_send(
            MAC_PRINCIPAL,
            (uint8_t *)&command,
            sizeof(command)
        );

    Serial.printf(
        "[CONTROL] R1=%d R2=%d HUM=%d result=%d\n",
        command.relay1,
        command.relay2,
        command.humidifier,
        result
    );
}

// ============================================================
// FOTOPERIODO
// ============================================================

static void markChanged();

// Los contadores son independientes de la UI: sólo se modifican cuando
// termina uno o más ciclos completos de LUZ + OSCURIDAD.
static void addCompletedCycles(unsigned long completedCycles) {
    if (completedCycles == 0) return;

    if (isVegetative) {
        daysVeg += completedCycles;
    } else {
        daysFlower += completedCycles;
    }

    markChanged();
}

static double consumeCompletedCycles(double elapsed, double totalSecs) {
    if (elapsed < totalSecs) return elapsed;

    unsigned long completedCycles =
        (unsigned long)(elapsed / totalSecs);

    addCompletedCycles(completedCycles);
    return elapsed - completedCycles * totalSecs;
}

static void updatePhotoperiod() {
    static unsigned long lastMillis = 0;

    unsigned long nowMs = millis();
    if (lastMillis == 0) {
        lastMillis = nowMs;
        return;
    }

    double delta = (nowMs - lastMillis) / 1000.0;
    lastMillis = nowMs;

    double totalSecs = (lightHours + darkHours) * 3600.0;
    double lightSecs = lightHours * 3600.0;
    if (totalSecs <= 0.0) return;

    // El fotoperiodo usa solamente millis(): la cuenta no depende del RTC.
    // Conserva el resto del ciclo si el bucle se retrasó y acredita todos los
    // ciclos completos, incluso si se sobrepasa más de uno entre iteraciones.
    photoSecondsElapsed += delta;
    photoSecondsElapsed =
        consumeCompletedCycles(photoSecondsElapsed, totalSecs);

    bool newLight = photoSecondsElapsed < lightSecs;
    if (newLight != inLightMode) {
        inLightMode = newLight;
        relay1Command = inLightMode;
        sendControl();
    }

    relay1Command = inLightMode;

    if (nowMs - lastHistoryUpdate > 3600000UL) {
        for (int i = 0; i < 23; i++) {
            history24[i] = history24[i + 1];
        }
        history24[23] = (photoSecondsElapsed / totalSecs) * 100.0f;
        lastHistoryUpdate = nowMs;
        historyDirty = true;
    }
}

// ============================================================
// WEEDAGOTCHI / CARITAS
// ============================================================

static int getMoodBucket() {
    float mood =
        (remoteSoil1 + remoteSoil2) * 0.5f;

    if (mood >= 60.0f) return 0;
    if (mood >= 40.0f) return 1;
    if (mood >= 10.0f) return 2;

    return 3;
}

static void renderFaceBitmap(
    const uint8_t *bmp,
    const uint8_t *outline
) {
    if (!faceCanvas) return;

    lv_canvas_fill_bg(
        faceCanvas,
        C_BG,
        LV_OPA_COVER
    );

    // 64x58 para permitir la sombra +1,+1.
    for (int y = 0; y < SPRITE_H; y++) {

        for (int x = 0; x < SPRITE_W; x++) {

            int index =
                y * 8 +
                (x >> 3);

            uint8_t mask =
                0x80 >>
                (x & 7);

            bool outlinePixel =
                pgm_read_byte(
                    outline + index
                ) & mask;

            bool facePixel =
                pgm_read_byte(
                    bmp + index
                ) & mask;

            if (outlinePixel &&
                x + 1 < 64 &&
                y + 1 < 58) {

                lv_canvas_set_px_color(
                    faceCanvas,
                    x + 1,
                    y + 1,
                    C_DARKFACE
                );
            }

            if (facePixel) {

                lv_canvas_set_px_color(
                    faceCanvas,
                    x,
                    y,
                    C_GREEN
                );
            }
        }
    }

    lv_obj_invalidate(faceCanvas);
}

static void updateFace() {
    static int lastMood = -1;

    int mood = getMoodBucket();

    if (mood == lastMood) {
        return;
    }

    const uint8_t *bmp = nullptr;
    const uint8_t *outline = nullptr;

    switch (mood) {

        case 0:
            bmp = happyFace_bmp;
            outline = happyFace_outline_bmp;
            break;

        case 1:
            bmp = angryFace_bmp;
            outline = angryFace_outline_bmp;
            break;

        case 2:
            bmp = sadFace_bmp;
            outline = sadFace_outline_bmp;
            break;

        default:
            bmp = deadFace_bmp;
            outline = deadFace_outline_bmp;
            break;
    }

    renderFaceBitmap(
        bmp,
        outline
    );

    lastMood = mood;
}

// ============================================================
// UI HELPERS
// ============================================================

static lv_obj_t *makeLabel(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color
) {
    lv_obj_t *label =
        lv_label_create(parent);

    lv_label_set_text(
        label,
        text
    );

    lv_obj_set_style_text_font(
        label,
        font,
        0
    );

    lv_obj_set_style_text_color(
        label,
        color,
        0
    );

    return label;
}

static lv_obj_t *makeLabelAt(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color,
    int x,
    int y
) {
    lv_obj_t *label = makeLabel(parent, text, font, color);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *makeButton(
    lv_obj_t *parent,
    const char *text,
    int x,
    int y,
    int w,
    int h
) {
    lv_obj_t *btn =
        lv_btn_create(parent);

    lv_obj_set_pos(
        btn,
        x,
        y
    );

    lv_obj_set_size(
        btn,
        w,
        h
    );

    lv_obj_set_style_radius(
        btn,
        4,
        0
    );

    lv_obj_set_style_bg_color(
        btn,
        C_GRAY2,
        0
    );

    lv_obj_set_style_border_width(
        btn,
        1,
        0
    );

    lv_obj_set_style_border_color(
        btn,
        C_GRAY,
        0
    );

    lv_obj_t *label =
        lv_label_create(btn);

    lv_label_set_text(
        label,
        text
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_12,
        0
    );

    lv_obj_set_style_text_color(
        label,
        C_WHITE,
        0
    );

    lv_obj_center(label);

    return btn;
}

static void setButtonStateColor(
    lv_obj_t *btn,
    bool on
) {
    if (!btn) return;

    lv_obj_set_style_bg_color(
        btn,
        on ? C_GREEN : C_GRAY2,
        0
    );

    lv_obj_set_style_text_color(
        btn,
        on ? C_BG : C_WHITE,
        0
    );
}

// ============================================================
// EVENTOS
// ============================================================

static void markChanged() {
    stateDirty = true;
    lastStateChangeMs = millis();
}

static void preserveCycleProgressAfterScheduleChange() {
    double totalSecs = (lightHours + darkHours) * 3600.0;
    photoSecondsElapsed = constrain(photoSecondsElapsed, 0.0, totalSecs - 0.001);

    if (rtcAnchored) {
        cycleStartEpoch = getRtcEpoch() - (time_t)photoSecondsElapsed;
    }
}

static void onLightMinus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lightHours =
        max(
            1,
            lightHours - 1
        );

    preserveCycleProgressAfterScheduleChange();
    markChanged();
}

static void onLightPlus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lightHours =
        min(
            24,
            lightHours + 1
        );

    preserveCycleProgressAfterScheduleChange();
    markChanged();
}

static void onDarkMinus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    darkHours =
        max(
            1,
            darkHours - 1
        );

    preserveCycleProgressAfterScheduleChange();
    markChanged();
}

static void onDarkPlus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    darkHours =
        min(
            24,
            darkHours + 1
        );

    preserveCycleProgressAfterScheduleChange();
    markChanged();
}

static void onVegMinus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    daysVeg =
        max(
            0,
            daysVeg - 1
        );

    markChanged();
}

static void onVegPlus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    daysVeg++;
    markChanged();
}

static void onFlowerMinus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    daysFlower =
        max(
            0,
            daysFlower - 1
        );

    markChanged();
}

static void onFlowerPlus(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    daysFlower++;
    markChanged();
}

static void onStage(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    // Una etapa nueva comienza un ciclo nuevo; los contadores acumulados
    // permanecen intactos y el ciclo incompleto anterior no se atribuye a
    // la etapa recién seleccionada.
    isVegetative = !isVegetative;
    photoSecondsElapsed = 0.0;
    inLightMode = true;

    if (rtcAnchored) {
        cycleStartEpoch = getRtcEpoch();
    }

    relay1Command = true;
    sendControl();

    // Actualizarlo en el mismo toque evita depender del siguiente refresco de
    // la UI y confirma de inmediato que el botón cambió de etapa.
    if (labelStage) {
        lv_label_set_text(labelStage, isVegetative ? "VEGETACION" : "FLORACION");
        lv_obj_set_style_text_color(labelStage, C_BG, 0);
    }
    lv_obj_set_style_bg_color(btnStage, isVegetative ? C_GREEN : C_AMBER, 0);
    lv_obj_set_style_text_color(btnStage, C_BG, 0);

    updateFace();
    markChanged();
}

static void onSkipPhase(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (inLightMode) {
        photoSecondsElapsed =
            lightHours * 3600.0 + 1.0;
    } else {
        photoSecondsElapsed = 0.0;
    }

    if (rtcAnchored) {
        cycleStartEpoch =
            getRtcEpoch() -
            (time_t)photoSecondsElapsed;
    }

    inLightMode =
        photoSecondsElapsed <
        lightHours * 3600.0;

    relay1Command = inLightMode;

    sendControl();

    markChanged();
}

static void onRelay1(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    relay1Command = !relay1Command;

    sendControl();
}

static void onRelay2(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    relay2Command = !relay2Command;

    sendControl();
}

static void onHumidifier(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    humidifierCommand =
        !humidifierCommand;

    sendControl();
}

static void onGraph(
    lv_event_t *e
);

static void onBackGraph(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    showGraph = false;

    lv_scr_load(screenMain);
}

static void updateProgressFromTouch() {
    if (!progressBar) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t area;
    lv_obj_get_coords(progressBar, &area);

    int width = lv_area_get_width(&area);
    if (width <= 0) return;

    float ratio = (float)(point.x - area.x1) / (float)width;
    ratio = constrain(ratio, 0.0f, 1.0f);

    double lightSecs = lightHours * 3600.0;
    double darkSecs  = darkHours * 3600.0;
    // La barra representa la fase actual. Al completar OSCURIDAD, sí termina
    // el ciclo LUZ + OSCURIDAD: se acredita exactamente un día a la etapa
    // activa y la barra vuelve al comienzo de LUZ.
    if (!inLightMode && ratio >= 1.0f) {
        if (!progressCompletedCycleThisTouch) {
            addCompletedCycles(1);
            progressCompletedCycleThisTouch = true;
            photoSecondsElapsed = 0.0;
            inLightMode = true;
            relay1Command = true;
            sendControl();
        }
        return;
    }

    if (inLightMode) {
        photoSecondsElapsed = ratio * lightSecs;
    } else {
        photoSecondsElapsed = lightSecs + ratio * darkSecs;
    }

    double totalSecs = lightSecs + darkSecs;
    if (totalSecs > 0.0) {
        photoSecondsElapsed = constrain(photoSecondsElapsed, 0.0, totalSecs - 0.001);
    }

    inLightMode = photoSecondsElapsed < lightSecs;
    relay1Command = inLightMode;

    markChanged();
}

static void onProgressTouch(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        if (code == LV_EVENT_PRESSED) {
            progressCompletedCycleThisTouch = false;
        }
        if (progressCompletedCycleThisTouch) return;
        progressDragging = true;
        updateProgressFromTouch();
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        progressDragging = false;
        progressCompletedCycleThisTouch = false;
        return;
    }
}

// ============================================================
// CREAR PANTALLA PRINCIPAL
// ============================================================

static void createMainUI() {
    screenMain = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screenMain, C_BG, 0);
    lv_obj_set_style_bg_opa(screenMain, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screenMain, LV_OBJ_FLAG_SCROLLABLE);

    // Reloj y ciclo
    labelClock = makeLabel(screenMain, "--:-- --", &lv_font_montserrat_24, C_WHITE);
    lv_obj_align(labelClock, LV_ALIGN_TOP_MID, -35, 2);

    labelCycle = makeLabel(screenMain, "Ciclo 24h", &lv_font_montserrat_12, C_AMBER);
    lv_obj_align(labelCycle, LV_ALIGN_TOP_MID, -35, 28);

    // --------------------------------------------------------
    // AJUSTES DE LUZ / OSCURIDAD
    // --------------------------------------------------------
    // El texto LUZ queda junto a sus flechas, no como titulo arriba.
    makeLabelAt(screenMain, "LUZ", &lv_font_montserrat_12, C_WHITE, 5, 53);

    labelLight = makeLabel(screenMain, "12h", &lv_font_montserrat_12, C_AMBER);
    lv_obj_set_pos(labelLight, 34, 53);

    btnLightMinus = makeButton(screenMain, "<", 60, 49, 20, 20);
    btnLightPlus  = makeButton(screenMain, ">", 83, 49, 20, 20);

    lv_obj_add_event_cb(btnLightMinus, onLightMinus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnLightPlus, onLightPlus, LV_EVENT_CLICKED, nullptr);

    makeLabelAt(screenMain, "OSC", &lv_font_montserrat_12, C_WHITE, 125, 53);

    labelDark = makeLabel(screenMain, "12h", &lv_font_montserrat_12, C_AMBER);
    lv_obj_set_pos(labelDark, 154, 53);

    btnDarkMinus = makeButton(screenMain, "<", 180, 49, 20, 20);
    btnDarkPlus  = makeButton(screenMain, ">", 203, 49, 20, 20);

    lv_obj_add_event_cb(btnDarkMinus, onDarkMinus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnDarkPlus, onDarkPlus, LV_EVENT_CLICKED, nullptr);

    // --------------------------------------------------------
    // MODO
    // --------------------------------------------------------
    makeLabelAt(screenMain, "MODO:", &lv_font_montserrat_12, C_WHITE, 5, 77);

    btnStage = makeButton(screenMain, "VEGETACION", 50, 74, 110, 22);
    labelStage = lv_obj_get_child(btnStage, 0);
    lv_obj_set_style_bg_color(btnStage, C_GREEN, 0);
    lv_obj_set_style_text_color(btnStage, C_BG, 0);
    lv_obj_set_style_text_color(labelStage, C_BG, 0);
    lv_obj_add_event_cb(btnStage, onStage, LV_EVENT_CLICKED, nullptr);

    // --------------------------------------------------------
    // DIAS
    // --------------------------------------------------------
    // Formato: DIAS VEG:  <  #  >
    // Las flechas quedan a ambos lados del contador.
    makeLabelAt(screenMain, "DIAS VEG:", &lv_font_montserrat_12, C_WHITE, 5, 102);

    btnVegMinus = makeButton(screenMain, "<", 72, 98, 20, 20);

    labelDaysVeg = makeLabel(screenMain, "0", &lv_font_montserrat_18, C_GREEN);
    lv_obj_set_pos(labelDaysVeg, 98, 98);

    btnVegPlus = makeButton(screenMain, ">", 122, 98, 20, 20);

    lv_obj_add_event_cb(btnVegMinus, onVegMinus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnVegPlus, onVegPlus, LV_EVENT_CLICKED, nullptr);

    // Cinco píxeles adicionales de separación respecto a DÍAS VEG.
    makeLabelAt(screenMain, "DIAS FLOR:", &lv_font_montserrat_12, C_WHITE, 5, 133);

    btnFlowerMinus = makeButton(screenMain, "<", 82, 129, 20, 20);

    labelDaysFlower = makeLabel(screenMain, "0", &lv_font_montserrat_18, C_AMBER);
    lv_obj_set_pos(labelDaysFlower, 108, 129);

    btnFlowerPlus = makeButton(screenMain, ">", 132, 129, 20, 20);

    lv_obj_add_event_cb(btnFlowerMinus, onFlowerMinus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnFlowerPlus, onFlowerPlus, LV_EVENT_CLICKED, nullptr);

    // --------------------------------------------------------
    // BARRA DE TIEMPO TOUCH
    // --------------------------------------------------------
    progressBar = lv_bar_create(screenMain);
    lv_obj_set_pos(progressBar, 5, 159);
    lv_obj_set_size(progressBar, 225, 13);
    lv_obj_clear_flag(progressBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(progressBar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_bg_color(progressBar, C_GRAY2, LV_PART_MAIN);
    lv_obj_set_style_border_color(progressBar, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(progressBar, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progressBar, C_GREEN, LV_PART_INDICATOR);

    lv_obj_add_event_cb(progressBar, onProgressTouch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(progressBar, onProgressTouch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(progressBar, onProgressTouch, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(progressBar, onProgressTouch, LV_EVENT_PRESS_LOST, nullptr);

    labelPhase = makeLabel(screenMain, "FASE: LUZ", &lv_font_montserrat_12, C_GREEN);
    lv_obj_set_pos(labelPhase, 5, 179);

    labelRemaining = makeLabel(screenMain, "00:00:00", &lv_font_montserrat_12, C_WHITE);
    lv_obj_set_pos(labelRemaining, 70, 179);

    labelPercent = makeLabel(screenMain, "0%", &lv_font_montserrat_12, C_AMBER);
    lv_obj_set_pos(labelPercent, 165, 179);

    // --------------------------------------------------------
    // S1 / S2: COLUMNAS VERTICALES INFERIORES
    // --------------------------------------------------------
    // Se llenan de abajo hacia arriba según el porcentaje.
    // El porcentaje queda arriba y S1/S2 abajo.
    //
    // S1: columna en x=60..80
    // S2: columna en x=80..100
    // El porcentaje queda centrado arriba de cada columna.
    // S1/S2 quedan centrados abajo de cada columna.

    labelSoil1 = makeLabel(screenMain, "0%", &lv_font_montserrat_10, C_GREEN);
    lv_obj_set_pos(labelSoil1, 40, 195);
    lv_obj_set_width(labelSoil1, 20);
    lv_label_set_long_mode(labelSoil1, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(labelSoil1, LV_TEXT_ALIGN_CENTER, 0);

    soilBar1 = lv_bar_create(screenMain);
    lv_obj_set_pos(soilBar1, 40, 209);
    lv_obj_set_size(soilBar1, 20, 82);
    lv_bar_set_range(soilBar1, 0, 100);
    lv_bar_set_value(soilBar1, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(soilBar1, C_GRAY2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(soilBar1, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(soilBar1, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(soilBar1, 1, LV_PART_MAIN);

    labelSoil2 = makeLabel(screenMain, "0%", &lv_font_montserrat_10, C_BLUE);
    lv_obj_set_pos(labelSoil2, 80, 195);
    lv_obj_set_width(labelSoil2, 20);
    lv_label_set_long_mode(labelSoil2, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(labelSoil2, LV_TEXT_ALIGN_CENTER, 0);

    soilBar2 = lv_bar_create(screenMain);
    lv_obj_set_pos(soilBar2, 80, 209);
    lv_obj_set_size(soilBar2, 20, 82);
    lv_bar_set_range(soilBar2, 0, 100);
    lv_bar_set_value(soilBar2, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(soilBar2, C_GRAY2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(soilBar2, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(soilBar2, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(soilBar2, 1, LV_PART_MAIN);

    makeLabelAt(screenMain, "S1", &lv_font_montserrat_10, C_GREEN, 40, 294);
    makeLabelAt(screenMain, "S2", &lv_font_montserrat_10, C_BLUE, 80, 294);

    // --------------------------------------------------------
    // CARITA: al fondo, centrada y sin redibujado periódico.
    // --------------------------------------------------------
    faceCanvas = lv_canvas_create(screenMain);
    lv_canvas_set_buffer(faceCanvas, faceBuffer, 64, 58, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(faceCanvas, 128, 220);

    updateFace();
}

// ============================================================
// GRAFICO 24 H
// ============================================================

static void createGraphUI() {
    screenGraph =
        lv_obj_create(nullptr);

    lv_obj_set_style_bg_color(
        screenGraph,
        C_BG,
        0
    );

    lv_obj_t *title =
        makeLabel(
            screenGraph,
            "[ HISTORIAL FOTOPERIODO 24H ]",
            &lv_font_montserrat_14,
            C_GREEN
        );

    lv_obj_align(
        title,
        LV_ALIGN_TOP_MID,
        0,
        3
    );

    lv_obj_t *chart =
        lv_chart_create(screenGraph);

    lv_obj_set_pos(
        chart,
        8,
        30
    );

    lv_obj_set_size(
        chart,
        304,
        160
    );

    lv_chart_set_type(
        chart,
        LV_CHART_TYPE_LINE
    );

    lv_chart_set_range(
        chart,
        LV_CHART_AXIS_PRIMARY_Y,
        0,
        100
    );

    lv_chart_set_point_count(
        chart,
        24
    );

    lv_obj_set_style_bg_color(
        chart,
        C_BG,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        chart,
        C_GRAY,
        LV_PART_MAIN
    );

    lv_obj_set_style_line_color(
        chart,
        C_GRAY,
        LV_PART_MAIN
    );

    lv_chart_series_t *series =
        lv_chart_add_series(
            chart,
            C_GREEN,
            LV_CHART_AXIS_PRIMARY_Y
        );

    for (int i = 0; i < 24; i++) {
        lv_chart_set_value_by_id(
            chart,
            series,
            i,
            (int)constrain(
                history24[i],
                0.0f,
                100.0f
            )
        );
    }

    lv_chart_refresh(chart);

    lv_obj_t *back =
        makeButton(
            screenGraph,
            "<<< TOCAR PARA VOLVER >>>",
            75,
            202,
            170,
            30
        );

    lv_obj_add_event_cb(
        back,
        onBackGraph,
        LV_EVENT_CLICKED,
        nullptr
    );
}

static void onGraph(
    lv_event_t *e
) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    showGraph = true;

    createGraphUI();

    lv_scr_load(screenGraph);
}

// ============================================================
// ACTUALIZAR UI
// ============================================================

static void updateClockUI() {
    if (!labelClock) return;

    struct tm ti;

    char clockBuf[16];

    if (getLocalTimeNoBlock(&ti)) {

        strftime(
            clockBuf,
            sizeof(clockBuf),
            "%I:%M %p",
            &ti
        );

    } else {

        strcpy(
            clockBuf,
            "--:-- --"
        );
    }

    lv_label_set_text(
        labelClock,
        clockBuf
    );
}

static void updateMainUI() {
    if (!screenMain) return;

    char buf[64];

    static int lastLightHours = -1;
    static int lastDarkHours = -1;
    static int lastVeg = -1;
    static int lastFlower = -1;
    static bool lastStage = false;
    static bool lastPhase = false;
    static int lastPercent = -1;
    static int lastSoil1 = -1;
    static int lastSoil2 = -1;
    static bool lastRelay1 = false;
    static bool lastMainConnected = false;
    static bool lastC3Connected = false;

    if (lastLightHours != lightHours) {
        snprintf(buf, sizeof(buf), "%dh", lightHours);
        lv_label_set_text(labelLight, buf);
        lastLightHours = lightHours;
    }

    if (lastDarkHours != darkHours) {
        snprintf(buf, sizeof(buf), "%dh", darkHours);
        lv_label_set_text(labelDark, buf);
        lastDarkHours = darkHours;
    }

    if (lastVeg != daysVeg) {
        snprintf(buf, sizeof(buf), "%d", daysVeg);
        lv_label_set_text(labelDaysVeg, buf);
        lastVeg = daysVeg;
    }

    if (lastFlower != daysFlower) {
        snprintf(buf, sizeof(buf), "%d", daysFlower);
        lv_label_set_text(labelDaysFlower, buf);
        lastFlower = daysFlower;
    }

    if (lastStage != isVegetative) {
        if (labelStage) {
            lv_label_set_text(labelStage, isVegetative ? "VEGETACION" : "FLORACION");
            lv_obj_set_style_text_color(labelStage, C_BG, 0);
        }
        lv_obj_set_style_bg_color(btnStage, isVegetative ? C_GREEN : C_AMBER, 0);
        lv_obj_set_style_text_color(btnStage, C_BG, 0);
        lastStage = isVegetative;
    }

    double lightSecs = lightHours * 3600.0;
    double darkSecs = darkHours * 3600.0;
    double phaseDur = inLightMode ? lightSecs : darkSecs;
    double phaseElapsed = inLightMode ? photoSecondsElapsed : photoSecondsElapsed - lightSecs;

    if (phaseDur < 1.0) phaseDur = 1.0;
    phaseElapsed = constrain(phaseElapsed, 0.0, phaseDur);

    int phasePercent = (int)((phaseElapsed / phaseDur) * 100.0);
    phasePercent = constrain(phasePercent, 0, 100);

    if (lastPercent != phasePercent || lastPhase != inLightMode) {
        lv_bar_set_value(progressBar, phasePercent, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progressBar, isVegetative ? C_GREEN : C_AMBER, LV_PART_INDICATOR);

        snprintf(buf, sizeof(buf), "FASE: %s", inLightMode ? "LUZ" : "OSC");
        lv_label_set_text(labelPhase, buf);

        lastPercent = phasePercent;
        lastPhase = inLightMode;
    }

    // Tiempo restante: se actualiza una vez por segundo como máximo.
    static unsigned long lastTimeLabel = 0;
    if (millis() - lastTimeLabel >= 1000UL || lastTimeLabel == 0) {
        double remaining = phaseDur - phaseElapsed;
        if (remaining < 0) remaining = 0;

        int rh = (int)(remaining / 3600);
        int rm = (int)(((long)remaining % 3600) / 60);
        int rs = (int)((long)remaining % 60);

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", rh, rm, rs);
        lv_label_set_text(labelRemaining, buf);

        snprintf(buf, sizeof(buf), "%d%%", phasePercent);
        lv_label_set_text(labelPercent, buf);

        updateClockUI();
        lastTimeLabel = millis();
    }

    int soil1 = constrain((int)roundf(remoteSoil1), 0, 100);
    int soil2 = constrain((int)roundf(remoteSoil2), 0, 100);

    if (lastSoil1 != soil1) {
        lv_bar_set_value(soilBar1, soil1, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", soil1);
        lv_label_set_text(labelSoil1, buf);
        lastSoil1 = soil1;
    }

    if (lastSoil2 != soil2) {
        lv_bar_set_value(soilBar2, soil2, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", soil2);
        lv_label_set_text(labelSoil2, buf);
        lastSoil2 = soil2;
    }

    bool mainConnected = lastMainReceive > 0 && millis() - lastMainReceive < 10000UL;
    bool c3Connected = lastC3Receive > 0 && millis() - lastC3Receive < 15000UL;

    if (connectionDotMain && mainConnected != lastMainConnected) {
        lv_obj_set_style_bg_color(connectionDotMain, mainConnected ? C_GREEN : C_RED, 0);
        lastMainConnected = mainConnected;
    }

    if (connectionDotC3 && c3Connected != lastC3Connected) {
        lv_obj_set_style_bg_color(connectionDotC3, c3Connected ? C_GREEN : C_RED, 0);
        lastC3Connected = c3Connected;
    }

    if (btnRelay1 && relay1Command != lastRelay1) {
        setButtonStateColor(btnRelay1, relay1Command);
        lastRelay1 = relay1Command;
    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("==============================");
    Serial.println("FOTOPERIODO - ESP32-S3");
    Serial.println("MODO PRUEBA DE INTERFAZ");
    Serial.println("==============================");

    // --------------------------------------------------------
    // Reloj simulado
    // --------------------------------------------------------
    rtc.begin();
    rtcReady = true;
    rtcAnchored = false;
    rtcNeedsNtpSync = false;

    // --------------------------------------------------------
    // SD: recupera contadores y ancla de ciclo antes de crear la UI.
    // --------------------------------------------------------
    if (initSdCard()) {
        loadState();
    }

    // --------------------------------------------------------
    // WiFi/ESP-NOW:
    // No se inicializa todavía. La interfaz funciona de forma
    // independiente para que podamos probar touch y navegación.
    // --------------------------------------------------------
    Serial.println("[ESP-NOW] omitido en modo prueba");

    // --------------------------------------------------------
    // Pantalla
    // --------------------------------------------------------
    Serial.println("[DISPLAY] inicializando...");

    board = new Board();
    board->init();

    if (!board->begin()) {
        Serial.println("[DISPLAY] ERROR");
        while (true) {
            delay(1000);
        }
    }

    lvgl_port_init(
        board->getLCD(),
        board->getTouch()
    );

    // --------------------------------------------------------
    // UI original del controlador de fotoperiodo
    // --------------------------------------------------------
    lvgl_port_lock(-1);

    createMainUI();

    lv_scr_load(screenMain);

    lvgl_port_unlock();

    // Los valores por defecto ya están en las variables globales. Si la SD
    // tenía un estado válido, loadState() los conservó.
    relay1Command = inLightMode;
    relay2Command = false;
    humidifierCommand = false;

    lastAutoSave = millis();

    // Estado visual inicial de los controles.
    setButtonStateColor(btnRelay1, relay1Command);

    Serial.println("[SISTEMA] INTERFAZ LISTA");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
    // Mientras se arrastra la barra, el tiempo no avanza para que el toque
    // no sea sobrescrito por el fotoperiodo ni provoque un salto.
    if (!progressDragging) {
        updatePhotoperiod();
    }

    static unsigned long lastUI = 0;
    if (millis() - lastUI >= 50UL) {
        lvgl_port_lock(-1);

        if (!showGraph) {
            updateMainUI();
            // updateFace() solamente cambia el bitmap si cambia el estado.
            updateFace();
        }

        lvgl_port_unlock();
        lastUI = millis();
    }

    if (stateDirty &&
        millis() - lastStateChangeMs >= SAVE_AFTER_TOUCH_RELEASE_MS) {
        if (!saveState()) {
            // Se reintentará en el siguiente ciclo; no se pierde el estado sucio.
            lastStateChangeMs = millis() - SAVE_AFTER_TOUCH_RELEASE_MS + SD_SAVE_RETRY_MS;
        }
    }

    delay(2);
}
