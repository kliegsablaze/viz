/*
 * device.h — grids, and the sender that keeps one up to date (DESIGN.md,
 * "Other grids: the device layer", "Sending: only what changed, within budget").
 *
 * Runs on the audio thread. It never calls the host itself: the caller hands
 * in a send function, so tests and the preview can watch every packet.
 */
#ifndef VIZ_DEVICE_H
#define VIZ_DEVICE_H

#include <stdint.h>

#include "viz.h"

enum { GRID_MATRIX, GRID_LP_MINI3, GRID_LP_X, GRID_LP_PRO3, GRID_LP_MK2, GRID_LP_PRO, GRID_COUNT };

typedef struct {
    const char *name;
    uint8_t hdr[5];         /* SysEx header after F0 */
    uint8_t cmd;            /* colour message */
    int     spec;           /* 0: idx r g b; 1: 03 idx r g b (Novation MK3 lighting type RGB) */
    int     bits;           /* per channel */
    int     fill;           /* has a fill-everything message (cmd, index 0) */
    float   edge_weight;    /* power cost of an edge light against a pad */
    uint8_t setup[4];       /* message body sent on load and every refresh, after the header */
    int     setup_len;
} viz_grid_t;

extern const viz_grid_t viz_grids[GRID_COUNT];
extern const char *const viz_grid_names[GRID_COUNT];

/* The grid's index for light id (0..63 pads, x + 8y from the top left; 64..95
 * edge, see viz.h), or 0 when the grid has no such light. */
uint8_t viz_grid_index(int grid, int id);
unsigned viz_grid_edge_sides(int grid);

typedef int (*viz_send_fn)(void *ctx, const uint8_t pkt[4]);

#define SEND_MAX_MSG 512
#define SEND_PER_SECOND 2000.0
#define SEND_PER_BLOCK_MAX 6

typedef struct {
    int grid;
    int pending_grid;           /* a GRID change waits for the message in flight */
    uint8_t frame[NLIGHT][3];   /* the newest frame, 8-bit */
    uint8_t want[NLIGHT][3];    /* the same, quantised to the grid's bits */
    uint8_t shown[NLIGHT][3];
    uint8_t known[NLIGHT];      /* 0: what the light shows is unknown, so send it */
    /* the message in flight */
    uint8_t msg[SEND_MAX_MSG];
    int msg_len, msg_pos;       /* bytes */
    uint8_t fl_id[NLIGHT];
    uint8_t fl_val[NLIGHT][3];
    int fl_n;
    int fl_fill;                /* the message is a fill: every present light takes fl_val[0] */
    double credit;
    /* Move's notes: the lights they hit ("hot"), and the Matrix's custom
     * palettes 1 and 2 (channels 3, 4). Entry id of one holds what hot light
     * id shows, and the other is written with what it will show next, before
     * the frame that changes it, so a cover can be exact */
    uint8_t hot[NLIGHT];
    uint8_t cur[NLIGHT][3];     /* what each light shows: frames, fills and covers (6-bit) */
    uint8_t cur_known[NLIGHT];
    int8_t nextb[NLIGHT];       /* the palette last written with a hot light's next colour, -1 none */
    uint8_t pal[2][NLIGHT][3];
    uint8_t pal_known[2][NLIGHT];
    int fl_pal;                 /* the message in flight writes palette fl_pal - 1 */
    int last_pal;
    /* covers: one-packet repaints sent the moment Move's own MIDI out paints a light */
    uint8_t cover[8][3];
    long cover_block[8];
    int cover_next, covers_this_block;
    long covers_block;
    int refresh_cursor;
    long blocks;
    long next_refresh, next_setup;
    int setup_due;
    /* counters, for tests and logs */
    long sent, refused, msgs, covers, cover_refused, painted, replaced;
} viz_sender_t;

void viz_sender_init(viz_sender_t *s, int grid);
/* Switch grids: everything is unknown again, and the new grid is set up. */
void viz_sender_set_grid(viz_sender_t *s, int grid);
/* Take a new frame (8-bit). */
void viz_sender_frame(viz_sender_t *s, const viz_frame_t *f);
/* One audio block: send what the budget allows. */
void viz_sender_block(viz_sender_t *s, viz_send_fn send, void *ctx);

/* The light a channel message paints on the grid (Matrix Performance: Note
 * On, Note Off or CC on channels 1-6 through its drum-rack note map; a
 * Launchpad in Programmer mode: channels 1-3, the note is the index), or -1. */
int viz_grid_painted(int grid, const uint8_t msg[3]);
/* Move is sending msg out of USB-A, and it reaches the grid as well. If it
 * paints a light, that light is sent again first thing. On the Matrix a
 * one-packet cover (the nearest palette colour, never brighter) takes the
 * place of Move's packet in mbox, the MIDI_OUT region about to ship, so the
 * Matrix never sees the note; when the packet isn't there (or mbox is NULL)
 * the cover is sent instead, at most 4 a block. Viz's own sent covers, coming
 * back, are ignored. */
void viz_sender_midi(viz_sender_t *s, const uint8_t msg[3], uint8_t *mbox, viz_send_fn send, void *ctx);
/* The two Matrix palettes (channel 1: Matrix, channel 2: Launchpad X), from
 * Matrix OS (MIT, 203 Systems), Applications/Performance/8x8/Performance8x8.h. */
extern const uint8_t viz_matrix_palette[2][128][3];
/* The palette colour nearest c that is no brighter (r+g+b) than c, as a
 * channel (0, 1) and velocity. */
void viz_matrix_nearest(const uint8_t c[3], int *channel, int *velocity);

#endif
