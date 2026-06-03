/*
 * ETAPE 1 — REPROGRAMMATION ADRESSE I2C
 * ======================================
 * Brancher UNIQUEMENT le capteur à reprogrammer (futur capteur 2)
 * Flasher ce fichier, laisser tourner 3 secondes, c'est bon.
 * Ce capteur aura ensuite l'adresse 0xE2 en permanence (EEPROM).
 * Ensuite passer au main.cpp normal avec les deux capteurs.
 */

#include "lvgl.h"
#include <stdio.h>

#ifdef ARDUINO
#include "lvglDrivers.h"
#include "stm32f7xx_hal.h"

I2C_HandleTypeDef hi2c1;

static void I2C1_Init()
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.Timing          = 0x00C0EAFF;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void show_result(uint8_t success)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "REPROGRAMMATION ADRESSE I2C");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6EDF3), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *lbl = lv_label_create(scr);
    if (success) {
        lv_label_set_text(lbl,
            LV_SYMBOL_OK "  Capteur detecte sur 0xE0\n\n"
            LV_SYMBOL_OK "  Adresse reprogrammee en 0xE2\n\n"
            "Colle un scotch sur ce capteur\n"
            "C'est maintenant le CAPTEUR 2");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x3FB950), 0);
    } else {
        lv_label_set_text(lbl,
            LV_SYMBOL_CLOSE "  Capteur NON detecte sur 0xE0\n\n"
            "Verifier le cablage I2C :\n"
            "- SDA sur PB9\n"
            "- SCL sur PB8\n"
            "- VCC 3.3V et GND branches\n"
            "- Pull-up 4.7k sur SDA et SCL");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF4444), 0);
    }
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

void mySetup()
{
    // Activer pull-up internes sur PB8 (SCL) et PB9 (SDA)
GPIO_InitTypeDef GPIO_InitStruct = {0};
__HAL_RCC_GPIOB_CLK_ENABLE();
GPIO_InitStruct.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
GPIO_InitStruct.Mode  = GPIO_MODE_AF_OD;
GPIO_InitStruct.Pull  = GPIO_PULLUP;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    I2C1_Init();

    /* Vérifie que le capteur répond sur 0xE0 */
    uint8_t detected = HAL_I2C_IsDeviceReady(&hi2c1, 0xE0, 3, 100) == HAL_OK;

    if (detected) {
        /* Séquence de reprogrammation SRF08 */
        uint8_t seq[2];
        seq[0] = 0x00; seq[1] = 0xA0;
        HAL_I2C_Master_Transmit(&hi2c1, 0xE0, seq, 2, 100);
        seq[1] = 0xAA;
        HAL_I2C_Master_Transmit(&hi2c1, 0xE0, seq, 2, 100);
        seq[1] = 0xA5;
        HAL_I2C_Master_Transmit(&hi2c1, 0xE0, seq, 2, 100);
        seq[1] = 0xE2;
        HAL_I2C_Master_Transmit(&hi2c1, 0xE0, seq, 2, 100);
        HAL_Delay(200); /* Laisse l'EEPROM sauvegarder */
    }

    show_result(detected);
}

void loop() {}

void myTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        /* Rien à faire, on attend juste */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

#else
#include "app_hal.h"

int main(void)
{
    printf("Simulateur — ecran reprogrammation\n");
    lv_init();
    hal_setup();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Ce fichier est uniquement pour la STM32\nPas de simulation disponible");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFD700), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    hal_loop();
    return 0;
}
#endif