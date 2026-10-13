/**
 * @file setup_card.h
 * A card over the dial with a QR code: "scan to open settings" once on WiFi,
 * or "scan to join the setup network" on first boot. Opened by a long press.
 */
#ifndef SETUP_CARD_H
#define SETUP_CARD_H

#include <stdbool.h>

#include "lvgl.h"

typedef struct {
    const char * title;   /* e.g. "SETTINGS" */
    const char * hint;    /* above the code, e.g. "Scan with your phone" */
    const char * qr_text; /* what the code encodes: a URL, or "WIFI:S:...;;" to join a network */
    const char * line1;   /* below the code, e.g. the address */
    const char * line2;   /* smaller, e.g. a status */
} setup_card_info_t;

/** Build the card, hidden, as a child of `parent` (the 480x480 face). */
void setup_card_create(lv_obj_t * parent);

/** Set what the card shows. Strings are copied. */
void setup_card_set_info(const setup_card_info_t * info);

void setup_card_show(bool show);
bool setup_card_visible(void);

#endif /* SETUP_CARD_H */
