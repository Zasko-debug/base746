/*
 * TEST SIMPLE — Un seul capteur SRF08 sur 0xE0
 * Affiche la distance sur l'écran, se rafraîchit toutes les 200ms
 */

#include "lvgl.h"
#include <stdio.h>

#ifdef ARDUINO
#include "lvglDrivers.h"
#include "stm32f7xx_hal.h"

I2C_HandleTypeDef hi2c1;

static void I2C1_Init()
{
    /* GPIO PB8=SCL, PB9=SDA */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    __HAL_RCC_I2C1_CLK_ENABLE();
    hi2c1.Instance             = I2C1;
    hi2c1.Init.Timing          = 0x00C0EAFF;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

/* Lit la distance en cm sur le SRF08 à l'adresse 0xE0 */
static uint16_t SRF08_ReadDistance()
{
    /* Lancer la mesure */
    uint8_t cmd[2] = {0x00, 0x51};
    if (HAL_I2C_Master_Transmit(&hi2c1, 0xE0, cmd, 2, 100) != HAL_OK)
        return 9999; /* Erreur */

    /* Attendre fin de mesure */
    HAL_Delay(70);

    /* Lire les 3 registres : lumière + distance haute + distance basse */
    uint8_t reg = 0x01;
    uint8_t raw[3] = {0};
    if (HAL_I2C_Master_Transmit(&hi2c1, 0xE0, &reg, 1, 100) != HAL_OK)
        return 9999;
    if (HAL_I2C_Master_Receive(&hi2c1, 0xE0, raw, 3, 100) != HAL_OK)
        return 9999;

    return ((uint16_t)raw[1] << 8) | raw[2];
}

/* ── Widgets ── */
static lv_obj_t *lbl_distance;
static lv_obj_t *lbl_status;
static lv_obj_t *bar;

static void build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);

    /* Titre */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "TEST SRF08");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00D4FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    /* Adresse */
    lv_obj_t *addr = lv_label_create(scr);
    lv_label_set_text(addr, "Capteur : 0xE0  |  I2C1 PB8/PB9");
    lv_obj_set_style_text_color(addr, lv_color_hex(0x8B949E), 0);
    lv_obj_align(addr, LV_ALIGN_TOP_MID, 0, 40);

    /* Distance en grand */
    lbl_distance = lv_label_create(scr);
    lv_label_set_text(lbl_distance, "--- cm");
    lv_obj_set_style_text_color(lbl_distance, lv_color_hex(0x3FB950), 0);
    lv_obj_align(lbl_distance, LV_ALIGN_CENTER, 0, -20);

    /* Barre de progression */
    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 400, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(bar, 0, 400);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3FB950), LV_PART_INDICATOR);

    /* Statut I2C */
    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "En attente...");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x8B949E), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui(uint16_t dist)
{
    char buf[32];

    if (dist == 9999) {
        lv_label_set_text(lbl_distance, "ERREUR");
        lv_obj_set_style_text_color(lbl_distance, lv_color_hex(0xFF4444), 0);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE " Capteur non detecte sur 0xE0");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF4444), 0);
    } else {
        /* Couleur selon distance */
        lv_color_t col;
        if (dist < 20)       col = lv_color_hex(0xFF4444); /* Rouge  */
        else if (dist < 50)  col = lv_color_hex(0xFFD700); /* Jaune  */
        else                 col = lv_color_hex(0x3FB950); /* Vert   */

        snprintf(buf, sizeof(buf), "%d cm", dist);
        lv_label_set_text(lbl_distance, buf);
        lv_obj_set_style_text_color(lbl_distance, col, 0);

        int bar_val = 400 - dist;
        if (bar_val < 0) bar_val = 0;
        lv_bar_set_value(bar, bar_val, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar, col, LV_PART_INDICATOR);

        lv_label_set_text(lbl_status, LV_SYMBOL_OK " Capteur OK");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x3FB950), 0);
    }
}

void mySetup()
{
    I2C1_Init();
    build_ui();
}

void loop() {}

void myTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        uint16_t dist = SRF08_ReadDistance();
        update_ui(dist);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
    }
}

#else
#include "app_hal.h"

int main(void)
{
    printf("Simulateur — Test SRF08\n");
    lv_init();
    hal_setup();
    build_ui();
    update_ui(123);
    hal_loop();
    return 0;
}
#endif