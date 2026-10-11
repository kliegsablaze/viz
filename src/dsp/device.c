/*
 * device.c — grids and the sender (DESIGN.md, "Other grids", "Sending").
 *
 * The sender keeps, for every light, what it wants and what it believes the
 * grid shows. Each block it sends the next packets of one SysEx message, at
 * about 2,000 packets a second and never more than 6 in a block. A message
 * carries the lights that changed most, or, on a grid that has one, a single
 * fill when most of the grid wants one colour. A light counts as shown only
 * when the last packet of its message was accepted; a refused packet is
 * retried on the next block, never skipped. Every light is re-sent at least
 * every 2 s, which heals pads Move's own MIDI out painted over and anything
 * lost across a replug.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"

const char *const viz_grid_names[GRID_COUNT] = {
    "Matrix", "LP Mini MK3", "LP X", "LP Pro MK3", "LP MK2", "LP Pro"
};

/* Header bytes after F0. The Launchpad rows are from Novation's programmer's
 * reference guides and are untested (DESIGN.md). */
const viz_grid_t viz_grids[GRID_COUNT] = {
    { "Matrix",      {0x00, 0x02, 0x03, 0x4D, 0x58}, 0x5E, 0, 6, 1, 2.0f, {0},          0 },
    { "LP Mini MK3", {0x00, 0x20, 0x29, 0x02, 0x0D}, 0x03, 1, 7, 0, 1.0f, {0x0E, 0x01}, 2 },
    { "LP X",        {0x00, 0x20, 0x29, 0x02, 0x0C}, 0x03, 1, 7, 0, 1.0f, {0x0E, 0x01}, 2 },
    { "LP Pro MK3",  {0x00, 0x20, 0x29, 0x02, 0x0E}, 0x03, 1, 7, 0, 1.0f, {0x0E, 0x01}, 2 },
    { "LP MK2",      {0x00, 0x20, 0x29, 0x02, 0x18}, 0x0B, 0, 6, 0, 1.0f, {0x22, 0x00}, 2 },
    { "LP Pro",      {0x00, 0x20, 0x29, 0x02, 0x10}, 0x0B, 0, 6, 0, 1.0f, {0x2C, 0x03}, 2 },
};
#define HDR_LEN 5

/* Row x 10 + column from the bottom left, on every grid (the Launchpad Pro
 * numbering the Matrix's 5E message borrows). */
uint8_t viz_grid_index(int grid, int id) {
    if (id < 0 || id >= NLIGHT) return 0;
    if (id < NPAD) {
        int x = id % 8, y = id / 8;
        return (uint8_t)((8 - y) * 10 + x + 1);
    }
    int side = (id - NPAD) / 8, i = (id - NPAD) % 8;
    switch (side) {
    case EDGE_RIGHT:
        return (uint8_t)((8 - i) * 10 + 9);
    case EDGE_LEFT:
        if (grid == GRID_MATRIX || grid == GRID_LP_PRO3 || grid == GRID_LP_PRO) return (uint8_t)((8 - i) * 10);
        return 0;
    case EDGE_BOTTOM:
        if (grid == GRID_MATRIX || grid == GRID_LP_PRO) return (uint8_t)(1 + i);
        if (grid == GRID_LP_PRO3) return (uint8_t)(101 + i);
        return 0;
    default:
        if (grid == GRID_LP_MK2) return (uint8_t)(104 + i);
        return (uint8_t)(91 + i);
    }
}

unsigned viz_grid_edge_sides(int grid) {
    unsigned m = 0;
    for (int s = 0; s < 4; s++)
        if (viz_grid_index(grid, NPAD + s * 8)) m |= 1u << s;
    return m;
}

void viz_sender_init(viz_sender_t *s, int grid) {
    memset(s, 0, sizeof *s);
    s->grid = grid >= 0 && grid < GRID_COUNT ? grid : 0;
    s->pending_grid = -1;
    s->setup_due = 1;
}

static void quantise(viz_sender_t *s) {
    int shift = 8 - viz_grids[s->grid].bits;
    for (int i = 0; i < NLIGHT; i++)
        for (int k = 0; k < 3; k++) s->want[i][k] = (uint8_t)(s->frame[i][k] >> shift);
}

static void switch_grid(viz_sender_t *s, int grid) {
    s->grid = grid;
    s->pending_grid = -1;
    quantise(s);                 /* the new grid may have other bits per channel */
    memset(s->known, 0, sizeof s->known);
    s->setup_due = 1;
}

void viz_sender_set_grid(viz_sender_t *s, int grid) {
    if (grid < 0 || grid >= GRID_COUNT || grid == s->grid) return;
    /* Finish the message on the wire first: cut short, it would leave the
     * device holding half a SysEx. */
    if (s->msg_pos > 0 && s->msg_pos < s->msg_len) { s->pending_grid = grid; return; }
    s->msg_len = s->msg_pos = 0;
    s->fl_n = 0;
    switch_grid(s, grid);
}

void viz_sender_frame(viz_sender_t *s, const viz_frame_t *f) {
    memcpy(s->frame, f->rgb, sizeof s->frame);
    quantise(s);
}

static void begin(viz_sender_t *s, const viz_grid_t *g) {
    s->msg[0] = 0xF0;
    memcpy(s->msg + 1, g->hdr, HDR_LEN);
    s->msg_len = 1 + HDR_LEN;
    s->msg_pos = 0;
    s->fl_n = 0;
    s->fl_fill = 0;
}

static int build_setup(viz_sender_t *s) {
    const viz_grid_t *g = &viz_grids[s->grid];
    if (!g->setup_len) return 0;
    begin(s, g);
    memcpy(s->msg + s->msg_len, g->setup, (size_t)g->setup_len);
    s->msg_len += g->setup_len;
    s->msg[s->msg_len++] = 0xF7;
    return 1;
}

static int same3(const uint8_t *a, const uint8_t *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

static int build_frame(viz_sender_t *s) {
    const viz_grid_t *g = &viz_grids[s->grid];
    int ids[NLIGHT], w[NLIGHT], n = 0, present = 0;
    for (int id = 0; id < NLIGHT; id++) {
        if (!viz_grid_index(s->grid, id)) continue;
        present++;
        int d;
        if (!s->known[id]) d = 10000;
        else d = abs(s->want[id][0] - s->shown[id][0]) + abs(s->want[id][1] - s->shown[id][1]) +
                 abs(s->want[id][2] - s->shown[id][2]);
        if (d > 0) { ids[n] = id; w[n] = d; n++; }
    }
    if (!n) return 0;

    /* Most of the grid wants one colour (a flash, a fade to black): one fill,
     * four packets, and the rest follows as ordinary changes. */
    if (g->fill && n >= 24) {
        int best = -1, best_count = 0;
        for (int a = 0; a < NLIGHT && best_count * 2 < present; a++) {
            if (!viz_grid_index(s->grid, a)) continue;
            int c = 0;
            for (int b = 0; b < NLIGHT; b++)
                if (viz_grid_index(s->grid, b) && same3(s->want[a], s->want[b])) c++;
            if (c > best_count) { best_count = c; best = a; }
        }
        if (best >= 0 && best_count * 10 >= present * 6) {
            begin(s, g);
            s->msg[s->msg_len++] = g->cmd;
            s->msg[s->msg_len++] = 0x00;
            memcpy(s->msg + s->msg_len, s->want[best], 3);
            s->msg_len += 3;
            s->msg[s->msg_len++] = 0xF7;
            s->fl_fill = 1;
            memcpy(s->fl_val[0], s->want[best], 3);
            return 1;
        }
    }

    /* Biggest change first, so under pressure the picture lags rather than
     * smears. One message holds about one frame's worth of packets. */
    for (int i = 1; i < n; i++) {
        int id = ids[i], wi = w[i], j = i - 1;
        while (j >= 0 && w[j] < wi) { ids[j + 1] = ids[j]; w[j + 1] = w[j]; j--; }
        ids[j + 1] = id; w[j + 1] = wi;
    }
    int per = g->spec ? 5 : 4;
    int bytes = (int)(SEND_PER_SECOND / VIZ_FPS) * 3;
    int maxl = (bytes - (HDR_LEN + 3)) / per;
    if (n > maxl) n = maxl;
    begin(s, g);
    s->msg[s->msg_len++] = g->cmd;
    for (int i = 0; i < n; i++) {
        int id = ids[i];
        if (g->spec) s->msg[s->msg_len++] = 0x03;
        s->msg[s->msg_len++] = viz_grid_index(s->grid, id);
        memcpy(s->msg + s->msg_len, s->want[id], 3);
        s->msg_len += 3;
        s->fl_id[i] = (uint8_t)id;
        memcpy(s->fl_val[i], s->want[id], 3);
    }
    s->fl_n = n;
    s->msg[s->msg_len++] = 0xF7;
    return 1;
}

static void commit(viz_sender_t *s) {
    if (s->fl_fill) {
        for (int id = 0; id < NLIGHT; id++)
            if (viz_grid_index(s->grid, id)) { memcpy(s->shown[id], s->fl_val[0], 3); s->known[id] = 1; }
    } else {
        for (int i = 0; i < s->fl_n; i++) {
            memcpy(s->shown[s->fl_id[i]], s->fl_val[i], 3);
            s->known[s->fl_id[i]] = 1;
        }
    }
    s->msgs++;
    s->msg_len = s->msg_pos = 0;
    s->fl_n = 0;
    s->fl_fill = 0;
}

void viz_sender_block(viz_sender_t *s, viz_send_fn send, void *ctx) {
    s->blocks++;
    /* Refresh: a quarter of the lights become unknown every 0.5 s, and a
     * Launchpad is told its mode again every 2 s, in case it was replugged. */
    long half = (long)(0.5 * VIZ_SR / VIZ_BLOCK);
    if (s->blocks >= s->next_refresh) {
        for (int k = 0; k < NLIGHT / 4; k++) s->known[(s->refresh_cursor + k) % NLIGHT] = 0;
        s->refresh_cursor = (s->refresh_cursor + NLIGHT / 4) % NLIGHT;
        s->next_refresh = s->blocks + half;
    }
    if (s->blocks >= s->next_setup) {
        s->setup_due = 1;
        s->next_setup = s->blocks + 4 * half;
    }

    /* Even spacing: never bank more than one block's worth. */
    double per_block = SEND_PER_SECOND * VIZ_BLOCK / VIZ_SR;
    double cap = ceil(per_block) < SEND_PER_BLOCK_MAX ? ceil(per_block) : SEND_PER_BLOCK_MAX;
    s->credit += per_block;
    if (s->credit > cap) s->credit = cap;
    int allow = (int)s->credit, sent = 0;

    while (sent < allow) {
        if (s->msg_pos >= s->msg_len) {
            if (s->pending_grid >= 0) switch_grid(s, s->pending_grid);
            int ok = 0;
            if (s->setup_due) { s->setup_due = 0; ok = build_setup(s); }
            if (!ok) ok = build_frame(s);
            if (!ok) break;
        }
        int rem = s->msg_len - s->msg_pos;
        uint8_t p[4] = { 0, 0, 0, 0 };
        int n = rem > 3 ? 3 : rem;
        p[0] = (uint8_t)(rem > 3 ? 0x24 : 0x24 + rem);   /* cable 2: SysEx continues / ends */
        memcpy(p + 1, s->msg + s->msg_pos, (size_t)n);
        if (!send(ctx, p)) { s->refused++; break; }
        s->msg_pos += n;
        s->sent++;
        sent++;
        if (s->msg_pos >= s->msg_len) commit(s);
    }
    s->credit -= sent;
}
