/**
 * @file    srf08.c
 * @brief   Driver pour les capteurs ultrason SRF08 sur STM32F746G-DISCO
 *          Deux capteurs sur le même bus I2C1, adresses 0xE0 et 0xE2
 */

#include "srf08.h"

/* ─────────────────────────────────────────────────────────────────────────
   Initialisation : vérifie la communication et lit la version firmware
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_Init(SRF08_t *sensor, I2C_HandleTypeDef *hi2c, uint16_t addr_7bit)
{
    sensor->hi2c        = hi2c;
    sensor->addr        = addr_7bit;
    sensor->distance_cm = 0;
    sensor->light       = 0;
    sensor->sw_version  = 0;
    sensor->valid       = 0;

    uint8_t reg = SRF08_REG_CMD;
    uint8_t version = 0;

    /* Lecture du registre 0 → retourne la version du firmware si le capteur répond */
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(sensor->hi2c,
                                                        sensor->addr << 1,
                                                        &reg, 1,
                                                        SRF08_I2C_TIMEOUT);
    if (status != HAL_OK) return status;

    status = HAL_I2C_Master_Receive(sensor->hi2c,
                                    sensor->addr << 1,
                                    &version, 1,
                                    SRF08_I2C_TIMEOUT);
    if (status == HAL_OK)
        sensor->sw_version = version;

    return status;
}

/* ─────────────────────────────────────────────────────────────────────────
   Lancer une mesure (retour en centimètres)
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_StartMeasurement(SRF08_t *sensor)
{
    uint8_t buf[2] = { SRF08_REG_CMD, SRF08_CMD_CM };
    return HAL_I2C_Master_Transmit(sensor->hi2c,
                                   sensor->addr << 1,
                                   buf, 2,
                                   SRF08_I2C_TIMEOUT);
}

/* ─────────────────────────────────────────────────────────────────────────
   Lire le résultat (appeler après SRF08_MEASURE_DELAY ms)
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_ReadResult(SRF08_t *sensor)
{
    /* Pointer sur le registre luminosité (0x01), puis lire 3 octets :
       0x01 = lumière, 0x02 = distance haut, 0x03 = distance bas */
    uint8_t reg = SRF08_REG_LIGHT;
    uint8_t buf[3] = {0};

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(sensor->hi2c,
                                                        sensor->addr << 1,
                                                        &reg, 1,
                                                        SRF08_I2C_TIMEOUT);
    if (status != HAL_OK) {
        sensor->valid = 0;
        return status;
    }

    status = HAL_I2C_Master_Receive(sensor->hi2c,
                                    sensor->addr << 1,
                                    buf, 3,
                                    SRF08_I2C_TIMEOUT);
    if (status == HAL_OK) {
        sensor->light       = buf[0];
        sensor->distance_cm = ((uint16_t)buf[1] << 8) | buf[2];
        sensor->valid       = 1;
    } else {
        sensor->valid = 0;
    }

    return status;
}

/* ─────────────────────────────────────────────────────────────────────────
   Mesure complète bloquante (start → attente → lecture)
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_Measure(SRF08_t *sensor)
{
    HAL_StatusTypeDef status = SRF08_StartMeasurement(sensor);
    if (status != HAL_OK) return status;
    HAL_Delay(SRF08_MEASURE_DELAY);
    return SRF08_ReadResult(sensor);
}

/* ─────────────────────────────────────────────────────────────────────────
   Changer l'adresse I2C du capteur (procédure spéciale SRF08)
   new_addr_8bit : 0xE0, 0xE2, 0xE4 … 0xFE  (toujours pair)
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_ChangeAddress(SRF08_t *sensor, uint8_t new_addr_8bit)
{
    HAL_StatusTypeDef status;
    uint8_t seq[2];

    /* Séquence obligatoire : 3 écritures puis la nouvelle adresse */
    seq[0] = SRF08_REG_CMD; seq[1] = 0xA0;
    status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->addr << 1, seq, 2, SRF08_I2C_TIMEOUT);
    if (status != HAL_OK) return status;

    seq[1] = 0xAA;
    status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->addr << 1, seq, 2, SRF08_I2C_TIMEOUT);
    if (status != HAL_OK) return status;

    seq[1] = 0xA5;
    status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->addr << 1, seq, 2, SRF08_I2C_TIMEOUT);
    if (status != HAL_OK) return status;

    seq[1] = new_addr_8bit;
    status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->addr << 1, seq, 2, SRF08_I2C_TIMEOUT);
    if (status == HAL_OK)
        sensor->addr = new_addr_8bit >> 1;  /* Mettre à jour l'adresse locale */

    HAL_Delay(100);  /* Attendre que le capteur sauvegarde en EEPROM */
    return status;
}

/* ─────────────────────────────────────────────────────────────────────────
   Configurer la portée max
   range_reg : 0x00 = 43mm ... 0xFF = 11008mm (pas de 43mm)
               Ex : 0xFF pour portée maximale ~11 mètres
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_SetMaxRange(SRF08_t *sensor, uint8_t range_reg)
{
    uint8_t buf[2] = { SRF08_REG_MAX_RANGE, range_reg };
    return HAL_I2C_Master_Transmit(sensor->hi2c,
                                   sensor->addr << 1,
                                   buf, 2,
                                   SRF08_I2C_TIMEOUT);
}

/* ─────────────────────────────────────────────────────────────────────────
   Configurer le gain max
   gain_reg : 0x00 = 94, ... 0x1F = 1025 (gain max)
   ───────────────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_SetMaxGain(SRF08_t *sensor, uint8_t gain_reg)
{
    uint8_t buf[2] = { SRF08_REG_MAX_GAIN, gain_reg };
    return HAL_I2C_Master_Transmit(sensor->hi2c,
                                   sensor->addr << 1,
                                   buf, 2,
                                   SRF08_I2C_TIMEOUT);
}
