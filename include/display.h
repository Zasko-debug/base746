#ifndef DISPLAY_H
#define DISPLAY_H

#include "stm32f7xx_hal.h"
#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_ts.h"
#include "srf08.h"

/* ── Dimensions écran F746G-DISCO ── */
#define LCD_WIDTH   480
#define LCD_HEIGHT  272

/* ── Couleurs (ARGB8888) ── */
#define COLOR_BG          0xFF0D1117   /* Fond noir-bleu */
#define COLOR_PANEL       0xFF161B22   /* Panneaux gris foncé */
#define COLOR_ACCENT1     0xFF00D4FF   /* Cyan vif  — Capteur 1 */
#define COLOR_ACCENT2     0xFFFF6B35   /* Orange vif — Capteur 2 */
#define COLOR_TEXT        0xFFE6EDF3   /* Blanc cassé */
#define COLOR_TEXT_DIM    0xFF8B949E   /* Gris doux */
#define COLOR_GREEN       0xFF3FB950   /* Vert OK */
#define COLOR_RED         0xFFFF4444   /* Rouge alerte */
#define COLOR_WARN        0xFFFFD700   /* Jaune avertissement */
#define COLOR_BAR_BG      0xFF21262D   /* Fond barres de progression */

/* ── Seuils d'alerte (cm) ── */
#define ALERT_NEAR        20           /* < 20 cm → rouge */
#define ALERT_WARN        50           /* < 50 cm → jaune */
#define MAX_DISPLAY_CM    400          /* Portée max affichée */

/* ── Modes d'affichage ── */
typedef enum {
    VIEW_DASHBOARD = 0,   /* Vue principale : 2 capteurs côte à côte */
    VIEW_RADAR,           /* Vue radar animée */
    VIEW_GRAPH,           /* Historique temporel */
    VIEW_SETTINGS         /* Réglages portée / gain */
} ViewMode_t;

/* ── Contexte UI ── */
typedef struct {
    ViewMode_t   current_view;
    uint8_t      refresh_needed;
    uint32_t     last_refresh_tick;

    /* Historique pour le graphe (64 points) */
    uint16_t     hist1[64];
    uint16_t     hist2[64];
    uint8_t      hist_idx;

    /* Réglages */
    uint8_t      gain_setting;        /* 0x00–0x1F */
    uint8_t      range_setting;       /* 0x00–0xFF */
} UI_Context_t;

/* ── Prototypes ── */
void Display_Init(UI_Context_t *ctx);
void Display_Update(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);
void Display_HandleTouch(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);
void Display_DrawDashboard(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);
void Display_DrawRadar(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);
void Display_DrawGraph(UI_Context_t *ctx);
void Display_DrawSettings(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);
void Display_PushHistory(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2);

#endif /* DISPLAY_H */
