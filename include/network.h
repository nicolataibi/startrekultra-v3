#ifndef NETWORK_H
#define NETWORK_H

#include "game_state.h"

#define DEFAULT_PORT 5000
#define MAX_CLIENTS 32
#define PKT_LOGIN 1
#define PKT_COMMAND 2
#define PKT_UPDATE 3
#define PKT_MESSAGE 4

#define SCOPE_GLOBAL 0
#define SCOPE_FACTION 1
#define SCOPE_PRIVATE 2

typedef enum {
    FACTION_FEDERATION = 0,
    FACTION_KLINGON,
    FACTION_ROMULAN,
    FACTION_BORG,
    FACTION_CARDASSIAN
} Faction;

typedef enum {
    SHIP_CLASS_CONSTITUTION = 0,
    SHIP_CLASS_MIRANDA,
    SHIP_CLASS_EXCELSIOR,
    SHIP_CLASS_CONSTELLATION,
    SHIP_CLASS_DEFIANT,
    SHIP_CLASS_GALAXY,          /* Enterprise-D style */
    SHIP_CLASS_SOVEREIGN,       /* Enterprise-E style */
    SHIP_CLASS_INTREPID,        /* Voyager style */
    SHIP_CLASS_AKIRA,           /* Heavy Escort / Carrier */
    SHIP_CLASS_NEBULA,          /* Galaxy-variant with pod */
    SHIP_CLASS_AMBASSADOR,      /* Enterprise-C style */
    SHIP_CLASS_OBERTH,          /* Small Science Ship */
    SHIP_CLASS_STEAMRUNNER,     /* Specialized Escort */
    SHIP_CLASS_GENERIC_ALIEN
} ShipClass;

typedef struct {
    int type;
    char name[64];
    int faction;
    int ship_class;
} PacketLogin;

typedef struct {
    int type;
    char cmd[256];
} PacketCommand;

typedef struct {
    int type;
    char from[64];
    int faction;
    int scope; /* 0: Global, 1: Faction, 2: Private */
    int target_id; /* Player ID (1-based) for Private Message */
    char text[4096];
} PacketMessage;

/* Update Packet: Inviato dal server ai client per aggiornare la Tactical View */
typedef struct {
    int type;
    long long frame_id;
    int q1, q2, q3;
    double s1, s2, s3;
    double ent_h, ent_m;
    int energy;
    int torpedoes;
    int shields[6];
    int lock_target;
    bool is_cloaked;
    int object_count;
    NetObject objects[MAX_NET_OBJECTS];
    int beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    NetPoint torp;
    NetPoint boom;
    NetDismantle dismantle;
} PacketUpdate;

#endif
