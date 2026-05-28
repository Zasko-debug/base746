/**
 * @file    display.c
 * @brief   Interface tactile pour STM32F746G-DISCO avec 2 capteurs SRF08
 *
 *  Vue DASHBOARD  : 2 panneaux côte à côte, barres de progression, valeurs num.
 *  Vue RADAR      : point mobile sur cercle concentrique selon la distance
 *  Vue GRAPH      : historique 64 pts des 2 capteurs
 *  Vue SETTINGS   : boutons +/- pour gain et portée
 *
 *  Navigation : 4 boutons en bas de l'écran (toujours visibles)
 */

#include "display.h"
#include <stdio.h>
#include <string.h>

/* ─── Helpers couleur ─────────────────────────────────────────────────── */
static uint32_t distance_color(uint16_t cm) {
    if (cm < ALERT_NEAR)  return COLOR_RED;
    if (cm < ALERT_WARN)  return COLOR_WARN;
    return COLOR_GREEN;
}

/* ─── Barre de navigation en bas ─────────────────────────────────────── */
static void draw_navbar(UI_Context_t *ctx)
{
    const char *labels[] = {"DASH", "RADAR", "GRAPH", "SET"};
    uint32_t    colors[] = {COLOR_ACCENT1, COLOR_ACCENT2, COLOR_GREEN, COLOR_TEXT_DIM};
    int bw = LCD_WIDTH / 4;
    int bh = 32;
    int by = LCD_HEIGHT - bh;

    BSP_LCD_SetFont(&Font12);
    for (int i = 0; i < 4; i++) {
        uint32_t bg = (ctx->current_view == i) ? colors[i] : COLOR_PANEL;
        uint32_t fg = (ctx->current_view == i) ? COLOR_BG   : COLOR_TEXT_DIM;
        BSP_LCD_SetTextColor(bg);
        BSP_LCD_FillRect(i * bw, by, bw - 1, bh);
        BSP_LCD_SetTextColor(fg);
        BSP_LCD_SetBackColor(bg);
        int lx = i * bw + (bw - strlen(labels[i]) * 7) / 2;
        BSP_LCD_DisplayStringAt(lx, by + 9, (uint8_t *)labels[i], LEFT_MODE);
    }
}

/* ─── Panneau d'un capteur (Dashboard) ───────────────────────────────── */
static void draw_sensor_panel(int x, int y, int w, int h,
                               SRF08_t *s, const char *name, uint32_t accent)
{
    char buf[32];
    uint16_t dist = s->valid ? s->distance_cm : 9999;
    uint32_t col  = s->valid ? distance_color(dist) : COLOR_TEXT_DIM;

    /* Fond panneau */
    BSP_LCD_SetTextColor(COLOR_PANEL);
    BSP_LCD_FillRect(x, y, w, h);

    /* Bordure accent */
    BSP_LCD_SetTextColor(accent);
    BSP_LCD_DrawRect(x, y, w, h);
    BSP_LCD_DrawRect(x+1, y+1, w-2, h-2);

    /* Titre */
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_SetTextColor(accent);
    BSP_LCD_SetBackColor(COLOR_PANEL);
    BSP_LCD_DisplayStringAt(x + 8, y + 8, (uint8_t *)name, LEFT_MODE);

    /* Distance en gros */
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_SetTextColor(col);
    if (s->valid)
        snprintf(buf, sizeof(buf), "%4d cm", dist);
    else
        snprintf(buf, sizeof(buf), "  --- cm");
    BSP_LCD_DisplayStringAt(x + 8, y + 30, (uint8_t *)buf, LEFT_MODE);

    /* Barre de progression */
    int bar_x = x + 8;
    int bar_y = y + 62;
    int bar_w = w - 16;
    int bar_h = 14;

    BSP_LCD_SetTextColor(COLOR_BAR_BG);
    BSP_LCD_FillRect(bar_x, bar_y, bar_w, bar_h);

    if (s->valid && dist < MAX_DISPLAY_CM) {
        int filled = bar_w - (int)((uint32_t)dist * bar_w / MAX_DISPLAY_CM);
        if (filled < 2) filled = 2;
        BSP_LCD_SetTextColor(col);
        BSP_LCD_FillRect(bar_x, bar_y, filled, bar_h);
    }

    /* Luminosité ambiante */
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
    snprintf(buf, sizeof(buf), "LUX: %3d", s->light);
    BSP_LCD_DisplayStringAt(x + 8, y + 82, (uint8_t *)buf, LEFT_MODE);

    /* Indicateur état I2C */
    BSP_LCD_SetTextColor(s->valid ? COLOR_GREEN : COLOR_RED);
    BSP_LCD_FillCircle(x + w - 12, y + 12, 5);
}

/* ─── DASHBOARD ─────────────────────────────────────────────────────── */
void Display_DrawDashboard(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT - 32);

    int pw = (LCD_WIDTH - 16) / 2;
    int ph = LCD_HEIGHT - 32 - 10;

    draw_sensor_panel(4,          4, pw, ph, s1, "CAPTEUR 1  [0xE0]", COLOR_ACCENT1);
    draw_sensor_panel(8 + pw,     4, pw, ph, s2, "CAPTEUR 2  [0xE2]", COLOR_ACCENT2);

    /* Ligne séparatrice centrale */
    BSP_LCD_SetTextColor(COLOR_PANEL);
    BSP_LCD_DrawVLine(LCD_WIDTH / 2, 4, ph);

    draw_navbar(ctx);
}

/* ─── RADAR ──────────────────────────────────────────────────────────── */
void Display_DrawRadar(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    (void)ctx;
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT - 32);

    int cx = LCD_WIDTH / 2;
    int cy = (LCD_HEIGHT - 32) / 2;
    int r_max = cy - 10;

    /* Cercles concentriques */
    uint32_t ring_colors[] = {0xFF1A2A1A, 0xFF1A2A1A, 0xFF1A2A1A, 0xFF1A2A1A};
    int radii[] = {r_max, r_max*3/4, r_max/2, r_max/4};
    for (int i = 0; i < 4; i++) {
        BSP_LCD_SetTextColor(ring_colors[i]);
        BSP_LCD_DrawCircle(cx, cy, radii[i]);
    }

    /* Axes */
    BSP_LCD_SetTextColor(0xFF1A3A1A);
    BSP_LCD_DrawHLine(cx - r_max, cy, 2 * r_max);
    BSP_LCD_DrawVLine(cx, cy - r_max, 2 * r_max);

    /* Étiquettes distances */
    char lbl[16];
    BSP_LCD_SetFont(&Font8);
    BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
    for (int i = 1; i <= 4; i++) {
        int cm_val = (MAX_DISPLAY_CM / 4) * i;
        snprintf(lbl, sizeof(lbl), "%d", cm_val);
        BSP_LCD_DisplayStringAt(cx + radii[4 - i] + 2, cy - 8,
                                 (uint8_t *)lbl, LEFT_MODE);
    }

    /* Point capteur 1 (en haut, axe vertical négatif = proche) */
    if (s1->valid) {
        int d1 = s1->distance_cm > MAX_DISPLAY_CM ? MAX_DISPLAY_CM : s1->distance_cm;
        int py1 = cy - (int)((uint32_t)d1 * r_max / MAX_DISPLAY_CM);
        BSP_LCD_SetTextColor(COLOR_ACCENT1);
        BSP_LCD_FillCircle(cx, py1, 6);
        BSP_LCD_SetFont(&Font12);
        BSP_LCD_SetBackColor(COLOR_BG);
        snprintf(lbl, sizeof(lbl), "C1:%dcm", s1->distance_cm);
        BSP_LCD_DisplayStringAt(cx + 10, py1 - 6, (uint8_t *)lbl, LEFT_MODE);
    }

    /* Point capteur 2 (à droite, axe horizontal positif) */
    if (s2->valid) {
        int d2 = s2->distance_cm > MAX_DISPLAY_CM ? MAX_DISPLAY_CM : s2->distance_cm;
        int px2 = cx + (int)((uint32_t)d2 * r_max / MAX_DISPLAY_CM);
        BSP_LCD_SetTextColor(COLOR_ACCENT2);
        BSP_LCD_FillCircle(px2, cy, 6);
        BSP_LCD_SetFont(&Font12);
        BSP_LCD_SetBackColor(COLOR_BG);
        snprintf(lbl, sizeof(lbl), "C2:%dcm", s2->distance_cm);
        BSP_LCD_DisplayStringAt(px2 - 50, cy + 10, (uint8_t *)lbl, LEFT_MODE);
    }

    draw_navbar(ctx);
}

/* ─── GRAPH ──────────────────────────────────────────────────────────── */
void Display_DrawGraph(UI_Context_t *ctx)
{
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT - 32);

    int gx = 30, gy = 10;
    int gw = LCD_WIDTH - gx - 10;
    int gh = LCD_HEIGHT - 32 - gy - 20;

    /* Axes */
    BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
    BSP_LCD_DrawVLine(gx, gy, gh);
    BSP_LCD_DrawHLine(gx, gy + gh, gw);

    /* Grille */
    for (int i = 1; i <= 4; i++) {
        int ly = gy + gh * i / 4;
        BSP_LCD_SetTextColor(COLOR_PANEL);
        BSP_LCD_DrawHLine(gx + 1, ly, gw);
        BSP_LCD_SetFont(&Font8);
        BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%d", MAX_DISPLAY_CM * (4 - i) / 4);
        BSP_LCD_DisplayStringAt(0, ly - 4, (uint8_t *)lbl, LEFT_MODE);
    }

    /* Courbe capteur 1 (cyan) */
    BSP_LCD_SetTextColor(COLOR_ACCENT1);
    for (int i = 1; i < 64; i++) {
        int ix = (ctx->hist_idx + i) & 63;
        int px0 = gx + (i - 1) * gw / 63;
        int px1 = gx + i       * gw / 63;
        int v0  = ctx->hist1[(ctx->hist_idx + i - 1) & 63];
        int v1  = ctx->hist1[ix];
        if (v0 > MAX_DISPLAY_CM) v0 = MAX_DISPLAY_CM;
        if (v1 > MAX_DISPLAY_CM) v1 = MAX_DISPLAY_CM;
        int py0 = gy + gh - (int)((uint32_t)v0 * gh / MAX_DISPLAY_CM);
        int py1 = gy + gh - (int)((uint32_t)v1 * gh / MAX_DISPLAY_CM);
        BSP_LCD_DrawLine(px0, py0, px1, py1);
    }

    /* Courbe capteur 2 (orange) */
    BSP_LCD_SetTextColor(COLOR_ACCENT2);
    for (int i = 1; i < 64; i++) {
        int ix = (ctx->hist_idx + i) & 63;
        int px0 = gx + (i - 1) * gw / 63;
        int px1 = gx + i       * gw / 63;
        int v0  = ctx->hist2[(ctx->hist_idx + i - 1) & 63];
        int v1  = ctx->hist2[ix];
        if (v0 > MAX_DISPLAY_CM) v0 = MAX_DISPLAY_CM;
        if (v1 > MAX_DISPLAY_CM) v1 = MAX_DISPLAY_CM;
        int py0 = gy + gh - (int)((uint32_t)v0 * gh / MAX_DISPLAY_CM);
        int py1 = gy + gh - (int)((uint32_t)v1 * gh / MAX_DISPLAY_CM);
        BSP_LCD_DrawLine(px0, py0, px1, py1);
    }

    /* Légende */
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(COLOR_ACCENT1);
    BSP_LCD_SetBackColor(COLOR_BG);
    BSP_LCD_DisplayStringAt(gx + 4, gy + 2, (uint8_t *)"-- C1", LEFT_MODE);
    BSP_LCD_SetTextColor(COLOR_ACCENT2);
    BSP_LCD_DisplayStringAt(gx + 60, gy + 2, (uint8_t *)"-- C2", LEFT_MODE);

    draw_navbar(ctx);
}

/* ─── SETTINGS ───────────────────────────────────────────────────────── */
void Display_DrawSettings(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    (void)s1; (void)s2;
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT - 32);

    BSP_LCD_SetFont(&Font16);
    BSP_LCD_SetTextColor(COLOR_TEXT);
    BSP_LCD_SetBackColor(COLOR_BG);
    BSP_LCD_DisplayStringAt(10, 10, (uint8_t *)"REGLAGES CAPTEURS", LEFT_MODE);

    char buf[40];

    /* ─ Gain ─ */
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
    BSP_LCD_DisplayStringAt(10, 45, (uint8_t *)"Gain max (reg 0x01):", LEFT_MODE);

    snprintf(buf, sizeof(buf), "0x%02X  (%d/31)", ctx->gain_setting, ctx->gain_setting);
    BSP_LCD_SetTextColor(COLOR_ACCENT1);
    BSP_LCD_DisplayStringAt(200, 45, (uint8_t *)buf, LEFT_MODE);

    /* Boutons gain */
    BSP_LCD_SetTextColor(COLOR_ACCENT1);
    BSP_LCD_FillRect(10,  65, 40, 28);   /* [-] */
    BSP_LCD_FillRect(60,  65, 40, 28);   /* [+] */
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_SetBackColor(COLOR_ACCENT1);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(22, 70, (uint8_t *)"-", LEFT_MODE);
    BSP_LCD_DisplayStringAt(72, 70, (uint8_t *)"+", LEFT_MODE);

    /* ─ Portée ─ */
    BSP_LCD_SetBackColor(COLOR_BG);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(COLOR_TEXT_DIM);
    BSP_LCD_DisplayStringAt(10, 110, (uint8_t *)"Portee max (reg 0x02):", LEFT_MODE);

    int range_mm = 43 * ((int)ctx->range_setting + 1);
    snprintf(buf, sizeof(buf), "0x%02X  (~%d mm)", ctx->range_setting, range_mm);
    BSP_LCD_SetTextColor(COLOR_ACCENT2);
    BSP_LCD_DisplayStringAt(200, 110, (uint8_t *)buf, LEFT_MODE);

    /* Boutons portée */
    BSP_LCD_SetTextColor(COLOR_ACCENT2);
    BSP_LCD_FillRect(10, 130, 40, 28);
    BSP_LCD_FillRect(60, 130, 40, 28);
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_SetBackColor(COLOR_ACCENT2);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(22, 135, (uint8_t *)"-", LEFT_MODE);
    BSP_LCD_DisplayStringAt(72, 135, (uint8_t *)"+", LEFT_MODE);

    /* Bouton APPLIQUER */
    BSP_LCD_SetTextColor(COLOR_GREEN);
    BSP_LCD_FillRect(10, 175, 150, 30);
    BSP_LCD_SetTextColor(COLOR_BG);
    BSP_LCD_SetBackColor(COLOR_GREEN);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(30, 182, (uint8_t *)"APPLIQUER", LEFT_MODE);

    draw_navbar(ctx);
}

/* ─── Initialisation ─────────────────────────────────────────────────── */
void Display_Init(UI_Context_t *ctx)
{
    memset(ctx, 0, sizeof(UI_Context_t));
    ctx->current_view    = VIEW_DASHBOARD;
    ctx->refresh_needed  = 1;
    ctx->gain_setting    = 0x1F;   /* Gain max par défaut */
    ctx->range_setting   = 0xFF;   /* Portée max par défaut */

    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(0);
    BSP_LCD_DisplayOn();
    BSP_LCD_Clear(COLOR_BG);

    BSP_TS_Init(LCD_WIDTH, LCD_HEIGHT);
}

/* ─── Historique ─────────────────────────────────────────────────────── */
void Display_PushHistory(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    ctx->hist1[ctx->hist_idx] = s1->valid ? s1->distance_cm : MAX_DISPLAY_CM;
    ctx->hist2[ctx->hist_idx] = s2->valid ? s2->distance_cm : MAX_DISPLAY_CM;
    ctx->hist_idx = (ctx->hist_idx + 1) & 63;
}

/* ─── Gestion du toucher ─────────────────────────────────────────────── */
void Display_HandleTouch(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    TS_StateTypeDef ts;
    BSP_TS_GetState(&ts);
    if (!ts.touchDetected) return;

    int tx = ts.touchX[0];
    int ty = ts.touchY[0];

    /* ── Navigation en bas ── */
    if (ty >= LCD_HEIGHT - 32) {
        int bw = LCD_WIDTH / 4;
        ViewMode_t new_view = (ViewMode_t)(tx / bw);
        if (new_view != ctx->current_view) {
            ctx->current_view   = new_view;
            ctx->refresh_needed = 1;
        }
        HAL_Delay(200);  /* Anti-rebond */
        return;
    }

    /* ── Actions dans SETTINGS ── */
    if (ctx->current_view == VIEW_SETTINGS) {
        /* Gain - */
        if (tx >= 10 && tx <= 50 && ty >= 65 && ty <= 93) {
            if (ctx->gain_setting > 0) ctx->gain_setting--;
            ctx->refresh_needed = 1;
        }
        /* Gain + */
        if (tx >= 60 && tx <= 100 && ty >= 65 && ty <= 93) {
            if (ctx->gain_setting < 0x1F) ctx->gain_setting++;
            ctx->refresh_needed = 1;
        }
        /* Portée - */
        if (tx >= 10 && tx <= 50 && ty >= 130 && ty <= 158) {
            if (ctx->range_setting > 0) ctx->range_setting--;
            ctx->refresh_needed = 1;
        }
        /* Portée + */
        if (tx >= 60 && tx <= 100 && ty >= 130 && ty <= 158) {
            if (ctx->range_setting < 0xFF) ctx->range_setting++;
            ctx->refresh_needed = 1;
        }
        /* APPLIQUER */
        if (tx >= 10 && tx <= 160 && ty >= 175 && ty <= 205) {
            SRF08_SetMaxGain(s1, ctx->gain_setting);
            SRF08_SetMaxGain(s2, ctx->gain_setting);
            SRF08_SetMaxRange(s1, ctx->range_setting);
            SRF08_SetMaxRange(s2, ctx->range_setting);
            /* Feedback visuel bref */
            BSP_LCD_SetTextColor(COLOR_GREEN);
            BSP_LCD_FillRect(10, 175, 150, 30);
            BSP_LCD_SetTextColor(COLOR_BG);
            BSP_LCD_SetBackColor(COLOR_GREEN);
            BSP_LCD_SetFont(&Font16);
            BSP_LCD_DisplayStringAt(30, 182, (uint8_t *)"APPLIQUE!", LEFT_MODE);
            HAL_Delay(600);
            ctx->refresh_needed = 1;
        }
        HAL_Delay(150);
    }
}

/* ─── Mise à jour principale ─────────────────────────────────────────── */
void Display_Update(UI_Context_t *ctx, SRF08_t *s1, SRF08_t *s2)
{
    if (!ctx->refresh_needed) return;
    ctx->refresh_needed = 0;

    switch (ctx->current_view) {
        case VIEW_DASHBOARD: Display_DrawDashboard(ctx, s1, s2); break;
        case VIEW_RADAR:     Display_DrawRadar(ctx, s1, s2);     break;
        case VIEW_GRAPH:     Display_DrawGraph(ctx);              break;
        case VIEW_SETTINGS:  Display_DrawSettings(ctx, s1, s2);  break;
    }
}
