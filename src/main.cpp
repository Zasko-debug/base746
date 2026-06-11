/*
 * DUAL SRF08 — Deux capteurs sur le même bus I2C
 *
 * IMPORTANT — Reprogrammer l'adresse du second capteur AVANT de câbler les deux :
 *   1. Brancher UNIQUEMENT le capteur n°2 sur le bus I2C
 *   2. Flasher le sketch d'adressage ci-dessous (section ADDR_PROG)
 *   3. Le capteur n°2 répondra désormais sur 0xE2 (7-bit : 0x71)
 *   4. Brancher les deux capteurs ensemble — ils coexistent sur le même bus
 *
 * Adresses utilisées :
 *   Capteur GAUCHE  →  0xE0  (défaut SRF08, 7-bit Wire : 0x70)
 *   Capteur DROIT   →  0xE2  (reprogrammé,  7-bit Wire : 0x71)
 *
 * Câblage I2C STM32F746NG Disco :
 *   SCL → PB8   SDA → PB9   (broches Arduino CN4)
 *   Alimenter les deux SRF08 en 5 V (Vin) — tolérant 3.3 V sur SDA/SCL
 */

/* ═══════════════════════════════════════════════════════════════════════
   SKETCH DE REPROGRAMMATION D'ADRESSE  (à flasher seul, capteur isolé)
   ═════════════════════════════════════════════════════════════════════

void setup() {
    Wire.begin();
    Wire.beginTransmission(0x70);   // adresse 7-bit de l'adresse 0xE0 par défaut
    Wire.write(0x00); Wire.write(0xA0); Wire.endTransmission(); delay(20);
    Wire.beginTransmission(0x70);
    Wire.write(0x00); Wire.write(0xAA); Wire.endTransmission(); delay(20);
    Wire.beginTransmission(0x70);
    Wire.write(0x00); Wire.write(0xA5); Wire.endTransmission(); delay(20);
    // Nouvelle adresse 0xE2 : écrire (0xE2 | 0x00) >> 1 = 0x71 en 7-bit
    Wire.beginTransmission(0x70);
    Wire.write(0x00); Wire.write(0xE2); Wire.endTransmission(); delay(20);
}
void loop() {}

   ═════════════════════════════════════════════════════════════════════ */

#include "lvgl.h"
#include <stdio.h>

/* Images LVGL 9 converties en C Array.
   Les fichiers .c doivent être placés dans src/images/.
   Ne pas faire #include "images/xxx.c" : PlatformIO les compile automatiquement. */
#ifndef LV_IMAGE_DECLARE
#define LV_IMAGE_DECLARE(var_name) extern const lv_image_dsc_t var_name
#endif

/* Image utilisée :
   - menu_bg : fond complet du menu principal

   IMPORTANT FLASH : garder uniquement menu_bg.c dans src/images/.
   Les logos PONG/DODGE ont été supprimés pour économiser la FLASH. */
LV_IMAGE_DECLARE(menu_bg);

#ifdef ARDUINO
#include "lvglDrivers.h"
#include <Wire.h>

/* ═══════════════════════════════════════════════════════════════════════
   DRIVER SRF08 — identique à l'original, paramétré par adresse
   ═════════════════════════════════════════════════════════════════════ */
typedef struct {
    uint16_t distance_cm;
    uint8_t  light;
    uint32_t response_ms;
    uint8_t  valid;
} SRF08_Result_t;

#define SRF08_ADDR_LEFT   0x70   /* 0xE0 >> 1 — capteur gauche (défaut) */
#define SRF08_ADDR_RIGHT  0x71   /* 0xE2 >> 1 — capteur droit (reprogrammé) */

static uint8_t SRF08_StartMeasure(uint8_t addr)
{
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    Wire.write(0x51);
    return (Wire.endTransmission() == 0);
}

static uint8_t SRF08_ReadResult(uint8_t addr, SRF08_Result_t *result, uint32_t t_start)
{
    /* Lire lumière + distance haute + distance basse.
       On ne relance pas une mesure ici : ça permet de démarrer les 2 capteurs
       presque en même temps, puis d'attendre une seule fois. Latence divisée. */
    Wire.beginTransmission(addr);
    Wire.write(0x01);
    if (Wire.endTransmission(false) != 0) {
        result->valid = 0;
        return 0;
    }

    Wire.requestFrom(addr, (uint8_t)3);
    if (Wire.available() < 3) {
        result->valid = 0;
        return 0;
    }

    result->light       = Wire.read();
    uint8_t high        = Wire.read();
    uint8_t low         = Wire.read();
    result->distance_cm = ((uint16_t)high << 8) | low;
    result->response_ms = millis() - t_start;
    result->valid       = 1;
    return 1;
}

static void SRF08_MeasurePair(SRF08_Result_t *left, SRF08_Result_t *right)
{
    uint32_t t_start = millis();

    uint8_t ok_left  = SRF08_StartMeasure(SRF08_ADDR_LEFT);
    uint8_t ok_right = SRF08_StartMeasure(SRF08_ADDR_RIGHT);

    /* SRF08 : mesure typique autour de 65 ms.
       Avant, le code faisait gauche 70 ms + droite 70 ms = grosse latence.
       Ici les 2 mesures sont lancées, puis on attend une seule fois. */
    delay(68);

    if (ok_left)  SRF08_ReadResult(SRF08_ADDR_LEFT, left, t_start);
    else          left->valid = 0;

    if (ok_right) SRF08_ReadResult(SRF08_ADDR_RIGHT, right, t_start);
    else          right->valid = 0;
}

/* Ancienne fonction gardée au cas où tu veux tester un capteur seul. */
static void SRF08_Measure(uint8_t addr, SRF08_Result_t *result)
{
    uint32_t t_start = millis();
    if (!SRF08_StartMeasure(addr)) { result->valid = 0; return; }
    delay(68);
    SRF08_ReadResult(addr, result, t_start);
}

/* ═══════════════════════════════════════════════════════════════════════
   FILTRE CAPTEURS — plus stable pour jouer avec les SRF08
   ═════════════════════════════════════════════════════════════════════

   Pourquoi : les ultrasons donnent parfois des sauts de quelques cm.
   Pour le jeu, ces sauts rendent la raquette nerveuse.

   Filtre utilisé :
   1) rejet des valeurs impossibles / hors zone utile ;
   2) médiane sur 3 mesures pour supprimer les pics isolés ;
   3) lissage EMA en entier : rapide mais stable ;
   4) limite de variation par cycle pour éviter les téléportations.
*/
#define SENSOR_MIN_VALID_CM   3
#define SENSOR_MAX_VALID_CM   120
#define FILTER_BUF_SIZE       3
#define FILTER_ALPHA_NUM      55   /* équilibre : réactif sans être nerveux */
#define FILTER_ALPHA_DEN      100
#define FILTER_MAX_STEP_CM     8   /* évite qu'une valeur parasite envoie la raquette en haut/bas */

typedef struct {
    uint16_t buf[FILTER_BUF_SIZE];
    uint8_t  idx;
    uint8_t  count;
    uint16_t filtered_cm;
    uint8_t  initialized;
} SensorFilter_t;

static SensorFilter_t filter_left  = {{0}, 0, 0, 20, 0};
static SensorFilter_t filter_right = {{0}, 0, 0, 20, 0};

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

static uint16_t sensor_filter_push(SensorFilter_t *f, uint16_t raw_cm)
{
    if (raw_cm < SENSOR_MIN_VALID_CM) raw_cm = SENSOR_MIN_VALID_CM;
    if (raw_cm > SENSOR_MAX_VALID_CM) raw_cm = SENSOR_MAX_VALID_CM;

    f->buf[f->idx] = raw_cm;
    f->idx = (f->idx + 1) % FILTER_BUF_SIZE;
    if (f->count < FILTER_BUF_SIZE) f->count++;

    uint16_t med = raw_cm;
    if (f->count >= 3) {
        med = median3(f->buf[0], f->buf[1], f->buf[2]);
    } else if (f->count == 2) {
        med = (uint16_t)((f->buf[0] + f->buf[1]) / 2);
    }

    if (!f->initialized) {
        f->filtered_cm = med;
        f->initialized = 1;
    } else {
        int diff = (int)med - (int)f->filtered_cm;
        if (diff > FILTER_MAX_STEP_CM) diff = FILTER_MAX_STEP_CM;
        if (diff < -FILTER_MAX_STEP_CM) diff = -FILTER_MAX_STEP_CM;

        int limited = (int)f->filtered_cm + diff;
        f->filtered_cm = (uint16_t)(((int)f->filtered_cm * (FILTER_ALPHA_DEN - FILTER_ALPHA_NUM) +
                                     limited * FILTER_ALPHA_NUM) / FILTER_ALPHA_DEN);
    }

    return f->filtered_cm;
}

static uint16_t get_filtered_cm(const SensorFilter_t *f)
{
    return f->filtered_cm;
}

/* ═══════════════════════════════════════════════════════════════════════
   MINI CONSOLE — MENU TACTILE + 2 JEUX AVEC 2 CAPTEURS SRF08
   ═════════════════════════════════════════════════════════════════════

   Jeu 1 : PONG ULTRASON
   - Capteur gauche -> raquette gauche
   - Capteur droit  -> raquette droite
   - Tactile        -> pause / reprise / retour menu

   Jeu 2 : DODGE ULTRASON
   - Le joueur est en bas de l'écran.
   - Main proche du capteur gauche -> déplacement vers la gauche
   - Main proche du capteur droit  -> déplacement vers la droite
   - Évite les obstacles qui tombent.
*/
#define SCREEN_W       480
#define SCREEN_H       272

#define GAME_MIN_CM      5
#define GAME_MAX_CM     35

#define PLAY_TOP        32
#define PLAY_BOTTOM    238

#define PADDLE_W        12
#define PADDLE_H        76   /* plus grand = plus jouable avec capteurs imprécis */
#define BALL_SIZE       11
#define LEFT_X          18
#define RIGHT_X        (SCREEN_W - LEFT_X - PADDLE_W)
#define WIN_SCORE        5
#define PONG_BALL_SPEED_START 4
#define PONG_BALL_SPEED_MAX    9

/* Réglage Pong séparé gauche/droite.
   Le capteur jaune/droit peut parfois lire trop court : on évite donc
   de mapper les valeurs extrêmes directement tout en haut. */
#define PONG_LEFT_MIN_CM       8
#define PONG_LEFT_MAX_CM      42
#define PONG_RIGHT_MIN_CM     10
#define PONG_RIGHT_MAX_CM     45
#define PONG_PADDLE_MARGIN     8
#define PONG_Y_MAX_STEP       14
#define PONG_Y_FOLLOW_NUM     55
#define PONG_Y_FOLLOW_DEN    100

#define COL_BG          0x080B12
#define COL_PANEL       0x141A24
#define COL_LINE        0x2B3444
#define COL_TEXT        0xFFFFFF
#define COL_MUTED       0x9AA4B2
#define COL_BLUE        0x00D4FF
#define COL_YELLOW      0xFFD700
#define COL_RED         0xFF4B5C
#define COL_GREEN       0x3FB950
#define COL_PURPLE      0xA970FF

#define DODGE_PLAYER_W   42
#define DODGE_PLAYER_H   18
#define DODGE_OBS_W      22
#define DODGE_OBS_H      18
#define DODGE_MAX_OBS     4

typedef enum {
    APP_MENU = 0,
    APP_PONG,
    APP_DODGE,
    APP_CALIBRATION,
    APP_INFO,
    APP_AUDIT
} AppMode_t;

typedef enum {
    GAME_WAIT_START = 0,
    GAME_RUNNING,
    GAME_PAUSED,
    GAME_OVER
} GameState_t;

static AppMode_t app_mode = APP_MENU;
static uint16_t g_left_cm = 20;
static uint16_t g_right_cm = 20;
static uint8_t  g_left_ok = 0;
static uint8_t  g_right_ok = 0;

/* Améliorations démo : calibrage, audit capteurs et meilleurs scores.
   Valeurs par défaut conservatrices, modifiables depuis l'écran CALIBRAGE. */
static uint16_t cal_left_near_cm  = 8;
static uint16_t cal_left_far_cm   = 42;
static uint16_t cal_right_near_cm = 10;
static uint16_t cal_right_far_cm  = 45;
static uint32_t best_dodge_score  = 0;
static uint8_t  best_pong_left    = 0;
static uint8_t  best_pong_right   = 0;

static lv_obj_t *audit_lbl_left = NULL;
static lv_obj_t *audit_lbl_right = NULL;
static lv_obj_t *audit_lbl_quality = NULL;

/* Objets communs */
static lv_obj_t *common_btn_menu = NULL;
static lv_obj_t *common_lbl_menu = NULL;
static lv_obj_t *common_lbl_left = NULL;
static lv_obj_t *common_lbl_right = NULL;

static int clamp_int(int v, int min_v, int max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static int map_distance_to_y(uint16_t cm, int obj_h)
{
    int d = clamp_int((int)cm, GAME_MIN_CM, GAME_MAX_CM);
    int usable_h = (PLAY_BOTTOM - PLAY_TOP) - obj_h;
    return PLAY_TOP + ((d - GAME_MIN_CM) * usable_h) / (GAME_MAX_CM - GAME_MIN_CM);
}

static int map_pong_distance_to_y(uint16_t cm, int obj_h, uint8_t right_side)
{
    int min_cm = right_side ? (int)cal_right_near_cm : (int)cal_left_near_cm;
    int max_cm = right_side ? (int)cal_right_far_cm  : (int)cal_left_far_cm;

    /* Sécurité : évite une division par zéro si le calibrage est raté. */
    if (max_cm <= min_cm + 5) {
        min_cm = right_side ? PONG_RIGHT_MIN_CM : PONG_LEFT_MIN_CM;
        max_cm = right_side ? PONG_RIGHT_MAX_CM : PONG_LEFT_MAX_CM;
    }

    int d = clamp_int((int)cm, min_cm, max_cm);

    int top = PLAY_TOP + PONG_PADDLE_MARGIN;
    int bottom = PLAY_BOTTOM - PONG_PADDLE_MARGIN - obj_h;
    int usable_h = bottom - top;

    return top + ((d - min_cm) * usable_h) / (max_cm - min_cm);
}

static int smooth_paddle_y(int current_y, int target_y)
{
    int diff = target_y - current_y;

    /* Petit bruit capteur : on ne bouge pas. */
    if (diff > -3 && diff < 3) return current_y;

    /* Suivi progressif : assez rapide, mais pas de téléportation. */
    int step = (diff * PONG_Y_FOLLOW_NUM) / PONG_Y_FOLLOW_DEN;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    step = clamp_int(step, -PONG_Y_MAX_STEP, PONG_Y_MAX_STEP);

    return current_y + step;
}

static void clear_screen(void)
{
    lv_obj_clean(lv_screen_active());
    common_btn_menu = NULL;
    common_lbl_menu = NULL;
    common_lbl_left = NULL;
    common_lbl_right = NULL;
}

/* Affiche une image plein écran en fond.
   Compatible LVGL 9 + images converties en C Array. */
static lv_obj_t *add_image_background(const void *src)
{
    lv_obj_t *bg = lv_image_create(lv_screen_active());
    lv_image_set_src(bg, src);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(bg);
    return bg;
}

static void style_panel(lv_obj_t *obj, uint32_t bg, uint32_t border, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
}

static void style_plain_obj(lv_obj_t *obj, uint32_t color, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

/* ═══════════════════════════════════════════════════════════════════════
   DESIGN SYSTEM — interface moderne sans images externes
   ═════════════════════════════════════════════════════════════════════
   Pour une démo fiable sur STM32, on évite les gros PNG/JPG en mémoire.
   Les "images" du menu et des jeux sont donc dessinées en LVGL :
   cartes, néons, mini-vaisseau, météores, terrain, étoiles, etc.
*/
static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t bg, uint32_t border, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, border ? 1 : 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, uint32_t color,
                            lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static void add_star_field(lv_obj_t *scr)
{
    static const uint16_t pts[][2] = {
        {14,18},{42,72},{75,33},{116,92},{150,20},{194,70},{238,15},{279,88},
        {318,42},{362,18},{404,76},{454,31},{22,210},{68,246},{116,194},{172,230},
        {226,205},{282,249},{338,198},{398,235},{455,208},{260,132},{30,132},{440,146}
    };
    for (uint8_t i = 0; i < sizeof(pts)/sizeof(pts[0]); i++) {
        lv_obj_t *st = lv_obj_create(scr);
        lv_obj_set_size(st, (i % 3) + 2, (i % 3) + 2);
        lv_obj_set_pos(st, pts[i][0], pts[i][1]);
        lv_obj_set_style_bg_color(st, lv_color_hex((i % 2) ? 0x335CFF : 0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(st, (i % 2) ? LV_OPA_40 : LV_OPA_70, 0);
        lv_obj_set_style_border_width(st, 0, 0);
        lv_obj_set_style_radius(st, 3, 0);
    }
}

static void add_neon_frame(lv_obj_t *scr, int top, int bottom)
{
    lv_obj_t *frame = make_box(scr, 4, top, SCREEN_W - 8, bottom - top, 0x07111F, COL_LINE, 10);
    lv_obj_set_style_bg_opa(frame, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(frame, 14, 0);
    lv_obj_set_style_shadow_opa(frame, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(frame, lv_color_hex(COL_BLUE), 0);

    for (int x = 30; x < SCREEN_W - 20; x += 38) {
        lv_obj_t *dash = make_box(scr, x, top + 6, 12, 2, COL_BLUE, 0, 1);
        lv_obj_set_style_bg_opa(dash, LV_OPA_50, 0);
    }
    for (int y = top + 28; y < bottom - 8; y += 28) {
        lv_obj_t *l = make_box(scr, SCREEN_W / 2 - 1, y, 2, 12, COL_LINE, 0, 1);
        lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
    }
}

static void add_pong_card_art(lv_obj_t *parent)
{
    /* Logos supprimés : dessin léger en LVGL. */
    make_label(parent, "PONG", COL_TEXT, LV_ALIGN_TOP_MID, 0, 18);
    make_label(parent, "Duel ultrason", COL_MUTED, LV_ALIGN_TOP_MID, 0, 48);
    make_label(parent, "TOUCHER", COL_BLUE, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void add_dodge_card_art(lv_obj_t *parent)
{
    /* Logos supprimés : dessin léger en LVGL. */
    make_label(parent, "DODGE", COL_TEXT, LV_ALIGN_TOP_MID, 0, 18);
    make_label(parent, "Esquive reflexe", COL_MUTED, LV_ALIGN_TOP_MID, 0, 48);
    make_label(parent, "TOUCHER", COL_PURPLE, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void add_touch_hint(lv_obj_t *scr, const char *txt)
{
    lv_obj_t *hint = make_box(scr, 92, 236, 296, 26, 0x111827, COL_LINE, 13);
    lv_obj_set_style_bg_opa(hint, LV_OPA_80, 0);
    make_label(hint, txt, COL_MUTED, LV_ALIGN_CENTER, 0, 0);
}

/* Objets 2D rétro : beaux mais légers pour la STM32.
   On évite les ombres et les effets dynamiques sur les objets mobiles. */
static void make_child_block(lv_obj_t *parent, int x, int y, int w, int h,
                             uint32_t color, int radius)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *create_pixel_paddle(lv_obj_t *parent, uint32_t main_col, uint32_t edge_col)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, PADDLE_W + 8, PADDLE_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    /* Contour + segments style borne d'arcade. */
    make_child_block(p, 0, 0, PADDLE_W + 8, PADDLE_H, edge_col, 4);
    make_child_block(p, 2, 3, PADDLE_W + 4, PADDLE_H - 6, main_col, 3);
    make_child_block(p, 4, 10, PADDLE_W, 10, 0xFFFFFF, 2);
    make_child_block(p, 4, PADDLE_H / 2 - 5, PADDLE_W, 10, 0xFFFFFF, 2);
    make_child_block(p, 4, PADDLE_H - 20, PADDLE_W, 10, 0xFFFFFF, 2);
    return p;
}

static lv_obj_t *create_pixel_ship(lv_obj_t *parent)
{
    lv_obj_t *ship = lv_obj_create(parent);
    lv_obj_set_size(ship, DODGE_PLAYER_W, DODGE_PLAYER_H + 8);
    lv_obj_set_style_bg_opa(ship, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ship, 0, 0);
    lv_obj_set_style_pad_all(ship, 0, 0);
    lv_obj_clear_flag(ship, LV_OBJ_FLAG_SCROLLABLE);

    int mid = DODGE_PLAYER_W / 2;
    make_child_block(ship, mid - 4, 0, 8, 8, COL_TEXT, 2);
    make_child_block(ship, mid - 12, 7, 24, 8, COL_BLUE, 3);
    make_child_block(ship, mid - 20, 15, 40, 9, COL_BLUE, 4);
    make_child_block(ship, mid - 8, 18, 16, 7, 0xFFFFFF, 2);
    make_child_block(ship, mid - 18, 24, 10, 4, COL_YELLOW, 2);
    make_child_block(ship, mid + 8, 24, 10, 4, COL_YELLOW, 2);
    return ship;
}

static lv_obj_t *create_pixel_obstacle(lv_obj_t *parent, uint32_t col)
{
    lv_obj_t *obs = lv_obj_create(parent);
    lv_obj_set_size(obs, DODGE_OBS_W + 8, DODGE_OBS_H + 8);
    lv_obj_set_style_bg_opa(obs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obs, 0, 0);
    lv_obj_set_style_pad_all(obs, 0, 0);
    lv_obj_clear_flag(obs, LV_OBJ_FLAG_SCROLLABLE);

    make_child_block(obs, 8, 0, 14, 6, col, 2);
    make_child_block(obs, 2, 6, 28, 8, col, 2);
    make_child_block(obs, 6, 14, 22, 8, col, 2);
    make_child_block(obs, 12, 20, 10, 5, 0xFFFFFF, 2);
    return obs;
}


static void update_common_distance_labels(void)
{
    char buf[48];

    if (!common_lbl_left || !common_lbl_right) return;

    if (g_left_ok) snprintf(buf, sizeof(buf), "G:%dcm", g_left_cm);
    else           snprintf(buf, sizeof(buf), "G:KO");
    lv_label_set_text(common_lbl_left, buf);

    if (g_right_ok) snprintf(buf, sizeof(buf), "D:%dcm", g_right_cm);
    else            snprintf(buf, sizeof(buf), "D:KO");
    lv_label_set_text(common_lbl_right, buf);
}

static void build_common_top_bar(const char *title_txt)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, title_txt);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 7);

    common_btn_menu = lv_button_create(scr);   /* Si erreur LVGL : remplacer par lv_btn_create */
    lv_obj_set_size(common_btn_menu, 72, 26);
    style_panel(common_btn_menu, COL_PANEL, COL_LINE, 8);
    lv_obj_align(common_btn_menu, LV_ALIGN_TOP_LEFT, 5, 3);

    common_lbl_menu = lv_label_create(common_btn_menu);
    lv_label_set_text(common_lbl_menu, "MENU");
    lv_obj_center(common_lbl_menu);

    common_lbl_left = lv_label_create(scr);
    lv_obj_set_style_text_color(common_lbl_left, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(common_lbl_left, LV_ALIGN_TOP_LEFT, 84, 8);

    common_lbl_right = lv_label_create(scr);
    lv_obj_set_style_text_color(common_lbl_right, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(common_lbl_right, LV_ALIGN_TOP_RIGHT, -8, 8);

    update_common_distance_labels();
}

/* Forward declarations */
static void build_menu(void);
static void build_pong(void);
static void build_dodge(void);
static void build_calibration(void);
static void build_info(void);
static void build_audit(void);
static void menu_pong_cb(lv_event_t *e);
static void menu_dodge_cb(lv_event_t *e);
static void menu_calibration_cb(lv_event_t *e);
static void menu_info_cb(lv_event_t *e);
static void menu_audit_cb(lv_event_t *e);
static void menu_btn_event_cb(lv_event_t *e);
static void restart_current_game_cb(lv_event_t *e);

static void show_result_screen(uint8_t victory, const char *restart_txt)
{
    lv_obj_t *scr = lv_screen_active();

    /* Écran résultat léger : pas d'image plein écran, donc pas d'explosion de FLASH. */
    lv_obj_t *overlay = make_box(scr, 0, 0, SCREEN_W, SCREEN_H, victory ? 0x041B2D : 0x2A0508, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
    lv_obj_move_foreground(overlay);

    for (int i = 0; i < 18; i++) {
        lv_obj_t *spark = make_box(scr, 20 + (i * 27) % 430, 35 + (i * 41) % 145,
                                   8 + (i % 4) * 3, 3,
                                   victory ? ((i % 2) ? COL_YELLOW : COL_BLUE) : COL_RED, 0, 2);
        lv_obj_move_foreground(spark);
    }

    lv_obj_t *card = make_box(scr, 42, 54, SCREEN_W - 84, 130,
                              victory ? 0x081D2B : 0x1B0709,
                              victory ? COL_BLUE : COL_RED, 18);
    lv_obj_set_style_shadow_width(card, 22, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(victory ? COL_BLUE : COL_RED), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_move_foreground(card);

    make_label(card, victory ? "VICTOIRE !" : "GAME OVER",
               victory ? COL_TEXT : 0xFFBBBB, LV_ALIGN_TOP_MID, 0, 22);
    make_label(card, victory ? "Bravo, belle partie" : "Obstacle touche",
               COL_MUTED, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *btn_restart = lv_button_create(scr);
    lv_obj_set_size(btn_restart, 132, 34);
    style_panel(btn_restart, COL_PANEL, COL_GREEN, 10);
    lv_obj_align(btn_restart, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_restart, restart_current_game_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn_restart);

    lv_obj_t *lbl_restart = lv_label_create(btn_restart);
    lv_label_set_text(lbl_restart, restart_txt);
    lv_obj_center(lbl_restart);

    lv_obj_t *btn_menu = lv_button_create(scr);
    lv_obj_set_size(btn_menu, 78, 28);
    style_panel(btn_menu, COL_PANEL, COL_LINE, 8);
    lv_obj_align(btn_menu, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn_menu);

    lv_obj_t *lbl_menu = lv_label_create(btn_menu);
    lv_label_set_text(lbl_menu, "MENU");
    lv_obj_center(lbl_menu);
}


/* Petit bouton glassmorphism pour le menu. */
static lv_obj_t *make_menu_small_button(lv_obj_t *scr, const char *txt, int x, int y, int w)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, w, 24);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x071427), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x6AA8FF), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(lbl);
    return btn;
}

static void build_menu(void)
{
    app_mode = APP_MENU;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);

    /* Fond menu moderne : l'image contient déjà les cartes et le décor. */
    lv_obj_t *menu_background = lv_image_create(scr);
    lv_image_set_src(menu_background, &menu_bg);
    lv_obj_set_pos(menu_background, 0, 0);
    lv_obj_clear_flag(menu_background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(menu_background);

    /* Zones tactiles transparentes sur les cartes du fond. */
    lv_obj_t *btn_pong = lv_button_create(scr);
    lv_obj_set_size(btn_pong, 172, 116);
    lv_obj_set_pos(btn_pong, 24, 112);
    lv_obj_set_style_bg_opa(btn_pong, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_pong, 0, 0);
    lv_obj_set_style_shadow_width(btn_pong, 0, 0);
    lv_obj_set_style_radius(btn_pong, 20, 0);
    lv_obj_add_event_cb(btn_pong, menu_pong_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_dodge = lv_button_create(scr);
    lv_obj_set_size(btn_dodge, 172, 116);
    lv_obj_set_pos(btn_dodge, 220, 112);
    lv_obj_set_style_bg_opa(btn_dodge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_dodge, 0, 0);
    lv_obj_set_style_shadow_width(btn_dodge, 0, 0);
    lv_obj_set_style_radius(btn_dodge, 20, 0);
    lv_obj_add_event_cb(btn_dodge, menu_dodge_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(btn_pong, lv_color_hex(COL_BLUE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_pong, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_dodge, lv_color_hex(COL_PURPLE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_dodge, LV_OPA_20, LV_STATE_PRESSED);

    /* Meilleurs scores : léger et utile pour la démo. */
    char buf[64];
    snprintf(buf, sizeof(buf), "Best Pong G:%d  D:%d     Best Dodge:%lu", best_pong_left, best_pong_right, best_dodge_score);
    lv_obj_t *best = lv_label_create(scr);
    lv_label_set_text(best, buf);
    lv_obj_set_style_text_color(best, lv_color_hex(0xDDEBFF), 0);
    lv_obj_align(best, LV_ALIGN_TOP_MID, 0, 42);

    /* Outils de soutenance : calibrage, audit capteurs, infos projet. */
    lv_obj_t *btn_cal = make_menu_small_button(scr, "CALIBRAGE", 22, 238, 108);
    lv_obj_add_event_cb(btn_cal, menu_calibration_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_audit = make_menu_small_button(scr, "AUDIT", 144, 238, 84);
    lv_obj_add_event_cb(btn_audit, menu_audit_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_info = make_menu_small_button(scr, "INFOS", 242, 238, 84);
    lv_obj_add_event_cb(btn_info, menu_info_cb, LV_EVENT_CLICKED, NULL);
}



/* ═══════════════════════════════════════════════════════════════════════
   ECRANS OUTILS — CALIBRAGE / AUDIT / INFOS
   ═════════════════════════════════════════════════════════════════════ */
static void calibration_near_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_left_ok)  cal_left_near_cm = g_left_cm;
    if (g_right_ok) cal_right_near_cm = g_right_cm;
    build_calibration();
}

static void calibration_far_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_left_ok)  cal_left_far_cm = g_left_cm;
    if (g_right_ok) cal_right_far_cm = g_right_cm;
    build_calibration();
}

static void calibration_reset_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    cal_left_near_cm  = PONG_LEFT_MIN_CM;
    cal_left_far_cm   = PONG_LEFT_MAX_CM;
    cal_right_near_cm = PONG_RIGHT_MIN_CM;
    cal_right_far_cm  = PONG_RIGHT_MAX_CM;
    build_calibration();
}

static void build_calibration(void)
{
    app_mode = APP_CALIBRATION;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    add_star_field(scr);
    build_common_top_bar("CALIBRAGE CAPTEURS");
    lv_obj_add_event_cb(common_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = make_box(scr, 24, 48, SCREEN_W - 48, 150, 0x101828, COL_BLUE, 16);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);

    char buf[160];
    snprintf(buf, sizeof(buf),
             "Valeurs actuelles\\nGauche : %d cm   Droite : %d cm\\n\\nCalibrage Pong\\nG near:%d  G far:%d\\nD near:%d  D far:%d",
             g_left_cm, g_right_cm, cal_left_near_cm, cal_left_far_cm, cal_right_near_cm, cal_right_far_cm);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 16, 14);

    lv_obj_t *btn_near = make_menu_small_button(scr, "MAIN PROCHE", 34, 212, 124);
    lv_obj_add_event_cb(btn_near, calibration_near_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_far = make_menu_small_button(scr, "MAIN LOIN", 178, 212, 112);
    lv_obj_add_event_cb(btn_far, calibration_far_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_reset = make_menu_small_button(scr, "RESET", 310, 212, 76);
    lv_obj_add_event_cb(btn_reset, calibration_reset_cb, LV_EVENT_CLICKED, NULL);
}

static void audit_update_screen(void)
{
    if (!audit_lbl_left || !audit_lbl_right || !audit_lbl_quality) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "Capteur gauche : %s  %d cm", g_left_ok ? "OK" : "KO", g_left_cm);
    lv_label_set_text(audit_lbl_left, buf);

    snprintf(buf, sizeof(buf), "Capteur droit  : %s  %d cm", g_right_ok ? "OK" : "KO", g_right_cm);
    lv_label_set_text(audit_lbl_right, buf);

    const char *quality = "BON";
    uint32_t col = COL_GREEN;
    if (!g_left_ok || !g_right_ok) {
        quality = "PROBLEME I2C / MESURE";
        col = COL_RED;
    } else if (g_left_cm < 4 || g_right_cm < 4 || g_left_cm > 110 || g_right_cm > 110) {
        quality = "HORS ZONE";
        col = COL_YELLOW;
    }

    lv_label_set_text(audit_lbl_quality, quality);
    lv_obj_set_style_text_color(audit_lbl_quality, lv_color_hex(col), 0);
}

static void build_audit(void)
{
    app_mode = APP_AUDIT;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    add_star_field(scr);
    build_common_top_bar("AUDIT CAPTEURS");
    lv_obj_add_event_cb(common_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = make_box(scr, 40, 58, SCREEN_W - 80, 136, 0x101828, COL_GREEN, 16);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);

    audit_lbl_left = lv_label_create(card);
    lv_obj_set_style_text_color(audit_lbl_left, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(audit_lbl_left, LV_ALIGN_TOP_LEFT, 18, 18);

    audit_lbl_right = lv_label_create(card);
    lv_obj_set_style_text_color(audit_lbl_right, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(audit_lbl_right, LV_ALIGN_TOP_LEFT, 18, 50);

    audit_lbl_quality = lv_label_create(card);
    lv_label_set_text(audit_lbl_quality, "---");
    lv_obj_align(audit_lbl_quality, LV_ALIGN_TOP_LEFT, 18, 90);

    make_label(scr, "Cet ecran sert a verifier les mesures en direct pendant la soutenance.", COL_MUTED, LV_ALIGN_BOTTOM_MID, 0, -34);

    audit_update_screen();
}

static void build_info(void)
{
    app_mode = APP_INFO;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    add_star_field(scr);
    build_common_top_bar("INFOS PROJET");
    lv_obj_add_event_cb(common_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = make_box(scr, 26, 48, SCREEN_W - 52, 168, 0x101828, COL_PURPLE, 16);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl,
        "Projet : mini-console STM32\\n"
        "Carte : STM32F746G-DISCO\\n"
        "Affichage : LVGL + ecran tactile\\n"
        "Capteurs : 2 x SRF08 sur bus I2C\\n"
        "Adresses : gauche 0x70 / droite 0x71\\n"
        "Jeux : Pong Ultrason + Dodge Ultrason\\n"
        "Optimisations : filtrage, calibrage, mesure paire, menu moderne");
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 18, 16);
}

/* ═══════════════════════════════════════════════════════════════════════
   JEU 1 — PONG
   ═════════════════════════════════════════════════════════════════════ */
typedef struct {
    lv_obj_t *paddle_left;
    lv_obj_t *paddle_right;
    lv_obj_t *ball;
    lv_obj_t *lbl_score;
    lv_obj_t *lbl_info;
    lv_obj_t *btn_start;
    lv_obj_t *lbl_btn;

    int left_y;
    int right_y;
    int ball_x;
    int ball_y;
    int ball_vx;
    int ball_vy;
    uint8_t score_left;
    uint8_t score_right;
    GameState_t state;
} PongGame_t;

static PongGame_t pong;

static void pong_set_message(const char *txt)
{
    lv_label_set_text(pong.lbl_info, txt);
}

static void pong_update_score_label(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d  -  %d", pong.score_left, pong.score_right);
    lv_label_set_text(pong.lbl_score, buf);
}

static void pong_reset_ball(int direction)
{
    pong.ball_x = (SCREEN_W - BALL_SIZE) / 2;
    pong.ball_y = PLAY_TOP + ((PLAY_BOTTOM - PLAY_TOP) - BALL_SIZE) / 2;
    pong.ball_vx = (direction >= 0) ? PONG_BALL_SPEED_START : -PONG_BALL_SPEED_START;
    pong.ball_vy = 2;
    lv_obj_set_pos(pong.ball, pong.ball_x, pong.ball_y);
}

static void pong_reset_full(void)
{
    pong.score_left = 0;
    pong.score_right = 0;
    pong.left_y = PLAY_TOP + 70;
    pong.right_y = PLAY_TOP + 70;
    lv_obj_set_pos(pong.paddle_left, LEFT_X, pong.left_y);
    lv_obj_set_pos(pong.paddle_right, RIGHT_X, pong.right_y);
    pong_update_score_label();
    pong_reset_ball(1);
    pong.state = GAME_WAIT_START;
    pong_set_message("START — Pong Neon: raquettes stables, balle progressive");
    lv_label_set_text(pong.lbl_btn, "START");
}

static void pong_start_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (pong.state == GAME_RUNNING) {
        pong.state = GAME_PAUSED;
        pong_set_message("PAUSE — touchez REPRENDRE");
        lv_label_set_text(pong.lbl_btn, "REPRENDRE");
    } else if (pong.state == GAME_OVER) {
        pong_reset_full();
        pong.state = GAME_RUNNING;
        pong_set_message("Jeu lance !");
        lv_label_set_text(pong.lbl_btn, "PAUSE");
    } else {
        pong.state = GAME_RUNNING;
        pong_set_message("Jeu lance !");
        lv_label_set_text(pong.lbl_btn, "PAUSE");
    }
}

static void build_pong(void)
{
    app_mode = APP_PONG;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    /* Fond Pong dessiné en LVGL pour économiser la FLASH. */
    build_common_top_bar("PONG ULTRASON");
    lv_obj_add_event_cb(common_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

    pong.lbl_score = lv_label_create(scr);
    lv_obj_set_style_text_color(pong.lbl_score, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(pong.lbl_score, LV_ALIGN_TOP_MID, 0, 28);

    add_star_field(scr);
    add_neon_frame(scr, PLAY_TOP - 2, PLAY_BOTTOM + 2);

    /* Décor façon arène : zones lumineuses derrière les raquettes */
    lv_obj_t *glow_l = make_box(scr, 6, PLAY_TOP + 18, 6, PLAY_BOTTOM - PLAY_TOP - 36, COL_BLUE, 0, 3);
    lv_obj_set_style_bg_opa(glow_l, LV_OPA_50, 0);
    lv_obj_t *glow_r = make_box(scr, SCREEN_W - 12, PLAY_TOP + 18, 6, PLAY_BOTTOM - PLAY_TOP - 36, COL_YELLOW, 0, 3);
    lv_obj_set_style_bg_opa(glow_r, LV_OPA_50, 0);

    /* Raquettes pixel-art : rendu rétro propre, beaucoup plus fluide que des ombres/glow. */
    pong.paddle_left = create_pixel_paddle(scr, COL_BLUE, 0x004C66);
    pong.paddle_right = create_pixel_paddle(scr, COL_YELLOW, 0x6A5200);

    pong.ball = lv_obj_create(scr);
    lv_obj_set_size(pong.ball, BALL_SIZE + 2, BALL_SIZE + 2);
    style_plain_obj(pong.ball, COL_TEXT, LV_RADIUS_CIRCLE);
    lv_obj_clear_flag(pong.ball, LV_OBJ_FLAG_SCROLLABLE);

    make_label(scr, "P1", COL_BLUE, LV_ALIGN_LEFT_MID, 20, -96);
    make_label(scr, "P2", COL_YELLOW, LV_ALIGN_RIGHT_MID, -20, -96);

    pong.lbl_info = lv_label_create(scr);
    lv_obj_set_style_text_color(pong.lbl_info, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(pong.lbl_info, LV_ALIGN_BOTTOM_MID, 0, -34);

    pong.btn_start = lv_button_create(scr);
    lv_obj_set_size(pong.btn_start, 118, 32);
    style_panel(pong.btn_start, COL_PANEL, COL_GREEN, 10);
    lv_obj_align(pong.btn_start, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(pong.btn_start, pong_start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    pong.lbl_btn = lv_label_create(pong.btn_start);
    lv_label_set_text(pong.lbl_btn, "START");
    lv_obj_center(pong.lbl_btn);

    pong_reset_full();
}

static void pong_update_from_sensors(void)
{
    /* Équilibre Pong : on filtre déjà la distance, puis on lisse aussi le mouvement.
       Ça corrige la raquette jaune qui partait tout en haut sur une valeur trop courte. */
    if (g_left_ok) {
        int target = map_pong_distance_to_y(g_left_cm, PADDLE_H, 0);
        pong.left_y = smooth_paddle_y(pong.left_y, target);
        lv_obj_set_pos(pong.paddle_left, LEFT_X, pong.left_y);
    }

    if (g_right_ok) {
        int target = map_pong_distance_to_y(g_right_cm, PADDLE_H, 1);
        pong.right_y = smooth_paddle_y(pong.right_y, target);
        lv_obj_set_pos(pong.paddle_right, RIGHT_X, pong.right_y);
    }
}

static void pong_step(void)
{
    if (pong.state != GAME_RUNNING) return;

    pong.ball_x += pong.ball_vx;
    pong.ball_y += pong.ball_vy;

    if (pong.ball_y <= PLAY_TOP) {
        pong.ball_y = PLAY_TOP;
        pong.ball_vy = -pong.ball_vy;
    }
    if (pong.ball_y >= PLAY_BOTTOM - BALL_SIZE) {
        pong.ball_y = PLAY_BOTTOM - BALL_SIZE;
        pong.ball_vy = -pong.ball_vy;
    }

    if (pong.ball_vx < 0 &&
        pong.ball_x <= LEFT_X + PADDLE_W &&
        pong.ball_x + BALL_SIZE >= LEFT_X &&
        pong.ball_y + BALL_SIZE >= pong.left_y &&
        pong.ball_y <= pong.left_y + PADDLE_H) {
        pong.ball_x = LEFT_X + PADDLE_W;
        pong.ball_vx = clamp_int(-pong.ball_vx + 1, -PONG_BALL_SPEED_MAX, PONG_BALL_SPEED_MAX);
        int hit = (pong.ball_y + BALL_SIZE / 2) - (pong.left_y + PADDLE_H / 2);
        pong.ball_vy = clamp_int(hit / 10, -6, 6);
        if (pong.ball_vy == 0) pong.ball_vy = 2;
    }

    if (pong.ball_vx > 0 &&
        pong.ball_x + BALL_SIZE >= RIGHT_X &&
        pong.ball_x <= RIGHT_X + PADDLE_W &&
        pong.ball_y + BALL_SIZE >= pong.right_y &&
        pong.ball_y <= pong.right_y + PADDLE_H) {
        pong.ball_x = RIGHT_X - BALL_SIZE;
        pong.ball_vx = clamp_int(-pong.ball_vx - 1, -PONG_BALL_SPEED_MAX, PONG_BALL_SPEED_MAX);
        int hit = (pong.ball_y + BALL_SIZE / 2) - (pong.right_y + PADDLE_H / 2);
        pong.ball_vy = clamp_int(hit / 10, -6, 6);
        if (pong.ball_vy == 0) pong.ball_vy = -2;
    }

    if (pong.ball_x < 0) {
        pong.score_right++;
        pong_update_score_label();
        pong_reset_ball(-1);
    }
    if (pong.ball_x > SCREEN_W - BALL_SIZE) {
        pong.score_left++;
        pong_update_score_label();
        pong_reset_ball(1);
    }

    if (pong.score_left >= WIN_SCORE || pong.score_right >= WIN_SCORE) {
        pong.state = GAME_OVER;
        if (pong.score_left > pong.score_right) {
            pong_set_message("FIN — joueur GAUCHE gagne !");
            if (best_pong_left < 255) best_pong_left++;
        } else {
            pong_set_message("FIN — joueur DROIT gagne !");
            if (best_pong_right < 255) best_pong_right++;
        }
        lv_label_set_text(pong.lbl_btn, "RESTART");
        show_result_screen(1, "REJOUER");
        return;
    }

    lv_obj_set_pos(pong.ball, pong.ball_x, pong.ball_y);
}

/* ═══════════════════════════════════════════════════════════════════════
   JEU 2 — DODGE
   ═════════════════════════════════════════════════════════════════════ */
typedef struct {
    lv_obj_t *player;
    lv_obj_t *obstacles[DODGE_MAX_OBS];
    lv_obj_t *lbl_score;
    lv_obj_t *lbl_info;
    lv_obj_t *btn_start;
    lv_obj_t *lbl_btn;

    int player_x;
    int player_y;
    int obs_x[DODGE_MAX_OBS];
    int obs_y[DODGE_MAX_OBS];
    int obs_speed[DODGE_MAX_OBS];
    int player_vx;
    uint32_t score;
    uint32_t tick;
    GameState_t state;
} DodgeGame_t;

static DodgeGame_t dodge;

static uint32_t rng_state = 1234567;

static uint32_t game_rand(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

static void dodge_update_score_label(void)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "Score : %lu", dodge.score);
    lv_label_set_text(dodge.lbl_score, buf);
}

static void dodge_set_message(const char *txt)
{
    lv_label_set_text(dodge.lbl_info, txt);
}

static void dodge_reset_obstacle(uint8_t i, int y_start)
{
    dodge.obs_x[i] = 20 + (int)(game_rand() % (SCREEN_W - 40 - DODGE_OBS_W));
    dodge.obs_y[i] = y_start;
    dodge.obs_speed[i] = 3 + (int)(game_rand() % 3);
    lv_obj_set_pos(dodge.obstacles[i], dodge.obs_x[i], dodge.obs_y[i]);
}

static void dodge_reset_full(void)
{
    dodge.player_x = (SCREEN_W - DODGE_PLAYER_W) / 2;
    dodge.player_y = PLAY_BOTTOM - DODGE_PLAYER_H - 2;
    dodge.player_vx = 0;
    dodge.score = 0;
    dodge.tick = 0;
    lv_obj_set_pos(dodge.player, dodge.player_x, dodge.player_y);

    for (uint8_t i = 0; i < DODGE_MAX_OBS; i++) {
        dodge_reset_obstacle(i, PLAY_TOP - 40 - i * 55);
    }

    dodge_update_score_label();
    dodge.state = GAME_WAIT_START;
    dodge_set_message("START — pilote le vaisseau avec les 2 capteurs");
    lv_label_set_text(dodge.lbl_btn, "START");
}

static void dodge_start_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (dodge.state == GAME_RUNNING) {
        dodge.state = GAME_PAUSED;
        dodge_set_message("PAUSE — touchez REPRENDRE");
        lv_label_set_text(dodge.lbl_btn, "REPRENDRE");
    } else if (dodge.state == GAME_OVER) {
        dodge_reset_full();
        dodge.state = GAME_RUNNING;
        dodge_set_message("Esquive les obstacles !");
        lv_label_set_text(dodge.lbl_btn, "PAUSE");
    } else {
        dodge.state = GAME_RUNNING;
        dodge_set_message("Esquive les obstacles !");
        lv_label_set_text(dodge.lbl_btn, "PAUSE");
    }
}

static void build_dodge(void)
{
    app_mode = APP_DODGE;
    clear_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    /* Fond Dodge dessiné en LVGL pour économiser la FLASH. */
    build_common_top_bar("DODGE ULTRASON");
    lv_obj_add_event_cb(common_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

    add_star_field(scr);
    add_neon_frame(scr, PLAY_TOP - 2, PLAY_BOTTOM + 2);

    /* Couloirs de Dodge : ça donne un vrai look de jeu d'arcade. */
    for (int x = 80; x < SCREEN_W; x += 80) {
        lv_obj_t *lane = make_box(scr, x, PLAY_TOP + 4, 1, PLAY_BOTTOM - PLAY_TOP - 8, COL_LINE, 0, 0);
        lv_obj_set_style_bg_opa(lane, LV_OPA_40, 0);
    }

    dodge.lbl_score = lv_label_create(scr);
    lv_obj_set_style_text_color(dodge.lbl_score, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(dodge.lbl_score, LV_ALIGN_TOP_MID, 0, 28);

    /* Vaisseau et obstacles pixel-art : pas d'ombres sur les objets mobiles = meilleur FPS. */
    dodge.player = create_pixel_ship(scr);

    for (uint8_t i = 0; i < DODGE_MAX_OBS; i++) {
        dodge.obstacles[i] = create_pixel_obstacle(scr, (i % 2) ? COL_YELLOW : COL_RED);
    }

    dodge.lbl_info = lv_label_create(scr);
    lv_obj_set_style_text_color(dodge.lbl_info, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(dodge.lbl_info, LV_ALIGN_BOTTOM_MID, 0, -34);

    dodge.btn_start = lv_button_create(scr);
    lv_obj_set_size(dodge.btn_start, 118, 32);
    style_panel(dodge.btn_start, COL_PANEL, COL_GREEN, 10);
    lv_obj_align(dodge.btn_start, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(dodge.btn_start, dodge_start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    dodge.lbl_btn = lv_label_create(dodge.btn_start);
    lv_label_set_text(dodge.lbl_btn, "START");
    lv_obj_center(dodge.lbl_btn);

    dodge_reset_full();
}

static void dodge_update_from_sensors(void)
{
    int target_vx = 0;

    /* Contrôle plus naturel : on compare les deux distances filtrées.
       Plus la différence est grande, plus le déplacement est rapide. */
    if (g_left_ok && g_right_ok) {
        int diff = (int)g_left_cm - (int)g_right_cm;
        if (diff > 5)       target_vx = clamp_int(diff / 2, 3, 10);
        else if (diff < -5) target_vx = clamp_int(diff / 2, -10, -3);
        else                target_vx = 0;
    } else if (g_left_ok && g_left_cm < 18) {
        target_vx = -7;
    } else if (g_right_ok && g_right_cm < 18) {
        target_vx = 7;
    }

    /* Inertie légère pour ne pas trembler à chaque mesure. */
    dodge.player_vx = (dodge.player_vx * 25 + target_vx * 75) / 100;
    if (target_vx == 0 && dodge.player_vx > -1 && dodge.player_vx < 1) dodge.player_vx = 0;

    dodge.player_x += dodge.player_vx;
    dodge.player_x = clamp_int(dodge.player_x, 8, SCREEN_W - DODGE_PLAYER_W - 8);
    lv_obj_set_pos(dodge.player, dodge.player_x, dodge.player_y);
}

static uint8_t rects_collide(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return (ax < bx + bw &&
            ax + aw > bx &&
            ay < by + bh &&
            ay + ah > by);
}

static void dodge_step(void)
{
    if (dodge.state != GAME_RUNNING) return;

    dodge.tick++;

    for (uint8_t i = 0; i < DODGE_MAX_OBS; i++) {
        dodge.obs_y[i] += dodge.obs_speed[i];

        if (dodge.obs_y[i] > PLAY_BOTTOM) {
            dodge.score++;
            dodge_reset_obstacle(i, PLAY_TOP - 20);
            dodge_update_score_label();

            /* Petite accélération progressive */
            if ((dodge.score % 10) == 0 && dodge.obs_speed[i] < 9) {
                dodge.obs_speed[i]++;
            }
        }

        lv_obj_set_pos(dodge.obstacles[i], dodge.obs_x[i], dodge.obs_y[i]);

        if (rects_collide(dodge.player_x, dodge.player_y, DODGE_PLAYER_W, DODGE_PLAYER_H,
                          dodge.obs_x[i], dodge.obs_y[i], DODGE_OBS_W, DODGE_OBS_H)) {
            dodge.state = GAME_OVER;
            if (dodge.score > best_dodge_score) best_dodge_score = dodge.score;
            dodge_set_message("PERDU — touche RESTART");
            lv_label_set_text(dodge.lbl_btn, "RESTART");
            show_result_screen(0, "REJOUER");
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   CALLBACKS MENU / NAVIGATION
   ═════════════════════════════════════════════════════════════════════ */
static void menu_pong_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_pong();
}

static void menu_dodge_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_dodge();
}

static void menu_calibration_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_calibration();
}

static void menu_info_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_info();
}

static void menu_audit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_audit();
}

static void menu_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    build_menu();
}

static void restart_current_game_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (app_mode == APP_PONG) {
        build_pong();
    } else if (app_mode == APP_DODGE) {
        build_dodge();
    } else {
        build_menu();
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   MISE A JOUR CAPTEURS + ROUTAGE JEU
   ═════════════════════════════════════════════════════════════════════ */
static void update_sensor_cache(SRF08_Result_t *left, SRF08_Result_t *right)
{
    if (left->valid) {
        g_left_cm = sensor_filter_push(&filter_left, left->distance_cm);
        g_left_ok = 1;
    } else {
        /* En cas de mesure ratée, on garde la dernière position filtrée au lieu de faire sauter le jeu. */
        g_left_cm = get_filtered_cm(&filter_left);
        g_left_ok = filter_left.initialized;
    }

    if (right->valid) {
        g_right_cm = sensor_filter_push(&filter_right, right->distance_cm);
        g_right_ok = 1;
    } else {
        g_right_cm = get_filtered_cm(&filter_right);
        g_right_ok = filter_right.initialized;
    }

    update_common_distance_labels();
}

static void app_update_from_sensors(SRF08_Result_t *left, SRF08_Result_t *right)
{
    update_sensor_cache(left, right);

    if (app_mode == APP_PONG) {
        pong_update_from_sensors();
        pong_step();
    } else if (app_mode == APP_DODGE) {
        dodge_update_from_sensors();
        dodge_step();
    } else if (app_mode == APP_AUDIT) {
        audit_update_screen();
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   SETUP & TASK
   ═════════════════════════════════════════════════════════════════════ */
void mySetup()
{
    Wire.begin();   /* Init I2C via Arduino Wire — PB8/PB9 par défaut */
    build_menu();   /* Menu tactile : Pong ou Dodge */
}

void loop() {}

void myTask(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    TickType_t xLastWakeTime = xTaskGetTickCount();
    SRF08_Result_t res_left, res_right;

    while (1)
    {
        /* Mesures quasi simultanées : on lance les 2 capteurs, puis on attend une seule fois. */
        SRF08_MeasurePair(&res_left, &res_right);

        lvglLock();
        app_update_from_sensors(&res_left, &res_right);
        lvglUnlock();

        /* La mesure des 2 capteurs prend maintenant environ 70 ms au lieu de 140 ms. */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(75));
    }
}

#else
/* ─── Stub simulateur desktop ─────────────────────────────────────── */
#include "app_hal.h"

typedef struct { uint16_t distance_cm; uint8_t light; uint32_t response_ms; uint8_t valid; } SRF08_Result_t;
typedef struct { uint16_t buf[3]; uint8_t idx; uint8_t count; } Averager_t;
typedef struct {
    lv_obj_t *panel, *lbl_title, *lbl_addr, *lbl_distance, *lbl_distance_raw;
    lv_obj_t *bar, *lbl_light, *lbl_response, *lbl_status, *lbl_counter;
    uint32_t measure_count;
} SensorUI_t;

static SensorUI_t ui_left, ui_right;
static Averager_t avg_left = {{0},0,0}, avg_right = {{0},0,0};

static lv_color_t dist_color(uint16_t cm) { if(cm<20) return lv_color_hex(0xFF4444); if(cm<50) return lv_color_hex(0xFFD700); return lv_color_hex(0x3FB950); }
static uint16_t push_average(Averager_t *a, uint16_t v) { (void)a; return v; }
static void build_sensor_panel(SensorUI_t *ui, lv_obj_t *scr, lv_coord_t x, lv_coord_t w, const char *t, const char *a) { (void)ui;(void)scr;(void)x;(void)w;(void)t;(void)a; }
static void build_ui() {}
static void update_sensor_ui(SensorUI_t *ui, SRF08_Result_t *r, Averager_t *avg) { (void)ui;(void)r;(void)avg; }

int main(void)
{
    printf("Simulateur — Dual SRF08\n");
    lv_init();
    hal_setup();
    build_ui();
    SRF08_Result_t fake_l = {123, 45, 72, 1};
    SRF08_Result_t fake_r = {67,  30, 68, 1};
    update_sensor_ui(&ui_left,  &fake_l, &avg_left);
    update_sensor_ui(&ui_right, &fake_r, &avg_right);
    hal_loop();
    return 0;
}
#endif