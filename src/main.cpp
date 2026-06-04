/*
 * TEST SIMPLE — Un seul capteur SRF08 sur 0xE0
 * Affiche la distance sur l'écran, se rafraîchit toutes les 200ms
 */

#include "lvgl.h"
#include <stdio.h>

#ifdef ARDUINO
#include "lvglDrivers.h"
#include <Wire.h>

/* ═══════════════════════════════════════════════════════════════════════
   DRIVER SRF08 via Wire
   ═════════════════════════════════════════════════════════════════════ */
typedef struct {
    uint16_t distance_cm;
    uint8_t  light;
    uint32_t response_ms;
    uint8_t  valid;
} SRF08_Result_t;

#define SRF08_ADDR  0x70  /* 0xE0 >> 1 = 0x70 en 7 bits pour Wire */

static void SRF08_Measure(SRF08_Result_t *result)
{
    uint32_t t_start = millis();

    /* Lancer la mesure en cm */
    Wire.beginTransmission(SRF08_ADDR);
    Wire.write(0x00);
    Wire.write(0x51);
    if (Wire.endTransmission() != 0) {
        result->valid = 0; return;
    }
    delay(70);
    /* Polling registre 0 — 0xFF tant que mesure en cours */
    uint8_t status = 0xFF;
    while (status == 0xFF) {
        delay(5);
        Wire.beginTransmission(SRF08_ADDR);
        Wire.write(0x00);
        Wire.endTransmission(false);
        Wire.requestFrom(SRF08_ADDR, (uint8_t)1);
        if (Wire.available()) status = Wire.read();
        if (millis() - t_start > 300) {
            result->valid = 0; return;
        }
    }

    /* Lire lumière + distance haute + distance basse */
    Wire.beginTransmission(SRF08_ADDR);
    Wire.write(0x01);
    Wire.endTransmission(false);
    Wire.requestFrom(SRF08_ADDR, (uint8_t)3);

    if (Wire.available() < 3) {
        result->valid = 0; return;
    }

    result->light       = Wire.read();
    uint8_t high        = Wire.read();
    uint8_t low         = Wire.read();
    result->distance_cm = ((uint16_t)high << 8) | low;
    result->response_ms = millis() - t_start;
    result->valid       = 1;
}

/* ═══════════════════════════════════════════════════════════════════════
   MOYENNE GLISSANTE SUR 3 MESURES
   ═════════════════════════════════════════════════════════════════════ */
#define AVG_SIZE 3
static uint16_t avg_buf[AVG_SIZE] = {0};
static uint8_t  avg_idx   = 0;
static uint8_t  avg_count = 0;

static uint16_t push_average(uint16_t new_val)
{
    avg_buf[avg_idx] = new_val;
    avg_idx = (avg_idx + 1) % AVG_SIZE;
    if (avg_count < AVG_SIZE) avg_count++;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < avg_count; i++) sum += avg_buf[i];
    return (uint16_t)(sum / avg_count);
}

/* ═══════════════════════════════════════════════════════════════════════
   WIDGETS LVGL
   ═════════════════════════════════════════════════════════════════════ */
static lv_obj_t *lbl_distance;
static lv_obj_t *lbl_distance_raw;
static lv_obj_t *lbl_light;
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_counter;
static lv_obj_t *lbl_response;
static lv_obj_t *bar;
static uint32_t  measure_count = 0;

static lv_color_t dist_color(uint16_t cm)
{
    if (cm < 20) return lv_color_hex(0xFF4444);
    if (cm < 50) return lv_color_hex(0xFFD700);
    return lv_color_hex(0x3FB950);
}

static void build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SRF08 — TEMPS REEL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00D4FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *addr = lv_label_create(scr);
    lv_label_set_text(addr, "0xE0  |  Wire I2C  |  PB8=SCL  PB9=SDA");
    lv_obj_set_style_text_color(addr, lv_color_hex(0x8B949E), 0);
    lv_obj_align(addr, LV_ALIGN_TOP_MID, 0, 32);

    lbl_distance = lv_label_create(scr);
    lv_label_set_text(lbl_distance, "--- cm");
    lv_obj_set_style_text_color(lbl_distance, lv_color_hex(0x3FB950), 0);
    lv_obj_align(lbl_distance, LV_ALIGN_CENTER, 0, -40);

    lbl_distance_raw = lv_label_create(scr);
    lv_label_set_text(lbl_distance_raw, "Brut : --- cm");
    lv_obj_set_style_text_color(lbl_distance_raw, lv_color_hex(0x8B949E), 0);
    lv_obj_align(lbl_distance_raw, LV_ALIGN_CENTER, 0, -10);

    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 420, 18);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(bar, 0, 400);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3FB950), LV_PART_INDICATOR);

    lbl_light = lv_label_create(scr);
    lv_label_set_text(lbl_light, "Luminosite : ---");
    lv_obj_set_style_text_color(lbl_light, lv_color_hex(0xFFD700), 0);
    lv_obj_align(lbl_light, LV_ALIGN_CENTER, -100, 55);

    lbl_response = lv_label_create(scr);
    lv_label_set_text(lbl_response, "Reponse : --- ms");
    lv_obj_set_style_text_color(lbl_response, lv_color_hex(0x8B949E), 0);
    lv_obj_align(lbl_response, LV_ALIGN_CENTER, 80, 55);

    lbl_counter = lv_label_create(scr);
    lv_label_set_text(lbl_counter, "Mesures : 0");
    lv_obj_set_style_text_color(lbl_counter, lv_color_hex(0x8B949E), 0);
    lv_obj_align(lbl_counter, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "En attente...");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x8B949E), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

static void update_ui(SRF08_Result_t *r)
{
    char buf[48];
    measure_count++;
    lvglLock();
    snprintf(buf, sizeof(buf), "Mesures : %lu", measure_count);
    lv_label_set_text(lbl_counter, buf);

    if (!r->valid) {
        lv_label_set_text(lbl_distance, "ERREUR");
        lv_obj_set_style_text_color(lbl_distance, lv_color_hex(0xFF4444), 0);
        lv_label_set_text(lbl_distance_raw, "Brut : --- cm");
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_light, "Luminosite : ---");
        lv_label_set_text(lbl_response, "Reponse : --- ms");
        lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE " Non detecte");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF4444), 0);
        lvglUnlock();
        return;
    }

    uint16_t dist_avg = push_average(r->distance_cm);
    lv_color_t col = dist_color(dist_avg);

    snprintf(buf, sizeof(buf), "%d cm", dist_avg);
    lv_label_set_text(lbl_distance, buf);
    lv_obj_set_style_text_color(lbl_distance, col, 0);

    snprintf(buf, sizeof(buf), "Brut : %d cm", r->distance_cm);
    lv_label_set_text(lbl_distance_raw, buf);

    int bar_val = 400 - dist_avg;
    if (bar_val < 0) bar_val = 0;
    lv_bar_set_value(bar, bar_val, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar, col, LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "Luminosite : %d", r->light);
    lv_label_set_text(lbl_light, buf);

    snprintf(buf, sizeof(buf), "Reponse : %lums", r->response_ms);
    lv_label_set_text(lbl_response, buf);

    lv_label_set_text(lbl_status, LV_SYMBOL_OK " Capteur OK");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x3FB950), 0);
    lvglUnlock();
}

void mySetup()
{
    Wire.begin();  /* Init I2C via Arduino Wire — PB8/PB9 par défaut */
    build_ui();
}

void loop() {}

void myTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    TickType_t xLastWakeTime = xTaskGetTickCount();
    SRF08_Result_t result;

    while (1)
    {
        SRF08_Measure(&result);
        update_ui(&result);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
    }
}

#else
#include "app_hal.h"

typedef struct { uint16_t distance_cm; uint8_t light; uint32_t response_ms; uint8_t valid; } SRF08_Result_t;
static lv_obj_t *lbl_distance, *lbl_distance_raw, *lbl_light, *lbl_status, *lbl_counter, *lbl_response, *bar;
static uint32_t measure_count = 0;
static lv_color_t dist_color(uint16_t cm) { if(cm<20) return lv_color_hex(0xFF4444); if(cm<50) return lv_color_hex(0xFFD700); return lv_color_hex(0x3FB950); }
static uint16_t push_average(uint16_t v) { return v; }
static void build_ui() {}
static void update_ui(SRF08_Result_t *r) { (void)r; }

int main(void)
{
    printf("Simulateur — Test SRF08\n");
    lv_init();
    hal_setup();
    build_ui();
    SRF08_Result_t fake = {123, 45, 72, 1};
    update_ui(&fake);
    hal_loop();
    return 0;
}
#endif