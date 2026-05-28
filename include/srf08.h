#ifndef SRF08_H
#define SRF08_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* ──────────────────────────────────────────────
   Adresses I2C des deux capteurs (7 bits)
   SRF08 utilise des adresses 8 bits : 0xE0, 0xE2…
   HAL attend du 7 bits → on décale d'1 bit à droite
   ────────────────────────────────────────────── */
#define SRF08_ADDR1_7BIT    (0xE0 >> 1)   /* Capteur 1 : adresse par défaut */
#define SRF08_ADDR2_7BIT    (0xE2 >> 1)   /* Capteur 2 : adresse reprogrammée */

/* Registres */
#define SRF08_REG_CMD       0x00   /* Registre commande / version logicielle (lecture) */
#define SRF08_REG_LIGHT     0x01   /* Registre lumière ambiante */
#define SRF08_REG_RANGE_H   0x02   /* Octet haut distance (premier écho) */
#define SRF08_REG_RANGE_L   0x03   /* Octet bas  distance (premier écho) */
#define SRF08_REG_MAX_GAIN  0x01   /* Gain max (écriture) */
#define SRF08_REG_MAX_RANGE 0x02   /* Portée max (écriture) */

/* Commandes de mesure */
#define SRF08_CMD_INCHES    0x50
#define SRF08_CMD_CM        0x51
#define SRF08_CMD_USEC      0x52

/* Timeout I2C (ms) */
#define SRF08_I2C_TIMEOUT   100

/* Délai de mesure (ms) — le SRF08 a besoin ~65 ms pour compléter une mesure */
#define SRF08_MEASURE_DELAY 70

/* ──────────────────────────────────────────────
   Structures
   ────────────────────────────────────────────── */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t           addr;        /* Adresse 7 bits */
    uint16_t           distance_cm; /* Dernière distance mesurée */
    uint8_t            light;       /* Luminosité ambiante */
    uint8_t            sw_version;  /* Version firmware du capteur */
    uint8_t            valid;       /* 1 si la dernière mesure est valide */
} SRF08_t;

/* ──────────────────────────────────────────────
   Prototypes
   ────────────────────────────────────────────── */
HAL_StatusTypeDef SRF08_Init(SRF08_t *sensor, I2C_HandleTypeDef *hi2c, uint16_t addr_7bit);
HAL_StatusTypeDef SRF08_StartMeasurement(SRF08_t *sensor);
HAL_StatusTypeDef SRF08_ReadResult(SRF08_t *sensor);
HAL_StatusTypeDef SRF08_Measure(SRF08_t *sensor);          /* Start + délai + Read */
HAL_StatusTypeDef SRF08_ChangeAddress(SRF08_t *sensor, uint8_t new_addr_8bit);
HAL_StatusTypeDef SRF08_SetMaxRange(SRF08_t *sensor, uint8_t range_reg);
HAL_StatusTypeDef SRF08_SetMaxGain(SRF08_t *sensor, uint8_t gain_reg);

#endif /* SRF08_H */
