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
 * every 2 s, which heals anything lost across a replug.
 *
 * Move's own MIDI out goes to the grid too, and the Matrix paints any note on
 * channels 1-6. Viz hears those notes as they leave (a Master FX gets them in
 * on_midi, in the same block), so it covers each one at once and re-sends the
 * light (viz_sender_midi).
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
        /* Only when the fill itself changes many lights: if the common colour
         * is already showing, filling again would only wipe the rest, forever. */
        int gain = 0, keep = 0, over = 0;
        if (best >= 0) {
            const uint8_t *b = s->want[best];
            for (int i = 0; i < n; i++) gain += same3(s->want[ids[i]], b);
            for (int id = 0; id < NLIGHT; id++) {
                if (!viz_grid_index(s->grid, id) || same3(s->want[id], b)) continue;
                const uint8_t *w = s->want[id];
                /* lights already right in another colour, which the fill would blank */
                if (s->known[id] && same3(w, s->shown[id])) keep++;
                /* a light the fill would make brighter than it should be: the
                 * power guard counted it at its own colour */
                if (b[0] + b[1] + b[2] > w[0] + w[1] + w[2]) over = 1;
            }
        }
        if (best >= 0 && best_count * 10 >= present * 6 && gain >= 24 && keep * 2 <= gain && !over) {
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

/* ---- Move's own notes --------------------------------------------------- */

const uint8_t viz_matrix_palette[2][128][3] = {
    {
        {0,0,0}, {60,0,0}, {125,0,0}, {190,0,0}, {255,125,125}, {255,0,0}, {60,12,0}, {125,28,0},
        {190,44,0}, {255,157,125}, {255,60,0}, {60,28,0}, {125,60,0}, {190,93,0}, {255,190,125}, {255,125,0},
        {60,44,0}, {125,93,0}, {190,141,0}, {255,222,125}, {255,190,0}, {60,60,0}, {125,125,0}, {190,190,0},
        {255,255,125}, {255,255,0}, {44,60,0}, {93,125,0}, {141,190,0}, {222,255,125}, {190,255,0}, {28,60,0},
        {60,125,0}, {93,190,0}, {190,255,125}, {125,255,0}, {12,60,0}, {28,125,0}, {44,190,0}, {157,255,125},
        {60,255,0}, {0,60,0}, {0,125,0}, {0,190,0}, {125,255,125}, {0,255,0}, {0,60,12}, {0,125,28},
        {0,190,44}, {125,255,157}, {0,255,60}, {0,60,28}, {0,125,60}, {0,190,93}, {125,255,190}, {0,255,125},
        {0,60,44}, {0,125,93}, {0,190,141}, {125,255,222}, {0,255,190}, {0,60,60}, {0,125,125}, {0,190,190},
        {125,255,255}, {0,255,255}, {0,44,60}, {0,93,125}, {0,141,190}, {125,222,255}, {0,190,255}, {0,28,60},
        {0,60,125}, {0,93,190}, {125,190,255}, {0,125,255}, {0,12,60}, {0,28,125}, {0,44,190}, {125,157,255},
        {0,60,255}, {0,0,60}, {0,0,125}, {0,0,190}, {125,125,255}, {0,0,255}, {12,0,60}, {28,0,125},
        {44,0,190}, {157,125,255}, {60,0,255}, {28,0,60}, {60,0,125}, {93,0,190}, {190,125,255}, {125,0,255},
        {44,0,60}, {93,0,125}, {141,0,190}, {222,125,255}, {190,0,255}, {60,0,60}, {125,0,125}, {190,0,190},
        {255,125,255}, {255,0,255}, {60,0,44}, {125,0,93}, {190,0,141}, {255,125,222}, {255,0,190}, {60,0,28},
        {125,0,60}, {190,0,93}, {255,125,190}, {255,0,125}, {60,0,12}, {125,0,28}, {190,0,44}, {255,125,157},
        {255,0,60}, {36,36,36}, {72,72,72}, {109,109,109}, {145,145,145}, {182,182,182}, {218,218,218}, {255,255,255},
    },
    {
        {0,0,0}, {63,63,63}, {127,127,127}, {255,255,255}, {255,63,63}, {255,0,0}, {127,0,0}, {63,0,0},
        {255,191,111}, {255,63,0}, {127,31,0}, {63,15,0}, {255,175,47}, {255,255,0}, {127,127,0}, {63,63,0},
        {127,255,47}, {79,255,0}, {47,127,0}, {23,63,0}, {79,255,63}, {0,255,0}, {0,127,0}, {0,63,0},
        {79,255,79}, {0,255,31}, {0,127,15}, {0,63,7}, {79,255,95}, {0,255,95}, {0,127,47}, {0,63,23},
        {79,255,191}, {0,255,159}, {0,127,79}, {0,63,39}, {79,191,255}, {0,175,255}, {0,87,127}, {0,47,63},
        {79,127,255}, {0,87,255}, {0,47,127}, {0,23,63}, {47,31,255}, {0,0,255}, {0,0,127}, {0,0,63},
        {95,63,255}, {47,0,255}, {23,0,127}, {15,0,63}, {255,63,255}, {255,0,255}, {127,0,127}, {63,0,63},
        {255,63,111}, {255,0,79}, {127,0,47}, {63,0,31}, {255,15,0}, {159,63,0}, {127,79,0}, {47,47,0},
        {0,63,0}, {0,63,31}, {0,31,111}, {0,0,255}, {0,63,63}, {31,0,191}, {95,63,79}, {31,15,23},
        {255,0,0}, {191,255,47}, {175,239,0}, {95,255,0}, {15,127,0}, {0,255,95}, {0,159,255}, {0,47,255},
        {31,0,255}, {95,0,239}, {175,31,127}, {47,15,0}, {255,47,0}, {127,223,0}, {111,255,31}, {0,255,0},
        {63,255,47}, {95,239,111}, {63,255,207}, {95,143,255}, {47,79,207}, {111,79,223}, {223,31,255}, {255,0,95},
        {255,79,0}, {191,175,0}, {143,255,0}, {127,95,0}, {63,47,0}, {0,71,15}, {15,79,31}, {23,23,47},
        {23,31,95}, {95,55,23}, {127,0,0}, {223,63,47}, {223,71,15}, {255,191,31}, {159,223,47}, {111,175,15},
        {23,23,47}, {223,223,111}, {127,239,143}, {159,159,255}, {143,111,255}, {63,63,63}, {111,111,111}, {223,255,255},
        {159,0,0}, {55,0,0}, {23,207,0}, {0,63,0}, {191,175,0}, {63,47,0}, {175,79,0}, {79,15,0},
    },
};

int viz_grid_painted(int grid, const uint8_t msg[3]) {
    int type = msg[0] & 0xF0, ch = msg[0] & 0x0F, n = msg[1];
    if (type != 0x80 && type != 0x90 && type != 0xB0) return -1;
    if (grid != GRID_MATRIX) {
        if (ch > 2) return -1;
        for (int id = 0; id < NLIGHT; id++) if (viz_grid_index(grid, id) == n) return id;
        return -1;
    }
    if (ch > 5) return -1;                       /* Performance ignores 7-16 */
    if (n >= 36 && n <= 99) {                    /* two 4-wide halves, bottom up */
        int k = n < 68 ? n - 36 : n - 68;
        return (n < 68 ? 0 : 4) + k % 4 + 8 * (7 - k / 4);
    }
    if (n >= 100 && n <= 107) return NPAD + EDGE_RIGHT * 8 + n - 100;
    if (n >= 108 && n <= 115) return NPAD + EDGE_LEFT * 8 + n - 108;
    if (n >= 116 && n <= 123) return NPAD + EDGE_BOTTOM * 8 + n - 116;
    if (n >= 28 && n <= 35)   return NPAD + EDGE_TOP * 8 + n - 28;
    return -1;
}

void viz_matrix_nearest(const uint8_t c[3], int *channel, int *velocity) {
    int sum = c[0] + c[1] + c[2], best = -1, bc = 0, bv = 0;
    for (int p = 0; p < 2; p++)
        for (int v = 0; v < 128; v++) {
            const uint8_t *q = viz_matrix_palette[p][v];
            if (q[0] + q[1] + q[2] > sum) continue;
            int d = (q[0] - c[0]) * (q[0] - c[0]) + (q[1] - c[1]) * (q[1] - c[1]) + (q[2] - c[2]) * (q[2] - c[2]);
            if (best < 0 || d < best) { best = d; bc = p; bv = v; }
        }
    *channel = bc;
    *velocity = bv;
}

#define COVERS_PER_BLOCK 4
#define COVER_ECHO_BLOCKS 16

void viz_sender_midi(viz_sender_t *s, const uint8_t msg[3], viz_send_fn send, void *ctx) {
    for (int i = 0; i < 8; i++)
        if (s->cover_block[i] && s->blocks - s->cover_block[i] <= COVER_ECHO_BLOCKS &&
            !memcmp(s->cover[i], msg, 3)) return;          /* our own cover, echoed */
    int id = viz_grid_painted(s->grid, msg);
    if (id < 0 || !viz_grid_index(s->grid, id)) return;
    s->known[id] = 0;                                       /* sent again, first */
    if (s->grid != GRID_MATRIX) return;
    if (s->covers_block != s->blocks) { s->covers_block = s->blocks; s->covers_this_block = 0; }
    if (s->covers_this_block >= COVERS_PER_BLOCK) return;
    int ch, vel;
    viz_matrix_nearest(s->frame[id], &ch, &vel);
    /* a note on of the light's own note: one packet, which the Matrix reads
     * on its own even between the packets of a SysEx message */
    uint8_t p[4] = { 0x29, (uint8_t)(0x90 | ch), msg[1], (uint8_t)vel };
    if (!send(ctx, p)) return;
    memcpy(s->cover[s->cover_next], p + 1, 3);
    s->cover_block[s->cover_next] = s->blocks ? s->blocks : 1;
    s->cover_next = (s->cover_next + 1) % 8;
    s->covers_this_block++;
    s->covers++;
}
