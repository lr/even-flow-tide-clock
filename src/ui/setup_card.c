/**
 * @file setup_card.c
 */
#include "setup_card.h"

#include <string.h>

#define CARD_SIZE 480
#define QR_SIZE   200 /* plus a quiet zone; about 22 mm on the 2.1" panel */

static struct {
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * hint;
    lv_obj_t * qr;
    lv_obj_t * line1;
    lv_obj_t * line2;
} card;

static lv_obj_t * make_label(lv_obj_t * parent, const lv_font_t * font, uint32_t color, int32_t y)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
    return label;
}

void setup_card_create(lv_obj_t * parent)
{
    card.card = lv_obj_create(parent);
    lv_obj_remove_style_all(card.card);
    lv_obj_set_size(card.card, CARD_SIZE, CARD_SIZE);
    lv_obj_center(card.card);
    lv_obj_set_style_radius(card.card, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(card.card, lv_color_hex(0x0b1622), 0);
    lv_obj_set_style_bg_opa(card.card, LV_OPA_COVER, 0);
    /* Let presses fall through to the dial, which decides what they mean */
    lv_obj_remove_flag(card.card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card.card, LV_OBJ_FLAG_HIDDEN);

    card.title = make_label(card.card, &lv_font_montserrat_14, 0x7f93a8, -168);
    lv_obj_set_style_text_letter_space(card.title, 3, 0);
    card.hint = make_label(card.card, &lv_font_montserrat_20, 0xe8eef4, -136);

    card.qr = lv_qrcode_create(card.card);
    lv_qrcode_set_size(card.qr, QR_SIZE);
    lv_qrcode_set_dark_color(card.qr, lv_color_hex(0x0b1622));
    lv_qrcode_set_light_color(card.qr, lv_color_white());
    lv_qrcode_set_quiet_zone(card.qr, true);
    lv_obj_align(card.qr, LV_ALIGN_CENTER, 0, 4);

    card.line1 = make_label(card.card, &lv_font_montserrat_20, 0xe8eef4, 134);
    card.line2 = make_label(card.card, &lv_font_montserrat_14, 0x7f93a8, 162);
}

void setup_card_set_info(const setup_card_info_t * info)
{
    lv_label_set_text(card.title, info->title ? info->title : "");
    lv_label_set_text(card.hint, info->hint ? info->hint : "");
    lv_label_set_text(card.line1, info->line1 ? info->line1 : "");
    lv_label_set_text(card.line2, info->line2 ? info->line2 : "");
    const char * qr = info->qr_text ? info->qr_text : "";
    lv_qrcode_update(card.qr, qr, (uint32_t)strlen(qr));
}

void setup_card_show(bool show)
{
    if(show) lv_obj_remove_flag(card.card, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(card.card, LV_OBJ_FLAG_HIDDEN);
}

bool setup_card_visible(void)
{
    return !lv_obj_has_flag(card.card, LV_OBJ_FLAG_HIDDEN);
}
