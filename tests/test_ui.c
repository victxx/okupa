#include "okupa_act.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include "okupa_facts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];

int main(void)
{
    int64_t t = 1800000000;
    ok_ui u;
    ok_pet p;
    int i, inked = 0;

    ok_pet_init(&p, t);
    ok_ui_init(&u);

    /* Buttons (button centres from the layout: columns at 8 + 62 i, rows at 34 and 92; tabs Care 95,160 and More 161,160). */
    assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_HUNGER] == 100);
    assert(!strcmp(u.say, "Yum! Thank you."));
    assert(ok_ui_touch(&u, &p, 35, 60, 1, t) == OK_UI_NOTHING); /* holding is not a second tap */
    assert(ok_ui_touch(&u, &p, 97, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_FUN] == 95);
    assert(ok_ui_touch(&u, &p, 159, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_LOVE] == 90);
    p.need[OK_NEED_ENERGY] = 90;
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && !p.asleep); /* not sleepy */
    p.need[OK_NEED_ENERGY] = 30;
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && p.asleep);
    assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_CHANGED && !strcmp(u.say, "Zzz... (it's sleeping)"));
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && !p.asleep);
    /* No mind: one menu (Feed..Sleep, Draw, Gallery, Dev), no tabs, no dead buttons. */
    assert(ok_ui_touch(&u, &p, 161, 160, 0, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 221, 118, 0, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 97, 118, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_GALLERY);
    assert(ok_ui_touch(&u, &p, 210, 175, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME); /* Back */
    /* Everything wired: Talk, Photo on the first menu; Games, Friend, Dev on More. */
    u.wired = OK_UI_HAS_PHOTO | OK_UI_HAS_TALK | OK_UI_HAS_GAMES | OK_UI_HAS_FRIEND;
    assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_TALK && u.scene == OK_UI_HOME);
    assert(ok_ui_touch(&u, &p, 221, 118, 0, t) == OK_UI_PHOTO);
    assert(ok_ui_touch(&u, &p, 161, 157, 0, t) == OK_UI_CHANGED && u.page == 1); /* More */
    assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_GAMES && p.need[OK_NEED_HUNGER] == 100);
    assert(u.scene == OK_UI_SKETCH && u.pictionary); /* Pictionary: its own page in the sketchbook */
    u.scene = OK_UI_HOME;
    u.pictionary = 0;
    assert(ok_ui_touch(&u, &p, 97, 60, 0, t) == OK_UI_FRIEND);
    assert(ok_ui_touch(&u, &p, 95, 157, 0, t) == OK_UI_CHANGED && u.page == 0); /* Care */
    u.wired = 0;
    ok_facts_now.og_features = 1 | 2 | 8; /* the mind hears and plays, no camera */
    assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_TALK);
    assert(ok_ui_touch(&u, &p, 221, 118, 0, t) == OK_UI_GAMES);
    /* Pictionary draws on its own page: the drawing to send is untouched; Guess! needs ink. */
    assert(u.scene == OK_UI_SKETCH && u.pictionary);
    assert(ok_ui_touch(&u, &p, 198, 140, 0, t) == OK_UI_CHANGED && !strcmp(u.say, "Draw something first!"));
    assert(ok_ui_touch(&u, &p, 40, 60, 0, t) == OK_UI_CHANGED);
    ok_ui_release(&u);
    for (i = 0, inked = 0; i < (int)sizeof u.doodle; i++) inked += u.doodle[i] != 0;
    assert(inked);
    for (i = 0; i < (int)sizeof u.drawing; i++) assert(u.drawing[i] == 0);
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 198, 140, 0, t) == OK_UI_GUESS);
    u.drawing_sent = 1; /* even with a drawing on its way, the doodle can change */
    assert(ok_ui_touch(&u, &p, 60, 60, 0, t) == OK_UI_CHANGED);
    ok_ui_release(&u);
    u.drawing_sent = 0;
    assert(ok_ui_touch(&u, &p, 198, 120, 0, t) == OK_UI_CHANGED); /* Clear: the doodle only */
    for (i = 0; i < (int)sizeof u.doodle; i++) assert(u.doodle[i] == 0);
    assert(ok_ui_touch(&u, &p, 198, 165, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME && !u.pictionary);
    inked = 0;
    /* Dreams configured: Dream is there with no dream yet, and asks for one; one kept is shown meanwhile. */
    ok_facts_now.og_features = 16;
    assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_DREAM && !u.dream_ticks);
    {
        static uint16_t sky[4] = {0x8000, 0x801f, 0x83e0, 0xfc00};
        ok_ui_dream_keep(&u, sky, 2, 2);
        assert(u.dream_w == 2 && !u.dream_ticks);
        assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_DREAM && u.dream_ticks);
        assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_CHANGED && !u.dream_ticks); /* a tap puts it away */
        ok_facts_now.og_features = 0;
        assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_CHANGED && u.dream_ticks); /* no mind: the kept one only */
        u.dream_ticks = 0;
        u.dream_w = u.dream_h = 0;
    }
    ok_facts_now.og_features = 0;
    assert(ok_ui_touch(&u, &p, 4, 4, 0, t) == OK_UI_NOTHING);

    /* The line goes back to the mood line after three seconds. */
    for (i = 0; i < 180; i++) ok_ui_tick(&u, &p);
    assert(u.say == NULL);

    /* Sketchbook: a stroke leaves no gaps, the eraser removes it, Done goes home. */
    assert(ok_ui_touch(&u, &p, 35, 118, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_SKETCH);
    assert(ok_ui_touch(&u, &p, 8, 100, 0, t) == OK_UI_CHANGED);
    assert(ok_ui_touch(&u, &p, 134, 100, 1, t) == OK_UI_CHANGED);
    ok_ui_release(&u);
    for (i = 0; i < 64; i++) inked += ok_drawing_get(u.drawing, (unsigned)i, 30);
    assert(inked == 64);
    assert(ok_ui_touch(&u, &p, 198, 85, 0, t) == OK_UI_CHANGED && u.eraser);
    assert(ok_ui_touch(&u, &p, 8, 100, 0, t) == OK_UI_CHANGED && ok_drawing_get(u.drawing, 0, 30) == 0);
    ok_ui_release(&u);
    assert(ok_ui_touch(&u, &p, 198, 120, 0, t) == OK_UI_CHANGED);
    for (i = 0; i < (int)sizeof u.drawing; i++) assert(u.drawing[i] == 0);
    assert(ok_ui_touch(&u, &p, 198, 165, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);
    /* Done with ink in it: the platform may ask 0G to look at it. */
    u.scene = OK_UI_SKETCH;
    ok_drawing_set(u.drawing, 3, 3, 1);
    assert(ok_ui_touch(&u, &p, 198, 165, 0, t) == OK_UI_DRAWING_DONE && u.scene == OK_UI_HOME);
    ok_drawing_set(u.drawing, 3, 3, 0);
    /* The gallery: a hung picture tapped is for 0G to look at. */
    u.scene = OK_UI_GALLERY;
    u.wall_count = 2;
    assert(ok_ui_touch(&u, &p, 12 + 82 + 30, 30 + 30, 0, t) == OK_UI_GALLERY_LOOK && u.gallery_pick == 1);
    assert(ok_ui_touch(&u, &p, 12 + 82 * 2 + 30, 30 + 30, 0, t) == OK_UI_NOTHING); /* an empty frame */
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    u.wall_count = 0;
    u.scene = OK_UI_HOME;
    /* Send hands the drawing to the platform; once sent it cannot change. */
    u.scene = OK_UI_SKETCH;
    assert(ok_ui_touch(&u, &p, 198, 140, 0, t) == OK_UI_SEND_DRAWING && u.scene == OK_UI_HOME);
    u.scene = OK_UI_SKETCH;
    u.drawing_sent = 1;
    assert(ok_ui_touch(&u, &p, 20, 60, 0, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 198, 140, 0, t) == OK_UI_NOTHING);
    u.drawing_sent = 0;
    u.scene = OK_UI_HOME;

    /* Dev (on the second menu) asks the platform to run the checks; Back returns. */
    assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_RUN_CHECKS && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 30, 176, 0, t) == OK_UI_RUN_NETWORK && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);

    /* Wi-Fi from Dev: search, pick a secured network, type its password. */
    assert(ok_ui_touch(&u, &p, 159, 118, 0, t) == OK_UI_RUN_CHECKS);
    assert(ok_ui_touch(&u, &p, 90, 176, 0, t) == OK_UI_WIFI_SCAN && u.scene == OK_UI_WIFI);
    u.ap_count = 2;
    strcpy(u.aps[0].ssid, "iPhone de Okupa");
    u.aps[0].secure = 1;
    u.aps[0].strength = 3;
    strcpy(u.aps[1].ssid, "Cafe libre");
    assert(ok_ui_touch(&u, &p, 50, 176, 0, t) == OK_UI_WIFI_SCAN);
    assert(ok_ui_touch(&u, &p, 100, 30, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_PASSWORD && u.ap_selected == 0);
    assert(ok_ui_touch(&u, &p, 8 + 24 * 2 + 5, 36 + 26 + 5, 0, t) == OK_UI_CHANGED);  /* e */
    assert(ok_ui_touch(&u, &p, 10, 145, 0, t) == OK_UI_CHANGED && u.keys == 1);      /* Caps */
    assert(ok_ui_touch(&u, &p, 8 + 24 * 0 + 5, 36 + 26 * 2 + 5, 0, t) == OK_UI_CHANGED); /* A */
    assert(ok_ui_touch(&u, &p, 70, 145, 0, t) == OK_UI_CHANGED && u.keys == 2);      /* symbols */
    assert(ok_ui_touch(&u, &p, 8 + 5, 36 + 26 + 5, 0, t) == OK_UI_CHANGED);          /* ! */
    assert(ok_ui_touch(&u, &p, 8 + 24 * 9 + 5, 36 + 5, 0, t) == OK_UI_CHANGED);      /* 0 */
    assert(ok_ui_touch(&u, &p, 130, 145, 0, t) == OK_UI_CHANGED);                    /* space */
    assert(!strcmp(u.password, "eA!0 ") && u.password_len == 5);
    assert(ok_ui_touch(&u, &p, 200, 145, 0, t) == OK_UI_CHANGED && !strcmp(u.password, "eA!0"));
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 190, 176, 0, t) == OK_UI_WIFI_CONNECT && u.scene == OK_UI_WIFI);
    assert(!strcmp(u.password, "eA!0"));
    assert(ok_ui_touch(&u, &p, 100, 55, 0, t) == OK_UI_WIFI_CONNECT && u.ap_selected == 1 && !u.password_len);
    assert(ok_ui_touch(&u, &p, 100, 30, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_PASSWORD);
    assert(ok_ui_touch(&u, &p, 60, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_WIFI); /* Cancel */
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_DEV);

    /* Wallet: scribbling (held or not) feeds the key until it is ready. */
    assert(ok_ui_touch(&u, &p, 150, 176, 0, t) == OK_UI_WALLET_OPEN && u.scene == OK_UI_WALLET);
    assert(ok_ui_touch(&u, &p, 100, 60, 0, t) == OK_UI_WALLET_SCRIBBLE);
    assert(ok_ui_touch(&u, &p, 120, 70, 1, t) == OK_UI_WALLET_SCRIBBLE);
    u.wallet_progress = 40;
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    u.wallet_ready = 1;
    strcpy(u.wallet_address, "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F");
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    /* Ready: the address and its balance (a tap asks again); the QR only when asked for. */
    assert(!u.wallet_qr && ok_ui_touch(&u, &p, 100, 60, 0, t) == OK_UI_WALLET_BALANCE);
    strcpy(u.wallet_balance, "0.000026 ETH");
    strcpy(u.wallet_balance_note, "block 21450590, 12:03");
    u.wallet_low = 1;
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 150, 176, 0, t) == OK_UI_CHANGED && u.wallet_qr);
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom); /* the QR is encoded once, then reused */
    assert(ok_ui_touch(&u, &p, 100, 60, 0, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 150, 176, 0, t) == OK_UI_CHANGED && !u.wallet_qr);
    assert(ok_ui_touch(&u, &p, 50, 176, 1, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 200, 176, 1, t) == OK_UI_NOTHING);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);

    /* The okupa's mind: a move starts at the next break; out through the door to the terrace and back. */
    p.asleep = 0;
    ok_ui_direct(&u, &p, OK_MOVE_WAVE, 0, OK_POSE_HAPPY, "Hello there!");
    assert(!strcmp(u.say, "Hello there!"));
    for (i = 0; i < 1200 && u.anim.move != OK_MOVE_WAVE; i++) ok_ui_tick(&u, &p);
    assert(u.anim.move == OK_MOVE_WAVE);
    ok_ui_direct(&u, &p, OK_MOVE_TO_DOOR, OK_PLACE_GARDEN, -1, NULL);
    for (i = 0; i < 3000 && ok_house_where() != OK_PLACE_GARDEN; i++) ok_ui_tick(&u, &p);
    assert(ok_house_where() == OK_PLACE_GARDEN);
    ok_ui_direct(&u, &p, OK_MOVE_TO_DOOR, OK_PLACE_ATTIC, -1, "");
    for (i = 0; i < 3000 && ok_house_where() != OK_PLACE_ATTIC; i++) ok_ui_tick(&u, &p);
    assert(ok_house_where() == OK_PLACE_ATTIC);

    /* Its inner life: a thought fades, a question stays until answered or 30 s, listening shows over both. */
    ok_ui_thought(&u, "Clouds\x01 are soft, I bet. This text is long enough to need cutting at sixty chars");
    assert(strlen(u.thought) == 60 && u.thought[6] == '?');
    ok_ui_ask(&u, "Lunch?");
    ok_ui_listening(&u, 1);
    assert(u.listening == 1);
    ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
    ok_ui_listening(&u, 2);
    ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
    ok_ui_listening(&u, 0);
    for (i = 0; i < 400; i++) ok_ui_tick(&u, &p);
    assert(!u.thought_ticks && u.ask_ticks);
    ok_ui_ask(&u, NULL);
    assert(!u.ask_ticks);
    ok_ui_ask_hint(&u, "Is it a cat?", "A yes  B no");
    assert(u.ask_ticks && !strcmp(u.ask_hint, "A yes  B no"));
    ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
    ok_ui_ask(&u, "Lunch?");
    assert(!u.ask_hint[0]);
    ok_ui_ask(&u, NULL);
    ok_ui_ask(&u, "Still there?");
    for (i = 0; i < 1800; i++) ok_ui_tick(&u, &p);
    assert(!u.ask_ticks);

    /* What 0G looked at: a card with the photo and its words; a tap closes it and ends a live look. */
    {
        static uint16_t photo[128 * 96];
        for (i = 0; i < 128 * 96; i++) photo[i] = (uint16_t)(0x8000 | i);
        u.scene = OK_UI_HOME;
        ok_ui_card(&u, "I see...", "A person with glasses holding a red mug, a bookshelf behind them and a lamp on",
                   photo, 128, 96, 1, "#1  3.4 s");
        u.card_live = 1;
        assert(u.card_ticks && u.card_w == 128 && u.card_verified && u.card_px[5] == photo[5]);
        ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
        assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_CHANGED && !u.card_ticks && !u.card_live);
        ok_ui_card(&u, "No answer", "0G did not answer: net -3: TCP: no connection", NULL, 0, 0, 0, "nothing made up");
        assert(!u.card_w && !u.card_verified);
        ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
        ok_ui_card_drawing(&u, "Your drawing", "A cat!", u.drawing, 1, NULL);
        assert(u.card_w == 96 && u.card_h == 96);
        for (i = 0; i < 600; i++) ok_ui_tick(&u, &p);
        assert(!u.card_ticks);
        ok_ui_heard(&u, "hola okupa, que tal? me llamo Victor y hoy he dibujado una casa con un arbol muy grande al lado");
        assert(u.heard_ticks && !strncmp(u.heard, "hola okupa", 10));
        ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
        ok_ui_heard(&u, NULL);
        assert(!u.heard_ticks);
        ok_ui_busy(&u, "Asking 0G...");
        ok_ui_listening(&u, 2);
        ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
        ok_ui_listening(&u, 0);
        ok_ui_busy(&u, NULL);
        assert(!u.busy[0]);
    }

    /* Every scene renders, with and without a font. */
    ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
    assert(top[0] & 0x8000 && bottom[OK_UI_W * OK_UI_H - 1] & 0x8000);
    u.scene = OK_UI_SKETCH;
    ok_ui_render(&u, &p, 23, 59, NULL, top, bottom);
    u.scene = OK_UI_DEV;
    p.asleep = 1;
    ok_ui_render(&u, &p, 3, 0, ok_font(), top, bottom);
    /* The closet: what pictures unlock stays locked until they hang. */
    {
        ok_ui c;
        ok_ui_init(&c);
        c.scene = OK_UI_CLOSET;
        assert(ok_ui_touch(&c, &p, 120, 110, 0, t) == OK_UI_CHANGED && c.look.wear == OK_WEAR_NONE); /* headphones: 3 */
        assert(ok_ui_touch(&c, &p, 200, 150, 0, t) == OK_UI_CHANGED && c.look.wear == OK_WEAR_SMILEY);
        c.wall_count = 3;
        assert(ok_ui_touch(&c, &p, 120, 110, 0, t) == OK_UI_CHANGED && c.look.wear == OK_WEAR_HEADPHONES);
        /* Posting: out with the envelope while it is on its way, not any more once it has landed. */
        c.scene = OK_UI_HOME;
        p.asleep = 0;
        c.drawing_sent = 1;
        ok_ui_tick(&c, &p);
        assert(c.outing == 1 && c.anim.carry);
        c.drawing_sent = 0;
        ok_ui_tick(&c, &p);
        assert(c.outing == 0 && !c.anim.carry);
    }
    puts("ui: buttons drive the okupa, sketchbook strokes and eraser, dev and back, all scenes render");
    return 0;
}
