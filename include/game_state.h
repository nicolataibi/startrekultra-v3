#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>

#define MAX_NET_OBJECTS 128
#define MAX_NET_BEAMS 8

typedef struct {
    float net_tx, net_ty, net_tz;
    int active;
} NetBeam;

typedef struct {
    float net_x, net_y, net_z;
    float h, m;
    int type;       /* 1=Player, 3=Base, 4=Star, 5=Planet, etc */
    int ship_class; /* Specifica il modello 3D (es. Galaxy, Constitution) */
    int active;
    int health_pct; /* 0-100% Health/Energy status for HUD */
    int id;         /* Universal Target ID */
} NetObject;

typedef struct {
    float net_x, net_y, net_z;
    int active;
} NetPoint;

typedef struct {
    float net_x, net_y, net_z;
    int species;
    int active;
} NetDismantle;

typedef struct {
    /* Coordinates */
    int q1, q2, q3;             /* Quadrant Position (X, Y, Z) */
    int old_q1, old_q2, old_q3; /* Persistence tracking */
    double s1, s2, s3;          /* Sector Position (X, Y, Z) */

    /* Galaxy Data */
    int g[11][11][11];          /* The Galaxy Cube */
    int z[11][11][11];          /* Scanned Map Cube */
    int k[4][6];                /* Enemy Data */
    int stars_pos[10][3];
    int base_pos[3][3];
    int planet_pos[3][5];
    int bh_pos[1][3];

    /* Resources & Status */
    int energy;
    int torpedoes;
    int crew_count;
    int inventory[7];
    int species_counts[11];
    int shields[6];
    
    /* Current Quadrant counts */
    int k3, b3, st3, p3, bh3;
    
    /* Totals */
    int k9, b9;

    /* Ship Systems */
    double ent_h, ent_m;
    int lock_target;
    float power_dist[3];
    bool is_playing_dead;
    bool is_cloaked;
    float system_health[8];
    float life_support;
    
    /* Time & Meta */
    double t, t0;
    int t9;
    int corbomite_count;
    char captain_name[64];

    /* Visual preferences */
    bool show_axes;
    bool show_grid;

    /* Multi-user sync */
    long long frame_id;
    int object_count;
    NetObject objects[MAX_NET_OBJECTS];
    int beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    NetPoint torp;
    NetPoint boom;
    NetDismantle dismantle;
} StarTrekGame;

#endif
