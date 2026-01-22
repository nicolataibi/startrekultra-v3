#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include "network.h"

typedef enum {
    NAV_STATE_IDLE = 0,
    NAV_STATE_ALIGN,
    NAV_STATE_WARP,
    NAV_STATE_REALIGN,
    NAV_STATE_IMPULSE
} NavState;

typedef struct {
    int socket;
    char name[64];
    int faction;
    int ship_class;
    int active;
    
    /* Warp State */
    NavState nav_state;
    int nav_timer; /* In frame (20 FPS) */
    double start_h, start_m;
    double target_h, target_m;
    double target_gx, target_gy, target_gz;
    double dx, dy, dz;
    double warp_speed;

    /* Torpedo State */
    bool torp_active;
    double tx, ty, tz;
    double tdx, tdy, tdz;
    StarTrekGame state; 
} ConnectedPlayer;

typedef enum {
    AI_STATE_PATROL = 0,
    AI_STATE_CHASE,
    AI_STATE_FLEE
} AIState;

typedef struct { int id, faction, q1, q2, q3; double x, y, z; int active; } NPCStar;
typedef struct { int id, q1, q2, q3; double x, y, z; int active; } NPCBlackHole;
typedef struct { int id, faction, q1, q2, q3; double x, y, z, h, m; int energy, active; int fire_cooldown; AIState ai_state; int target_player_idx; int nav_timer; double dx, dy, dz; } NPCShip;
typedef struct { int id, q1, q2, q3; double x, y, z; int resource_type, amount, active; } NPCPlanet;
typedef struct { int id, faction, q1, q2, q3; double x, y, z; int health, active; } NPCBase;

#define MAX_NPC 600
#define MAX_PLANETS 400
#define MAX_BASES 100
#define MAX_STARS 1200
#define MAX_BH 100

NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCPlanet planets[MAX_PLANETS];
NPCBase bases[MAX_BASES];
NPCShip npcs[MAX_NPC];
ConnectedPlayer players[MAX_CLIENTS];
StarTrekGame galaxy_master;

void save_galaxy() {
    FILE *f = fopen("galaxy.dat", "wb");
    if (!f) { perror("Failed to open galaxy.dat for writing"); return; }
    fwrite(&galaxy_master, sizeof(StarTrekGame), 1, f);
    fwrite(npcs, sizeof(NPCShip), MAX_NPC, f);
    fwrite(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fwrite(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fwrite(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fwrite(bases, sizeof(NPCBase), MAX_BASES, f);
    fwrite(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    printf("--- GALAXY STATE PERSISTED TO DISK ---\n");
}

int load_galaxy() {
    FILE *f = fopen("galaxy.dat", "rb");
    if (!f) return 0;
    fread(&galaxy_master, sizeof(StarTrekGame), 1, f);
    fread(npcs, sizeof(NPCShip), MAX_NPC, f);
    fread(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fread(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fread(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fread(bases, sizeof(NPCBase), MAX_BASES, f);
    fread(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    
    /* Reset transient network data for loaded players */
    for(int i=0; i<MAX_CLIENTS; i++) {
        players[i].active = 0;
        players[i].socket = 0;
    }
    
    printf("--- PERSISTENT GALAXY LOADED SUCCESSFULLY ---\n");
    return 1;
}

const char* get_species_name(int s) {
    switch(s) {
        case 1: return "Federation"; case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 10: return "Klingon"; case 11: return "Romulan"; case 12: return "Borg";
        case 13: return "Cardassian"; case 14: return "Jem'Hadar"; case 15: return "Tholian";
        case 16: return "Gorn"; case 17: return "Ferengi"; case 18: return "Species 8472";
        case 19: return "Breen"; case 20: return "Hirogen";
        default: return "Unknown";
    }
}

void generate_galaxy() {
    printf("Generating Master Galaxy...\n");
    memset(&galaxy_master, 0, sizeof(StarTrekGame));
    int n_count = 0, b_count = 0, p_count = 0, s_count = 0, bh_count = 0;
    
    for(int i=1; i<=10; i++)
        for(int j=1; j<=10; j++)
            for(int l=1; l<=10; l++) {
                int r = rand()%100;
                int kling = (r > 96) ? 3 : (r > 92) ? 2 : (r > 85) ? 1 : 0;
                int base = (rand()%100 > 98) ? 1 : 0;
                int planets_cnt = (rand()%100 > 90) ? (rand()%2 + 1) : 0;
                int star = (rand()%100 < 40) ? (rand()%3 + 1) : 0;
                int bh = (rand()%100 < 5) ? 1 : 0;
                
                int actual_k = 0, actual_b = 0, actual_p = 0, actual_s = 0, actual_bh = 0;
                
                for(int e=0; e<kling && n_count < MAX_NPC; e++) {
                    npcs[n_count] = (NPCShip){n_count, 10+(rand()%11), i,j,l, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%100)/10.0, 0,0, 1000, 1, 60 + rand()%241, AI_STATE_PATROL, -1, 0, 0,0,0}; n_count++; actual_k++;
                }
                for(int b=0; b<base && b_count < MAX_BASES; b++) {
                    bases[b_count] = (NPCBase){b_count, FACTION_FEDERATION, i,j,l, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%100)/10.0, 5000, 1}; b_count++; actual_b++;
                }
                for(int p=0; p<planets_cnt && p_count < MAX_PLANETS; p++) {
                    planets[p_count] = (NPCPlanet){p_count, i,j,l, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%6)+1, 1000, 1}; p_count++; actual_p++;
                }
                for(int s=0; s<star && s_count < MAX_STARS; s++) {
                    stars_data[s_count] = (NPCStar){s_count, 4, i,j,l, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%100)/10.0, 1}; s_count++; actual_s++;
                }
                for(int h=0; h<bh && bh_count < MAX_BH; h++) {
                    black_holes[bh_count] = (NPCBlackHole){bh_count, i,j,l, (rand()%100)/10.0, (rand()%100)/10.0, (rand()%100)/10.0, 1}; bh_count++; actual_bh++;
                }

                galaxy_master.g[i][j][l] = actual_bh * 10000 + actual_p * 1000 + actual_k * 100 + actual_b * 10 + actual_s;
                galaxy_master.k9 += actual_k;
                galaxy_master.b9 += actual_b;
            }
    printf("Galaxy generated: %d NPCs, %d Stars, %d Planets, %d Bases, %d Black Holes.\n", n_count, s_count, p_count, b_count, bh_count);
}

void broadcast_message(PacketMessage *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (players[i].active) {
        if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
        if (msg->scope == SCOPE_PRIVATE) {
            /* Send to target (ID matches) or sender (echo) */
            /* msg->target_id is 1-based Player ID. players index is 0-based. */
            bool is_target = ((i + 1) == msg->target_id);
            bool is_sender = (strcmp(players[i].name, msg->from) == 0);
            if (!is_target && !is_sender) continue;
        }
        send(players[i].socket, msg, sizeof(PacketMessage), 0);
    }
}

void send_server_msg(int p_idx, const char *from, const char *text) {
    PacketMessage msg = {PKT_MESSAGE, "", 0, 0, 0, ""};
    strncpy(msg.from, from, 63); strncpy(msg.text, text, 4095);
    send(players[p_idx].socket, &msg, sizeof(PacketMessage), 0);
}

void *game_loop(void *arg) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (1) {
        /* 30 FPS Update (approx 33.3ms) */
        ts.tv_nsec += 33333333;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!players[i].active) continue;

            /* Unified Navigation State Machine */
            if (players[i].nav_state == NAV_STATE_ALIGN) {
                players[i].nav_timer--;
                /* Interpolazione rotazione (2 secondi = 60 frame a 30 FPS) */
                double t = 1.0 - (double)players[i].nav_timer / 60.0;
                players[i].state.ent_h = players[i].start_h + (players[i].target_h - players[i].start_h) * t;
                players[i].state.ent_m = players[i].start_m + (players[i].target_m - players[i].start_m) * t;
                
                if (players[i].nav_timer <= 0) {
                    players[i].nav_state = NAV_STATE_WARP;
                    /* Il timer del warp dipende dalla distanza (3s per quadrante = 90 frame per 10 unità) */
                    double dist = sqrt(pow(players[i].target_gx - ((players[i].state.q1-1)*10+players[i].state.s1), 2) + 
                                       pow(players[i].target_gy - ((players[i].state.q2-1)*10+players[i].state.s2), 2) + 
                                       pow(players[i].target_gz - ((players[i].state.q3-1)*10+players[i].state.s3), 2));
                    players[i].nav_timer = (int)(dist / 10.0 * 90.0);
                    if (players[i].nav_timer < 30) players[i].nav_timer = 30; /* Minimo 1 secondo */
                    players[i].warp_speed = dist / players[i].nav_timer;
                    send_server_msg(i, "HELMSMAN", "Entering Warp drive.");
                }
            } 
            else if (players[i].nav_state == NAV_STATE_WARP) {
                players[i].nav_timer--;
                double cur_gx = (players[i].state.q1 - 1) * 10.0 + players[i].state.s1;
                double cur_gy = (players[i].state.q2 - 1) * 10.0 + players[i].state.s2;
                double cur_gz = (players[i].state.q3 - 1) * 10.0 + players[i].state.s3;

                /* Warp Safety Interlock: Proactive collision detection */
                bool emergency_stop = false;
                
                /* Check for Black Holes in current quadrant */
                for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
                    double dx = black_holes[h].x - players[i].state.s1;
                    double dy = black_holes[h].y - players[i].state.s2;
                    double dz = black_holes[h].z - players[i].state.s3;
                    if((dx*dx + dy*dy + dz*dz) < 2.25) { /* 1.5 units safety margin */
                        /* Check if moving AWAY: dot product < 0 */
                        double dot = dx * players[i].dx + dy * players[i].dy + dz * players[i].dz;
                        if (dot > 0) { /* Moving towards or perpendicular */
                            send_server_msg(i, "COMPUTER", "EMERGENCY: Gravitational shear detected. Dropping out of Warp.");
                            emergency_stop = true; break;
                        }
                    }
                }
                /* Check for Stars in current quadrant */
                if (!emergency_stop) {
                    for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
                        double dx = stars_data[s].x - players[i].state.s1;
                        double dy = stars_data[s].y - players[i].state.s2;
                        double dz = stars_data[s].z - players[i].state.s3;
                        if((dx*dx + dy*dy + dz*dz) < 1.44) { /* 1.2 units safety margin */
                            /* Check if moving AWAY */
                            double dot = dx * players[i].dx + dy * players[i].dy + dz * players[i].dz;
                            if (dot > 0) {
                                send_server_msg(i, "COMPUTER", "EMERGENCY: Solar proximity warning. Warp drive disengaged.");
                                emergency_stop = true; break;
                            }
                        }
                    }
                }

                if (emergency_stop) {
                    players[i].nav_state = NAV_STATE_REALIGN;
                    players[i].nav_timer = 30; /* Faster recovery (1s) */
                    players[i].start_h = players[i].state.ent_h;
                    players[i].start_m = players[i].state.ent_m;
                } else {
                    cur_gx += players[i].dx * players[i].warp_speed;
                    cur_gy += players[i].dy * players[i].warp_speed;
                    cur_gz += players[i].dz * players[i].warp_speed;

                    /* Galaxy Boundary Check */
                    bool barrier_hit = false;
                    if (cur_gx < 0) { cur_gx = 0.1; barrier_hit = true; } else if (cur_gx >= 100.0) { cur_gx = 99.9; barrier_hit = true; }
                    if (cur_gy < 0) { cur_gy = 0.1; barrier_hit = true; } else if (cur_gy >= 100.0) { cur_gy = 99.9; barrier_hit = true; }
                    if (cur_gz < 0) { cur_gz = 0.1; barrier_hit = true; } else if (cur_gz >= 100.0) { cur_gz = 99.9; barrier_hit = true; }

                    if (barrier_hit) {
                        send_server_msg(i, "HELMSMAN", "Galactic Barrier reached. Disengaging Warp.");
                        players[i].nav_state = NAV_STATE_REALIGN;
                        players[i].nav_timer = 30;
                    }

                    players[i].state.q1 = (int)(cur_gx / 10.0) + 1;
                    players[i].state.q2 = (int)(cur_gy / 10.0) + 1;
                    players[i].state.q3 = (int)(cur_gz / 10.0) + 1;
                    players[i].state.s1 = fmod(cur_gx, 10.0);
                    players[i].state.s2 = fmod(cur_gy, 10.0);
                    players[i].state.s3 = fmod(cur_gz, 10.0);

                    if (!barrier_hit && players[i].nav_timer <= 0) {
                        players[i].nav_state = NAV_STATE_REALIGN;
                        players[i].nav_timer = 60; /* 2 secondi per tornare a mark 0 */
                        players[i].start_h = players[i].state.ent_h;
                        players[i].start_m = players[i].state.ent_m;
                        send_server_msg(i, "HELMSMAN", "Exiting Warp. Realigning ship.");
                    }
                }
            }
            else if (players[i].nav_state == NAV_STATE_REALIGN) {
                players[i].nav_timer--;
                double t = 1.0 - (double)players[i].nav_timer / 60.0;
                /* Torniamo a Mark 0, Heading rimane invariato */
                players[i].state.ent_m = players[i].start_m * (1.0 - t);
                
                    if (players[i].nav_timer <= 0) {
                        players[i].state.ent_m = 0;
                        players[i].nav_state = NAV_STATE_IDLE;
                        send_server_msg(i, "HELMSMAN", "Stabilized at sub-light speed.");
                    }
            }
            else if (players[i].nav_state == NAV_STATE_IMPULSE) {
                /* Impulse Engine Logic */
                if (players[i].state.energy > 0) {
                    players[i].state.energy -= 1; /* Low consumption */
                    
                    double dx = players[i].dx * players[i].warp_speed; /* Reusing warp_speed var for impulse speed */
                    double dy = players[i].dy * players[i].warp_speed;
                    double dz = players[i].dz * players[i].warp_speed;
                    
                    /* Predict position */
                    double next_s1 = players[i].state.s1 + dx;
                    double next_s2 = players[i].state.s2 + dy;
                    double next_s3 = players[i].state.s3 + dz;
                    
                    /* Boundary Check - Wrap or Stop? Sector 0-10 */
                    /* Save previous state to revert if we hit the wall */
                    int old_q1 = players[i].state.q1, old_q2 = players[i].state.q2, old_q3 = players[i].state.q3;
                    double old_s1 = players[i].state.s1, old_s2 = players[i].state.s2, old_s3 = players[i].state.s3;

                    /* If leaving sector, update quadrant */
                    if (next_s1 >= 10.0) { players[i].state.q1++; next_s1 -= 10.0; }
                    else if (next_s1 < 0.0) { players[i].state.q1--; next_s1 += 10.0; }
                    if (next_s2 >= 10.0) { players[i].state.q2++; next_s2 -= 10.0; }
                    else if (next_s2 < 0.0) { players[i].state.q2--; next_s2 += 10.0; }
                    if (next_s3 >= 10.0) { players[i].state.q3++; next_s3 -= 10.0; }
                    else if (next_s3 < 0.0) { players[i].state.q3--; next_s3 += 10.0; }
                    
                    /* Galaxy Limits Check */
                    if (players[i].state.q1 < 1 || players[i].state.q1 > 10 || 
                        players[i].state.q2 < 1 || players[i].state.q2 > 10 || 
                        players[i].state.q3 < 1 || players[i].state.q3 > 10) {
                        
                        /* Hit the wall - Revert position */
                        players[i].state.q1 = old_q1; players[i].state.q2 = old_q2; players[i].state.q3 = old_q3;
                        players[i].state.s1 = old_s1; players[i].state.s2 = old_s2; players[i].state.s3 = old_s3;
                        
                        send_server_msg(i, "HELMSMAN", "Galactic Barrier reached. Course corrected.");
                        players[i].nav_state = NAV_STATE_IDLE;
                    } else {
                        /* Collision Check (Basic) */
                        bool collision = false;
                        for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
                            double d = sqrt(pow(stars_data[s].x-next_s1,2)+pow(stars_data[s].y-next_s2,2)+pow(stars_data[s].z-next_s3,2));
                            if (d < 0.8) { collision = true; send_server_msg(i, "HELMSMAN", "Collision alert! All stop."); break; }
                        }
                        if (!collision) {
                            players[i].state.s1 = next_s1;
                            players[i].state.s2 = next_s2;
                            players[i].state.s3 = next_s3;
                        } else {
                            players[i].nav_state = NAV_STATE_IDLE;
                        }
                    }
                } else {
                    send_server_msg(i, "ENGINEERING", "Impulse engines offline. Energy depleted.");
                    players[i].nav_state = NAV_STATE_IDLE;
                }
            }
            /* Collisioni e stress ambientali */
            for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
                double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2));
                if(d < 0.8) {
                    send_server_msg(i, "COMPUTER", "CRITICAL: Solar collision detected!");
                    for(int sh=0; sh<6; sh++) players[i].state.shields[sh] = 0;
                    players[i].state.energy -= 1000;
                }
            }
            for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
                double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2));
                if(d < 1.0) {
                    send_server_msg(i, "COMPUTER", "EVENT HORIZON CROSSED. Structural integrity failing.");
                    players[i].active = 0; /* Morte istantanea */
                }
            }

            static int global_tick = 0;
            if (global_tick % 60 == 0) {
                /* Effetti Power Distribution: 0:Engines, 1:Shields, 2:Weapons */
                float p_shields = players[i].state.power_dist[1];

                if (players[i].state.is_cloaked) {
                    players[i].state.energy -= 50; if (players[i].state.energy <= 0) { players[i].state.energy = 0; players[i].state.is_cloaked = false; }
                } else if (players[i].state.energy < 3000) {
                    players[i].state.energy += 10;
                }

                /* Rigenerazione scudi basata su power allocation */
                for(int s=0; s<6; s++) {
                    if (players[i].state.shields[s] < 1000 && players[i].state.energy > 20) {
                        int reg = (int)(15 * p_shields);
                        players[i].state.shields[s] += reg;
                        players[i].state.energy -= reg/2;
                    }
                }

                /* Controllo vittoria globale (Eseguito solo una volta per tick globale) */
                if (i == 0) {
                    int current_k9 = 0;
                    for(int n=0; n<MAX_NPC; n++) if(npcs[n].active) current_k9++;
                    galaxy_master.k9 = current_k9;
                    
                    if (galaxy_master.k9 == 0) {
                        PacketMessage win_msg = {PKT_MESSAGE, "STARFLEET", 0, 0, 0, "\033[1;32mMISSION COMPLETE: All hostile entities neutralized. The galaxy is safe.\033[0m"};
                        broadcast_message(&win_msg);
                    }
                }

                for(int s=0; s<8; s++) if(players[i].state.system_health[s]<100) players[i].state.system_health[s]+=0.1;
            }
            if (i == MAX_CLIENTS - 1) {
                global_tick++;
                /* Auto-save every 60 seconds (1800 ticks at 30 FPS) */
                if (global_tick % 1800 == 0) save_galaxy();
            }

            if (players[i].torp_active) {
                players[i].tx += players[i].tdx * 0.8; players[i].ty += players[i].tdy * 0.8; players[i].tz += players[i].tdz * 0.8;
                players[i].state.torp = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1};
                
                /* Collisione con altri giocatori (Ottimizzata) */
                for (int k=0; k<MAX_CLIENTS; k++) if (k != i && players[k].active && players[k].state.q1 == players[i].state.q1 && players[k].state.q2 == players[i].state.q2 && players[k].state.q3 == players[i].state.q3) {
                    double dx = players[k].state.s1-players[i].tx;
                    double dy = players[k].state.s2-players[i].ty;
                    double dz = players[k].state.s3-players[i].tz;
                    double d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 < 0.25) {
                        players[i].torp_active = false; players[i].state.torp.active = 0;
                        /* Danno agli scudi e travaso su energia/sistemi */
                        int dmg = 500 + rand()%500;
                        for(int s=0; s<6; s++) {
                            players[k].state.shields[s] -= dmg/6;
                            if (players[k].state.shields[s] < 0) {
                                players[k].state.energy += players[k].state.shields[s]; /* Sottrae il residuo */
                                players[k].state.shields[s] = 0;
                                if (rand()%100 > 70) {
                                    int sys = rand()%8; players[k].state.system_health[sys] -= 10.0 + (rand()%20);
                                    if (players[k].state.system_health[sys] < 0) players[k].state.system_health[sys] = 0;
                                    send_server_msg(k, "DAMAGE CONTROL", "Direct hit! System damage reported.");
                                }
                            }
                        }
                        send_server_msg(i, "TACTICAL", "Impact confirmed on player vessel."); 
                        send_server_msg(k, "BRIDGE", "Hull breach! Torpedo impact.");
                    }
                }
                /* Collisione con NPC (Ottimizzata) */
                for (int n=0; n<MAX_NPC; n++) if (npcs[n].active && npcs[n].q1 == players[i].state.q1 && npcs[n].q2 == players[i].state.q2 && npcs[n].q3 == players[i].state.q3) {
                    double dx = npcs[n].x-players[i].tx;
                    double dy = npcs[n].y-players[i].ty;
                    double dz = npcs[n].z-players[i].tz;
                    double d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 < 0.36) {
                        players[i].torp_active = false; players[i].state.torp.active = 0;
                        players[i].state.boom = (NetPoint){(float)npcs[n].x, (float)npcs[n].y, (float)npcs[n].z, 1};
                        npcs[n].energy -= 800;
                        if (npcs[n].energy <= 0) {
                            npcs[n].active = 0;
                            char kill_msg[128]; sprintf(kill_msg, "%s vessel destroyed at [%.1f, %.1f, %.1f].", get_species_name(npcs[n].faction), npcs[n].x, npcs[n].y, npcs[n].z);
                            send_server_msg(i, "TACTICAL", kill_msg);
                            /* Notifica perdita lock globale */
                            int npc_tid = n + 100;
                            for(int p_idx=0; p_idx<MAX_CLIENTS; p_idx++) {
                                if(players[p_idx].active && players[p_idx].state.lock_target == npc_tid) {
                                    players[p_idx].state.lock_target = 0;
                                    send_server_msg(p_idx, "TACTICAL", "Target destroyed. Lock released.");
                                }
                            }
                        } else send_server_msg(i, "TACTICAL", "Target hit.");
                    }
                }
                if (players[i].tx<0||players[i].tx>10||players[i].ty<0||players[i].ty>10||players[i].tz<0||players[i].tz>10) { players[i].torp_active = false; players[i].state.torp.active = 0; }
            }

            /* NPC AI: State Machine & Independent Fire */
            for (int n=0; n<MAX_NPC; n++) if (npcs[n].active && npcs[n].q1 == players[i].state.q1 && npcs[n].q2 == players[i].state.q2 && npcs[n].q3 == players[i].state.q3) {
                
                /* 1. Sensing & State Transitions */
                int closest_player = -1;
                double min_dist2 = 100.0; /* 10 units sensor range */
                
                for(int p=0; p<MAX_CLIENTS; p++) if(players[p].active && players[p].state.q1==npcs[n].q1 && players[p].state.q2==npcs[n].q2 && players[p].state.q3==npcs[n].q3 && !players[p].state.is_cloaked) {
                    double d2 = pow(npcs[n].x - players[p].state.s1, 2) + pow(npcs[n].y - players[p].state.s2, 2) + pow(npcs[n].z - players[p].state.s3, 2);
                    if (d2 < min_dist2) { min_dist2 = d2; closest_player = p; }
                }
                
                if (npcs[n].energy < 200) npcs[n].ai_state = AI_STATE_FLEE;
                else if (closest_player != -1) { npcs[n].ai_state = AI_STATE_CHASE; npcs[n].target_player_idx = closest_player; }
                else npcs[n].ai_state = AI_STATE_PATROL;
                
                /* 2. State-Specific Logic (Movement) */
                if (npcs[n].ai_state == AI_STATE_PATROL) {
                    if (npcs[n].nav_timer <= 0) {
                        npcs[n].nav_timer = 100 + rand()%200;
                        npcs[n].dx = ((rand()%100)-50)/1000.0; /* Slow drift */
                        npcs[n].dy = ((rand()%100)-50)/1000.0;
                        npcs[n].dz = ((rand()%100)-50)/1000.0;
                    }
                } 
                else if (npcs[n].ai_state == AI_STATE_CHASE && npcs[n].target_player_idx != -1) {
                    int p = npcs[n].target_player_idx;
                    double tx = players[p].state.s1, ty = players[p].state.s2, tz = players[p].state.s3;
                    double dxx = tx - npcs[n].x, dyy = ty - npcs[n].y, dzz = tz - npcs[n].z;
                    double d = sqrt(dxx*dxx + dyy*dyy + dzz*dzz);
                    if (d > 1.5) { /* Mantieni una minima distanza tattica */
                        npcs[n].dx = (dxx/d) * 0.03; 
                        npcs[n].dy = (dyy/d) * 0.03;
                        npcs[n].dz = (dzz/d) * 0.03;
                    } else { npcs[n].dx = npcs[n].dy = npcs[n].dz = 0; }
                }
                else if (npcs[n].ai_state == AI_STATE_FLEE && closest_player != -1) {
                    int p = closest_player;
                    double tx = players[p].state.s1, ty = players[p].state.s2, tz = players[p].state.s3;
                    double dxx = npcs[n].x - tx, dyy = npcs[n].y - ty, dzz = npcs[n].z - tz; /* Move AWAY */
                    double d = sqrt(dxx*dxx + dyy*dyy + dzz*dzz);
                    if (d > 0.1) {
                        npcs[n].dx = (dxx/d) * 0.05; 
                        npcs[n].dy = (dyy/d) * 0.05;
                        npcs[n].dz = (dzz/d) * 0.05;
                    }
                }
                
                /* Apply Movement & Sector Limit Check */
                npcs[n].x += npcs[n].dx; npcs[n].y += npcs[n].dy; npcs[n].z += npcs[n].dz;
                npcs[n].nav_timer--;
                if (npcs[n].x < 0.5 || npcs[n].x > 9.5) npcs[n].dx *= -1;
                if (npcs[n].y < 0.5 || npcs[n].y > 9.5) npcs[n].dy *= -1;
                if (npcs[n].z < 0.5 || npcs[n].z > 9.5) npcs[n].dz *= -1;

                /* 3. Fire Logic */
                if (npcs[n].fire_cooldown > 0) npcs[n].fire_cooldown--;
                
                if (npcs[n].fire_cooldown <= 0) {
                    double dx_fire = npcs[n].x-players[i].state.s1;
                    double dy_fire = npcs[n].y-players[i].state.s2;
                    double dz_fire = npcs[n].z-players[i].state.s3;
                    double d2_fire = dx_fire*dx_fire + dy_fire*dy_fire + dz_fire*dz_fire;
                    if (d2_fire < 36.0 && !players[i].state.is_cloaked) { 
                        /* Il nemico spara! */
                        players[i].state.beam_count = 1;
                        players[i].state.beams[0] = (NetBeam){(float)npcs[n].x, (float)npcs[n].y, (float)npcs[n].z, 1};
                        int dmg = (int)(200.0 / sqrt(d2_fire));
                        
                        int damage_remaining = dmg;
                        for(int s=0; s<6; s++) {
                            if (damage_remaining <= 0) break;
                            int absorbed = (players[i].state.shields[s] >= damage_remaining/6) ? damage_remaining/6 : players[i].state.shields[s];
                            players[i].state.shields[s] -= absorbed;
                            if (players[i].state.shields[s] < 0) players[i].state.shields[s] = 0; 
                            damage_remaining -= absorbed;
                        }
                        
                        if (damage_remaining > 0) {
                            players[i].state.energy -= damage_remaining;
                            send_server_msg(i, "DAMAGE CONTROL", "Shields failing! Taking hull damage.");
                            if (players[i].state.energy <= 0) {
                                players[i].state.energy = 0; players[i].active = 0;
                                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                                send_server_msg(i, "COMPUTER", "CRITICAL FAILURE. Ship destroyed.");
                            }
                        } else { send_server_msg(i, "WARNING", "Incoming phaser fire! Shields holding."); }
                        npcs[n].fire_cooldown = 60 + rand()%241;
                    }
                }
            }

            PacketUpdate upd; 
            memset(&upd, 0, sizeof(PacketUpdate));
            upd.type = PKT_UPDATE;
            static long long local_frame_counter = 0;
            upd.frame_id = local_frame_counter++;
            upd.q1 = players[i].state.q1; upd.q2 = players[i].state.q2; upd.q3 = players[i].state.q3;
            upd.s1 = players[i].state.s1; upd.s2 = players[i].state.s2; upd.s3 = players[i].state.s3;
            upd.ent_h = players[i].state.ent_h; upd.ent_m = players[i].state.ent_m;
            upd.energy = players[i].state.energy;
            upd.torpedoes = players[i].state.torpedoes;
            for(int s=0; s<6; s++) upd.shields[s] = players[i].state.shields[s];
            upd.lock_target = players[i].state.lock_target;
            upd.is_cloaked = players[i].state.is_cloaked;
            
            int obj_idx = 0;
            /* Self */
            upd.objects[obj_idx++] = (NetObject){(float)players[i].state.s1,(float)players[i].state.s2,(float)players[i].state.s3,(float)players[i].state.ent_h,(float)players[i].state.ent_m,1,players[i].ship_class,1, 
                                                 (int)((players[i].state.energy / 3000.0) * 100), i+1};
            
            /* Other Players */
            for(int j=0; j<MAX_CLIENTS; j++) if (i!=j && players[j].active && players[j].state.q1==players[i].state.q1 && players[j].state.q2==players[i].state.q2 && players[j].state.q3==players[i].state.q3 && !players[j].state.is_cloaked && obj_idx < MAX_NET_OBJECTS) {
                upd.objects[obj_idx++] = (NetObject){(float)players[j].state.s1,(float)players[j].state.s2,(float)players[j].state.s3,(float)players[j].state.ent_h,(float)players[j].state.ent_m,1,players[j].ship_class,1,
                                                     (int)((players[j].state.energy / 3000.0) * 100), j+1};
            }
            
            /* NPCs */
            for(int n=0; n<MAX_NPC; n++) if(npcs[n].active && npcs[n].q1==players[i].state.q1 && npcs[n].q2==players[i].state.q2 && npcs[n].q3==players[i].state.q3 && obj_idx < MAX_NET_OBJECTS)
                upd.objects[obj_idx++] = (NetObject){(float)npcs[n].x,(float)npcs[n].y,(float)npcs[n].z,0,0,npcs[n].faction,0,1,
                                                     (int)((npcs[n].energy / 1000.0) * 100), n+100};
            
            /* Bases */
            for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3 && obj_idx < MAX_NET_OBJECTS)
                upd.objects[obj_idx++] = (NetObject){(float)bases[b].x,(float)bases[b].y,(float)bases[b].z,0,0,3,0,1,
                                                     (int)((bases[b].health / 5000.0) * 100), b+500};
            
            /* Planets, Stars, Black Holes (No health bar, but ID) */
            for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active && planets[p].q1==players[i].state.q1 && planets[p].q2==players[i].state.q2 && planets[p].q3==players[i].state.q3 && obj_idx < MAX_NET_OBJECTS)
                upd.objects[obj_idx++] = (NetObject){(float)planets[p].x,(float)planets[p].y,(float)planets[p].z,0,0,5,0,1, 0, p+1000};
            for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3 && obj_idx < MAX_NET_OBJECTS)
                upd.objects[obj_idx++] = (NetObject){(float)stars_data[s].x,(float)stars_data[s].y,(float)stars_data[s].z,0,0,4,0,1, 0, s+2000};
            for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3 && obj_idx < MAX_NET_OBJECTS)
                upd.objects[obj_idx++] = (NetObject){(float)black_holes[h].x,(float)black_holes[h].y,(float)black_holes[h].z,0,0,6,0,1, 0, h+3000};
            upd.object_count = obj_idx;
            
            upd.beam_count = players[i].state.beam_count;
            for(int b=0; b<upd.beam_count && b<MAX_NET_BEAMS; b++) upd.beams[b] = players[i].state.beams[b];
            upd.torp = players[i].state.torp;
            upd.boom = players[i].state.boom;
            upd.dismantle = players[i].state.dismantle;

            send(players[i].socket, &upd, sizeof(PacketUpdate), 0);
            
            /* Reset One-Shot Events after sending */
            if (players[i].state.beam_count > 0) players[i].state.beam_count = 0;
            if (players[i].state.boom.active) players[i].state.boom.active = 0;
            if (players[i].state.dismantle.active) players[i].state.dismantle.active = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    int server_fd, new_socket; struct sockaddr_in addr; int opt=1, adlen=sizeof(addr); fd_set fds;
    memset(players, 0, sizeof(players)); srand(time(NULL)); 
    
    if (!load_galaxy()) {
        generate_galaxy();
        save_galaxy();
    }
    
    pthread_t tid; pthread_create(&tid, NULL, game_loop, NULL);
    server_fd = socket(AF_INET, SOCK_STREAM, 0); setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(DEFAULT_PORT);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)); listen(server_fd, 3);
    printf("TREK SERVER started on port %d\n", DEFAULT_PORT);
    while (1) {
        FD_ZERO(&fds); FD_SET(server_fd, &fds); int msd = server_fd;
        for (int i=0; i<MAX_CLIENTS; i++) if (players[i].active) { FD_SET(players[i].socket, &fds); if (players[i].socket > msd) msd = players[i].socket; }
        select(msd+1, &fds, NULL, NULL, NULL);
        if (FD_ISSET(server_fd, &fds)) {
            new_socket = accept(server_fd, (struct sockaddr *)&addr, (socklen_t*)&adlen);
            for (int i=0; i<MAX_CLIENTS; i++) if (!players[i].active) { players[i].socket = new_socket; players[i].active = 1; break; }
        }
        for (int i=0; i<MAX_CLIENTS; i++) if (players[i].active && FD_ISSET(players[i].socket, &fds)) {
            char buf[2048]; int vr = read(players[i].socket, buf, 2048);
            if (vr <= 0) { close(players[i].socket); players[i].active = 0; }
            else {
                int type = *(int*)buf;
                if (type == PKT_LOGIN) {
                    PacketLogin *pkt = (PacketLogin*)buf; 
                    
                    /* Check if player already exists in persistence */
                    int saved_idx = -1;
                    for(int j=0; j<MAX_CLIENTS; j++) {
                        if (strcmp(players[j].name, pkt->name) == 0) { saved_idx = j; break; }
                    }
                    
                    if (saved_idx != -1 && saved_idx != i) {
                        /* Migrate saved state to current slot i */
                        int old_sock = players[i].socket;
                        players[i] = players[saved_idx];
                        players[i].socket = old_sock;
                        players[i].active = 1;
                        /* Clear the old slot to avoid duplicates */
                        memset(&players[saved_idx], 0, sizeof(ConnectedPlayer));
                        send_server_msg(i, "SERVER", "Welcome back, Captain. State restored.");
                    } else {
                        strcpy(players[i].name, pkt->name); players[i].faction = pkt->faction; players[i].ship_class = pkt->ship_class;
                        memset(&players[i].state, 0, sizeof(StarTrekGame)); players[i].state.energy = 3000; players[i].state.torpedoes = 10;
                        players[i].state.q1 = rand()%10 + 1; players[i].state.q2 = rand()%10 + 1; players[i].state.q3 = rand()%10 + 1;
                        players[i].state.s1 = 5.0; players[i].state.s2 = 5.0; players[i].state.s3 = 5.0;
                        for(int s=0; s<8; s++) players[i].state.system_health[s] = 100.0f;
                        send_server_msg(i, "SERVER", "Welcome aboard, new Captain.");
                    }
                    
                    send(players[i].socket, &galaxy_master, sizeof(StarTrekGame), 0);
                } else if (type == PKT_COMMAND) {
                    char *cmd = ((PacketCommand*)buf)->cmd;
                    if (strncmp(cmd, "nav ", 4) == 0) {
                        double h, m, w; if (sscanf(cmd, "nav %lf %lf %lf", &h, &m, &w) == 3) {
                            players[i].target_h = h; players[i].target_m = m;
                            players[i].start_h = players[i].state.ent_h;
                            players[i].start_m = players[i].state.ent_m;
                            
                            double rad_h = h * M_PI / 180.0;
                            double rad_m = m * M_PI / 180.0;
                            players[i].dx = cos(rad_m) * sin(rad_h);
                            players[i].dy = cos(rad_m) * -cos(rad_h);
                            players[i].dz = sin(rad_m);
                            
                            players[i].target_gx = (players[i].state.q1-1)*10.0+players[i].state.s1+players[i].dx*w*10.0;
                            players[i].target_gy = (players[i].state.q2-1)*10.0+players[i].state.s2+players[i].dy*w*10.0;
                            players[i].target_gz = (players[i].state.q3-1)*10.0+players[i].state.s3+players[i].dz*w*10.0;
                            
                            players[i].nav_state = NAV_STATE_ALIGN;
                            players[i].nav_timer = 60; /* 2 secondi di allineamento */
                            send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
                        }
                    } else if (strncmp(cmd, "imp ", 4) == 0) {
                        double h, m, s;
                        if (sscanf(cmd, "imp %lf %lf %lf", &h, &m, &s) == 3) {
                            if (s <= 0.0) {
                                players[i].nav_state = NAV_STATE_IDLE;
                                send_server_msg(i, "HELMSMAN", "Impulse engines All Stop.");
                            } else {
                                if (s > 1.0) s = 1.0;
                                players[i].target_h = h; players[i].target_m = m;
                                players[i].state.ent_h = h; players[i].state.ent_m = m; /* Instant Turn for manual control */
                                
                                double rad_h = h * M_PI / 180.0;
                                double rad_m = m * M_PI / 180.0;
                                players[i].dx = cos(rad_m) * sin(rad_h);
                                players[i].dy = cos(rad_m) * -cos(rad_h);
                                players[i].dz = sin(rad_m);
                                
                                players[i].warp_speed = s * 0.1; /* Max speed 0.1 units/tick */
                                players[i].nav_state = NAV_STATE_IMPULSE;
                                char msg[64]; sprintf(msg, "Impulse engines engaged at %.0f%%.", s*100.0);
                                send_server_msg(i, "HELMSMAN", msg);
                            }
                        }
                    } else if (strcmp(cmd, "srs") == 0) {
                        char b[4096]; 
                        int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
                        double s1 = players[i].state.s1, s2 = players[i].state.s2, s3 = players[i].state.s3;
                        
                        snprintf(b, sizeof(b), "\033[1;36m\n--- SHORT RANGE SENSOR ANALYSIS ---\033[0m\n");
                        snprintf(b+strlen(b), sizeof(b)-strlen(b), "QUADRANT: [%d,%d,%d] | SECTOR: [%.1f,%.1f,%.1f]\n", q1, q2, q3, s1, s2, s3);
                        snprintf(b+strlen(b), sizeof(b)-strlen(b), "ENERGY: %d | TORPEDOES: %d | STATUS: %s\n", 
                                players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? "\033[1;35mCLOAKED\033[0m" : "\033[1;32mNORMAL\033[0m");
                        snprintf(b+strlen(b), sizeof(b)-strlen(b), "\033[1;37mDEFLECTORS:  F:%-4d R:%-4d T:%-4d B:%-4d L:%-4d RI:%-4d\033[0m\n",
                                players[i].state.shields[0], players[i].state.shields[1], players[i].state.shields[2],
                                players[i].state.shields[3], players[i].state.shields[4], players[i].state.shields[5]);
                        strncat(b, "\n\033[1;37mTYPE       ID    POSITION      DIST   H / M         DETAILS\033[0m\n", sizeof(b)-strlen(b)-1);

                        /* Players */
                        for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active && i!=j && players[j].state.q1 == q1 && players[j].state.q2 == q2 && players[j].state.q3 == q3 && !players[j].state.is_cloaked) {
                            double tx=players[j].state.s1, ty=players[j].state.s2, tz=players[j].state.s3;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double h=atan2(dx,-dy)*180/M_PI; if(h<0) h+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s (Player) [E:%d]\n", "Vessel", j+1, tx, ty, tz, dist, h, m, players[j].name, players[j].state.energy); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        /* NPCs */
                        for(int n=0; n<MAX_NPC; n++) if(npcs[n].active && npcs[n].q1 == q1 && npcs[n].q2 == q2 && npcs[n].q3 == q3) {
                            double tx=npcs[n].x, ty=npcs[n].y, tz=npcs[n].z;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double h=atan2(dx,-dy)*180/M_PI; if(h<0) h+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s [E:%d]\n", "Vessel", n+100, tx, ty, tz, dist, h, m, get_species_name(npcs[n].faction), npcs[n].energy); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        /* Bases */
                        for(int bs=0; bs<MAX_BASES; bs++) if(bases[bs].active && bases[bs].q1 == q1 && bases[bs].q2 == q2 && bases[bs].q3 == q3) {
                            double tx=bases[bs].x, ty=bases[bs].y, tz=bases[bs].z;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double h=atan2(dx,-dy)*180/M_PI; if(h<0) h+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Federation Outpost\n", "Starbase", bs+500, tx, ty, tz, dist, h, m); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        /* Planets */
                        for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) {
                            double tx=planets[p].x, ty=planets[p].y, tz=planets[p].z;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double h=atan2(dx,-dy)*180/M_PI; if(h<0) h+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            const char* res[]={"-","Dilithium","Tritanium","Verterium","Monotanium","Isolinear","Gases"};
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Class-M (Res: %s)\n", "Planet", p+1000, tx, ty, tz, dist, h, m, res[planets[p].resource_type]); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        /* Stars */
                        for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1 == q1 && stars_data[s].q2 == q2 && stars_data[s].q3 == q3) {
                            double tx=stars_data[s].x, ty=stars_data[s].y, tz=stars_data[s].z;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double h=atan2(dx,-dy)*180/M_PI; if(h<0) h+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Type-G Main Sequence\n", "Star", s+2000, tx, ty, tz, dist, h, m); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        /* Black Holes */
                        for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1 == q1 && black_holes[h].q2 == q2 && black_holes[h].q3 == q3) {
                            double tx=black_holes[h].x, ty=black_holes[h].y, tz=black_holes[h].z;
                            double dx=tx-s1, dy=ty-s2, dz=tz-s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
                            double hh=atan2(dx,-dy)*180/M_PI; if(hh<0) hh+=360; double m=(dist>0.001)?asin(dz/dist)*180/M_PI:0;
                            char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     \033[1;31mWARN: Gravitational Shear\033[0m\n", "B-Hole", h+3000, tx, ty, tz, dist, hh, m); 
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        strncat(b, "-------------------------------------------------------------------\n", sizeof(b)-strlen(b)-1);
                        send_server_msg(i, "COMPUTER", b);
                    } else if (strcmp(cmd, "lrs") == 0) {
                        char rep[4096] = "\033[1;36m\n--- 3D LONG RANGE SENSOR SCAN ---\n\033[0m";
                        char line[512];
                        int pq1 = players[i].state.q1;
                        int pq2 = players[i].state.q2;
                        int pq3 = players[i].state.q3;
                        double ps1 = players[i].state.s1;
                        double ps2 = players[i].state.s2;
                        double ps3 = players[i].state.s3;

                                                for (int l = pq3 + 1; l >= pq3 - 1; l--) {

                                                    if (l < 1 || l > 10) continue;

                                                    snprintf(line, sizeof(line), "\033[1;37m\n[ DECK Z:%d ]\n\033[0m", l); strncat(rep, line, sizeof(rep)-strlen(rep)-1);

                                                    strncat(rep, "         X-1 (West)               X (Center)               X+1 (East)\n", sizeof(rep)-strlen(rep)-1);

                                                    

                                                    for (int y = pq2 - 1; y <= pq2 + 1; y++) {

                                                        if (y == pq2 - 1) strncat(rep, "Y-1 (N) ", sizeof(rep)-strlen(rep)-1);

                                                        else if (y == pq2) strncat(rep, "Y   (C) ", sizeof(rep)-strlen(rep)-1);

                                                        else strncat(rep, "Y+1 (S) ", sizeof(rep)-strlen(rep)-1);

                                                        

                                                        for (int x = pq1 - 1; x <= pq1 + 1; x++) {

                                                            if (x >= 1 && x <= 10 && y >= 1 && y <= 10) {

                                                                /* Dynamic counts */

                                                                int bh_cnt = 0; for(int h=0;h<MAX_BH;h++) if(black_holes[h].active && black_holes[h].q1==x && black_holes[h].q2==y && black_holes[h].q3==l) bh_cnt++;

                                                                int p_cnt = 0; for(int p=0;p<MAX_PLANETS;p++) if(planets[p].active && planets[p].q1==x && planets[p].q2==y && planets[p].q3==l) p_cnt++;

                                                                int e_cnt = 0; for(int n=0;n<MAX_NPC;n++) if(npcs[n].active && npcs[n].q1==x && npcs[n].q2==y && npcs[n].q3==l) e_cnt++;

                                                                int b_cnt = 0; for(int b=0;b<MAX_BASES;b++) if(bases[b].active && bases[b].q1==x && bases[b].q2==y && bases[b].q3==l) b_cnt++;

                                                                int u_cnt = 0; for(int u=0;u<MAX_CLIENTS;u++) if(players[u].active && players[u].state.q1==x && players[u].state.q2==y && players[u].state.q3==l) u_cnt++;

                                                                int s_cnt_dyn = 0; for(int s=0;s<MAX_STARS;s++) if(stars_data[s].active && stars_data[s].q1==x && stars_data[s].q2==y && stars_data[s].q3==l) s_cnt_dyn++;

                                                                

                                                                int final_val = (bh_cnt > 0 ? 1 : 0)*10000 + p_cnt*1000 + (e_cnt + u_cnt)*100 + b_cnt*10 + s_cnt_dyn;

                        

                                                                /* Accurate Ballistic Heading */

                                                                int h = -1;

                                                                if (y == pq2 - 1) { /* North */

                                                                    if (x == pq1 - 1) h = 315; else if (x == pq1) h = 0; else h = 45;

                                                                } else if (y == pq2) { /* Center */

                                                                    if (x == pq1 - 1) h = 270; else if (x == pq1 + 1) h = 90;

                                                                } else if (y == pq2 + 1) { /* South */

                                                                    if (x == pq1 - 1) h = 225; else if (x == pq1) h = 180; else h = 135;

                                                                }

                        

                                                                double dx_s = (x - pq1) * 10.0 + (5.5 - ps1);

                                                                double dy_s = (pq2 - y) * 10.0 + (ps2 - 5.5);

                                                                double dz_s = (l - pq3) * 10.0 + (5.5 - ps3);

                                                                double dist_s = sqrt(dx_s*dx_s + dy_s*dy_s + dz_s*dz_s);

                                                                double w_req = dist_s / 10.0;

                                                                int m = (dist_s > 0.001) ? (int)(asin(dz_s / dist_s) * 180.0 / M_PI) : 0;

                        

                                                                if (x == pq1 && y == pq2 && l == pq3) {

                                                                    strncat(rep, ":[        \033[1;34mYOU\033[0m         ]: ", sizeof(rep)-strlen(rep)-1);

                                                                } else {

                                                                    snprintf(line, sizeof(line), "[%05d/H%03d/M%+03d/W%.1f]: ", final_val, (h==-1?0:h), m, w_req);

                                                                    strncat(rep, line, sizeof(rep)-strlen(rep)-1);

                                                                }

                                                            } else {

                                                                strncat(rep, "[:        ***         ]: ", sizeof(rep)-strlen(rep)-1);

                                                            }

                                                        }

                                                        strncat(rep, "\n", sizeof(rep)-strlen(rep)-1);

                                                    }

                                                }

                        
                        send_server_msg(i, "SCIENCE", rep);
                    } else if (strncmp(cmd, "pha ", 4) == 0) {
                        int e_fire; if (sscanf(cmd,"pha %d",&e_fire)==1 && players[i].state.energy>=e_fire) {
                            players[i].state.energy-=e_fire; players[i].state.beam_count=1; players[i].state.beams[0].active=1;
                            double tx, ty, tz; int tid = players[i].state.lock_target;
                            bool tid_found = false;
                            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                                tx = players[tid-1].state.s1; ty = players[tid-1].state.s2; tz = players[tid-1].state.s3; tid_found = true;
                            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                                tx = npcs[tid-100].x; ty = npcs[tid-100].y; tz = npcs[tid-100].z; tid_found = true;
                            } else if (tid >= 500 && tid < 500+MAX_BASES && bases[tid-500].active) {
                                tx = bases[tid-500].x; ty = bases[tid-500].y; tz = bases[tid-500].z; tid_found = true;
                            } else if (tid >= 1000 && tid < 1000+MAX_PLANETS && planets[tid-1000].active) {
                                tx = planets[tid-1000].x; ty = planets[tid-1000].y; tz = planets[tid-1000].z; tid_found = true;
                            } else if (tid >= 2000 && tid < 2000+MAX_STARS && stars_data[tid-2000].active) {
                                tx = stars_data[tid-2000].x; ty = stars_data[tid-2000].y; tz = stars_data[tid-2000].z; tid_found = true;
                            } else if (tid >= 3000 && tid < 3000+MAX_BH && black_holes[tid-3000].active) {
                                tx = black_holes[tid-3000].x; ty = black_holes[tid-3000].y; tz = black_holes[tid-3000].z; tid_found = true;
                            }

                            if (tid_found) {
                                /* Targeted fire */
                            } else {
                                tx = players[i].state.s1+cos(players[i].state.ent_m*M_PI/180.0)*sin(players[i].state.ent_h*M_PI/180.0)*5.0;
                                ty = players[i].state.s2+cos(players[i].state.ent_m*M_PI/180.0)*-cos(players[i].state.ent_h*M_PI/180.0)*5.0;
                                tz = players[i].state.s3+sin(players[i].state.ent_m*M_PI/180.0)*5.0;
                            }
                            players[i].state.beams[0].tx=tx; players[i].state.beams[0].ty=ty; players[i].state.beams[0].tz=tz;
                            send_server_msg(i, "TACTICAL", "Phasers fired.");
                            /* Danno Phasers - influenzato dalla potenza assegnata alle armi */
                            double d = sqrt(pow(tx-players[i].state.s1,2)+pow(ty-players[i].state.s2,2)+pow(tz-players[i].state.s3,2));
                            if(d < 0.1) d = 0.1; 
                            float w_boost = 0.5f + players[i].state.power_dist[2]; /* 0.5 to 1.5 multiplier */
                            int hit = (int)((e_fire / d) * w_boost);
                            
                            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                                int damage_remaining = hit;
                                for(int s=0;s<6;s++) {
                                    if (damage_remaining <= 0) break;
                                    int absorbed = (players[tid-1].state.shields[s] >= damage_remaining/6) ? damage_remaining/6 : players[tid-1].state.shields[s];
                                    players[tid-1].state.shields[s] -= absorbed;
                                    damage_remaining -= absorbed;
                                }
                                
                                /* Shield Bleed-through */
                                if (damage_remaining > 0) {
                                    players[tid-1].state.energy -= damage_remaining;
                                    send_server_msg(tid-1, "DAMAGE CONTROL", "Shields penetrated! Structural damage.");
                                    if (rand()%100 > 80) {
                                        int sys = rand()%8;
                                        players[tid-1].state.system_health[sys] -= (damage_remaining / 100.0f);
                                        if (players[tid-1].state.system_health[sys] < 0) players[tid-1].state.system_health[sys] = 0;
                                    }
                                    if (players[tid-1].state.energy <= 0) {
                                        players[tid-1].state.energy = 0;
                                        players[tid-1].active = 0;
                                        players[tid-1].state.boom = (NetPoint){(float)players[tid-1].state.s1, (float)players[tid-1].state.s2, (float)players[tid-1].state.s3, 1};
                                        send_server_msg(tid-1, "COMPUTER", "Critical failure. Ship destroyed.");
                                        send_server_msg(i, "TACTICAL", "Target destroyed.");
                                    }
                                } else {
                                    send_server_msg(tid-1, "BRIDGE", "Shields holding under phaser fire.");
                                }
                            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                                npcs[tid-100].energy -= hit; 
                                if(npcs[tid-100].energy<=0) {
                                    npcs[tid-100].active=0;
                                    players[i].state.boom = (NetPoint){(float)npcs[tid-100].x, (float)npcs[tid-100].y, (float)npcs[tid-100].z, 1};
                                    /* Notifica perdita lock a tutti i giocatori che puntavano questo NPC */
                                    for(int p_idx=0; p_idx<MAX_CLIENTS; p_idx++) {
                                        if(players[p_idx].active && players[p_idx].state.lock_target == tid) {
                                            players[p_idx].state.lock_target = 0;
                                            send_server_msg(p_idx, "TACTICAL", "Target destroyed. Lock released.");
                                        }
                                    }
                                }
                            }
                        }
                    } else if (strncmp(cmd, "tor", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
                        double h,m; bool manual = true;
                        if (players[i].state.lock_target > 0) {
                            int tid = players[i].state.lock_target; double tx, ty, tz;
                            bool tid_found = false;
                            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                                tx = players[tid-1].state.s1; ty = players[tid-1].state.s2; tz = players[tid-1].state.s3; tid_found = true;
                            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                                tx = npcs[tid-100].x; ty = npcs[tid-100].y; tz = npcs[tid-100].z; tid_found = true;
                            } else if (tid >= 500 && tid < 500+MAX_BASES && bases[tid-500].active) {
                                tx = bases[tid-500].x; ty = bases[tid-500].y; tz = bases[tid-500].z; tid_found = true;
                            } else if (tid >= 1000 && tid < 1000+MAX_PLANETS && planets[tid-1000].active) {
                                tx = planets[tid-1000].x; ty = planets[tid-1000].y; tz = planets[tid-1000].z; tid_found = true;
                            } else if (tid >= 2000 && tid < 2000+MAX_STARS && stars_data[tid-2000].active) {
                                tx = stars_data[tid-2000].x; ty = stars_data[tid-2000].y; tz = stars_data[tid-2000].z; tid_found = true;
                            } else if (tid >= 3000 && tid < 3000+MAX_BH && black_holes[tid-3000].active) {
                                tx = black_holes[tid-3000].x; ty = black_holes[tid-3000].y; tz = black_holes[tid-3000].z; tid_found = true;
                            }

                            if (tid_found) {
                                double dx = tx - players[i].state.s1, dy = ty - players[i].state.s2, dz = tz - players[i].state.s3;
                                h = atan2(dx, -dy) * 180.0 / M_PI; if(h<0) h+=360; m = asin(dz/sqrt(dx*dx+dy*dy+dz*dz)) * 180.0 / M_PI;
                                manual = false;
                            }
                        }
                        if (manual && sscanf(cmd,"tor %lf %lf",&h,&m) != 2) manual = false; /* fallthrough */
                        if ((!manual || players[i].state.lock_target > 0) && players[i].state.torpedoes>0) {
                            players[i].state.torpedoes--; players[i].torp_active=true;
                            players[i].tx=players[i].state.s1; players[i].ty=players[i].state.s2; players[i].tz=players[i].state.s3;
                            players[i].tdx=cos(m*M_PI/180.0)*sin(h*M_PI/180.0); players[i].tdy=cos(m*M_PI/180.0)*-cos(h*M_PI/180.0); players[i].tdz=sin(m*M_PI/180.0);
                            send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : "Torpedo away (Lock-on).");
                        }
                    } else if (strncmp(cmd, "she ", 4) == 0) {
                        int f,r,t,b,l,ri; if(sscanf(cmd,"she %d %d %d %d %d %d",&f,&r,&t,&b,&l,&ri)==6) {
                            players[i].state.shields[0]=f; players[i].state.shields[1]=r; players[i].state.shields[2]=t; players[i].state.shields[3]=b;
                            players[i].state.shields[4]=l; players[i].state.shields[5]=ri;
                            send_server_msg(i, "ENGINEERING", "Shields updated (6-axis).");
                        }
                    } else if (strncmp(cmd, "lock", 4) == 0) {
                        int tid = 0; 
                        /* Prova a leggere l'ID, se fallisce tid rimane 0 (release) */
                        sscanf(cmd + 4, "%d", &tid);
                        players[i].state.lock_target = tid;
                        send_server_msg(i, "TACTICAL", tid == 0 ? "Lock released." : "Target locked."); 
                    } else if (strncmp(cmd, "pow ", 4) == 0) {
                        float e,s,w; if(sscanf(cmd,"pow %f %f %f",&e,&s,&w)==3) { players[i].state.power_dist[0]=e; players[i].state.power_dist[1]=s; players[i].state.power_dist[2]=w; send_server_msg(i,"ENGINEERING","Power set."); }
                    } else if (strcmp(cmd, "psy") == 0) {
                        /* Corbomite Bluff logic */
                        bool scared = (rand()%100 > 60);
                        if (scared) {
                            for (int n=0; n<MAX_NPC; n++) if (npcs[n].active && npcs[n].q1 == players[i].state.q1 && npcs[n].q2 == players[i].state.q2 && npcs[n].q3 == players[i].state.q3) {
                                npcs[n].energy = 0; npcs[n].active = 0; /* Surrender or flee */
                            }
                            send_server_msg(i, "COMMUNICATIONS", "Enemy vessel has surrendered after Corbomite bluff.");
                        } else {
                            PacketMessage msg = {PKT_MESSAGE, "", players[i].faction, 0, 0, ""};
                            strncpy(msg.from, players[i].name, 63); strncpy(msg.text, "Corbomite device armed. Surrender now!", 1023);
                            broadcast_message(&msg);
                            send_server_msg(i, "COMMUNICATIONS", "Bluff failed. Enemies remain hostile.");
                        }
                    } else if (strncmp(cmd, "rep ", 4) == 0) {
                        int sid; if(sscanf(cmd,"rep %d",&sid)==1 && sid>=0 && sid<8) {
                            /* Requires materials: Monotanium for hull/engines (0,1,5,7), Isolinear for electronics (2,3,4,6) */
                            bool can_rep = false;
                            if (sid == 0 || sid == 1 || sid == 5 || sid == 7) {
                                if (players[i].state.inventory[4] >= 50) { players[i].state.inventory[4] -= 50; can_rep = true; }
                                else send_server_msg(i, "ENGINEERING", "Insufficient Monotanium for structural repairs.");
                            } else {
                                if (players[i].state.inventory[5] >= 30) { players[i].state.inventory[5] -= 30; can_rep = true; }
                                else send_server_msg(i, "ENGINEERING", "Insufficient Isolinear Crystals for electronic repairs.");
                            }
                            if (can_rep) {
                                players[i].state.system_health[sid] = 100.0f;
                                send_server_msg(i, "ENGINEERING", "Repairs complete using onboard resources.");
                            }
                        }
                    } else if (strncmp(cmd, "con ", 4) == 0) {
                        int t,a; if(sscanf(cmd,"con %d %d",&t,&a)==2 && t>=1 && t<=6 && players[i].state.inventory[t]>=a) {
                            players[i].state.inventory[t]-=a; 
                            if(t==1) players[i].state.energy+=a*10; 
                            else if(t==2) players[i].state.energy+=a*2;
                            else if(t==3) players[i].state.torpedoes+=a/20; 
                            else if(t==6) players[i].state.energy+=a*5; /* Gas to Life Support/Energy */
                            send_server_msg(i,"ENGINEERING","Resource conversion complete.");
                        }
                    } else if (strcmp(cmd, "aux jettison") == 0) {
                        send_server_msg(i, "ENGINEERING", "WARP CORE JETTISONED! Mass energy release!");
                        players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                        players[i].active = 0; /* Suicide */
                    } else if (strcmp(cmd, "clo") == 0) {
                        players[i].state.is_cloaked = !players[i].state.is_cloaked;
                        send_server_msg(i, "ENGINEERING", players[i].state.is_cloaked ? "Cloak active." : "Cloak offline.");
                    } else if (strcmp(cmd, "min") == 0) {
                        int f=0; for(int p=0;p<MAX_PLANETS;p++) if(planets[p].active && planets[p].q1==players[i].state.q1 && planets[p].q2==players[i].state.q2 && planets[p].q3==players[i].state.q3) {
                            double d=sqrt(pow(planets[p].x-players[i].state.s1,2)+pow(planets[p].y-players[i].state.s2,2)+pow(planets[p].z-players[i].state.s3,2));
                            if(d<2.0){ 
                                int ex=(planets[p].amount>100)?100:planets[p].amount; 
                                planets[p].amount-=ex; 
                                players[i].state.inventory[planets[p].resource_type]+=ex; 
                                const char* res_names[]={"-","Dilithium","Tritanium","Verterium","Monotanium","Isolinear","Gases"};
                                char b_msg[128];
                                sprintf(b_msg, "Mining successful. Collected %d units of %s.", ex, res_names[planets[p].resource_type]);
                                send_server_msg(i,"GEOLOGY",b_msg); f=1; break; 
                            }
                        }
                        if(!f) send_server_msg(i,"COMPUTER","No planet in range.");
                    } else if (strncmp(cmd, "con ", 4) == 0) {
                        int t,a; if(sscanf(cmd,"con %d %d",&t,&a)==2 && t>=1 && t<=6 && players[i].state.inventory[t]>=a) {
                            players[i].state.inventory[t]-=a; if(t==1) players[i].state.energy+=a*5; else if(t==3) players[i].state.torpedoes+=a/50; send_server_msg(i,"ENGINEERING","Conversion complete.");
                        }
                    } else if (strncmp(cmd, "rep ", 4) == 0) {
                        int sid; if(sscanf(cmd,"rep %d",&sid)==1 && sid>=0 && sid<8 && players[i].state.energy > 200) {
                            players[i].state.energy -= 200; players[i].state.system_health[sid] = 100.0f;
                            send_server_msg(i, "ENGINEERING", "Repairs complete.");
                        }
                    } else if (strcmp(cmd, "doc") == 0) {
                        bool near = false;
                        for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
                            double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2));
                            if(d<2.0) { near=true; break; }
                        }
                        if(near) {
                            players[i].state.energy = 3000; players[i].state.torpedoes = 10;
                            for(int s=0; s<8; s++) players[i].state.system_health[s] = 100.0f;
                            for(int s=0; s<6; s++) players[i].state.shields[s] = 0;
                            send_server_msg(i, "STARBASE", "Docking complete. Systems restored. Shields lowered.");
                        } else send_server_msg(i, "COMPUTER", "No starbase in range.");
                    } else if (strcmp(cmd, "sco") == 0) {
                        bool near = false;
                        for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
                            double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2));
                            if(d<2.0) { near=true; break; }
                        }
                        if(near) {
                            players[i].state.energy += 500; if(players[i].state.energy > 5000) players[i].state.energy = 5000;
                            int s_idx = rand()%6; players[i].state.shields[s_idx] -= 100; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0;
                            send_server_msg(i, "ENGINEERING", "Solar scooping successful. Collected 500 units of Energy.");
                        } else send_server_msg(i, "COMPUTER", "No star in range for solar scooping.");
                    } else if (strcmp(cmd, "har") == 0) {
                        bool near = false;
                        for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
                            double dx = black_holes[h].x-players[i].state.s1;
                            double dy = black_holes[h].y-players[i].state.s2;
                            double dz = black_holes[h].z-players[i].state.s3;
                            if((dx*dx + dy*dy + dz*dz) < 4.0) { near=true; break; }
                        }
                        if(near) {
                            players[i].state.energy += 1000; if(players[i].state.energy > 5000) players[i].state.energy = 5000;
                            players[i].state.inventory[1] += 50; /* Dilithium */
                            int s_idx = rand()%6; players[i].state.shields[s_idx] -= 300; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0;
                            send_server_msg(i, "ENGINEERING", "Antimatter harvest successful. Collected 1000 Energy and 50 Dilithium.");
                        } else send_server_msg(i, "COMPUTER", "No black hole in range.");
                    } else if (strcmp(cmd, "inv") == 0) {
                        char b[256]="Inv: "; char it[32]; const char* r[]={"-","Dil","Tri","Ver","Mon","Iso","Gas"};
                        for(int j=1;j<=6;j++){sprintf(it,"%s:%d ",r[j],players[i].state.inventory[j]);strcat(b,it);}
                        send_server_msg(i, "LOGISTICS", b);
                    } else if (strcmp(cmd, "sta") == 0) {
                        char b[256]; sprintf(b, "\n--- MISSION STATUS ---\nCommander: %s | Faction: %d | Class: %d\nEnergy: %d | Torps: %d", players[i].name, players[i].faction, players[i].ship_class, players[i].state.energy, players[i].state.torpedoes);
                        send_server_msg(i, "COMPUTER", b);
                    } else if (strcmp(cmd, "dam") == 0) {
                        char b[512]="Integrity: "; char sbuf[64]; const char* sys[]={"Warp","Impulse","Sensors","Transp","Phasers","Torps","Computer","Life"};
                        for(int s=0;s<8;s++){sprintf(sbuf,"%s:%.1f%% ",sys[s],players[i].state.system_health[s]);strcat(b,sbuf);}
                        send_server_msg(i,"ENGINEERING",b);
                    } else if (strncmp(cmd, "apr ", 4) == 0) {
                        int tid; double target_dist;
                        if (sscanf(cmd, "apr %d %lf", &tid, &target_dist) == 2) {
                            double tx, ty, tz; bool found = false;
                            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                                tx = (players[tid-1].state.q1-1)*10+players[tid-1].state.s1;
                                ty = (players[tid-1].state.q2-1)*10+players[tid-1].state.s2;
                                tz = (players[tid-1].state.q3-1)*10+players[tid-1].state.s3;
                                found = true;
                            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                                tx = (npcs[tid-100].q1-1)*10+npcs[tid-100].x;
                                ty = (npcs[tid-100].q2-1)*10+npcs[tid-100].y;
                                tz = (npcs[tid-100].q3-1)*10+npcs[tid-100].z;
                                found = true;
                            } else if (tid >= 500 && tid < 500+MAX_BASES && bases[tid-500].active) {
                                tx = (bases[tid-500].q1-1)*10+bases[tid-500].x;
                                ty = (bases[tid-500].q2-1)*10+bases[tid-500].y;
                                tz = (bases[tid-500].q3-1)*10+bases[tid-500].z;
                                found = true;
                            } else if (tid >= 1000 && tid < 1000+MAX_PLANETS && planets[tid-1000].active) {
                                tx = (planets[tid-1000].q1-1)*10+planets[tid-1000].x;
                                ty = (planets[tid-1000].q2-1)*10+planets[tid-1000].y;
                                tz = (planets[tid-1000].q3-1)*10+planets[tid-1000].z;
                                found = true;
                            } else if (tid >= 2000 && tid < 2000+MAX_STARS && stars_data[tid-2000].active) {
                                tx = (stars_data[tid-2000].q1-1)*10+stars_data[tid-2000].x;
                                ty = (stars_data[tid-2000].q2-1)*10+stars_data[tid-2000].y;
                                tz = (stars_data[tid-2000].q3-1)*10+stars_data[tid-2000].z;
                                found = true;
                            } else if (tid >= 3000 && tid < 3000+MAX_BH && black_holes[tid-3000].active) {
                                tx = (black_holes[tid-3000].q1-1)*10+black_holes[tid-3000].x;
                                ty = (black_holes[tid-3000].q2-1)*10+black_holes[tid-3000].y;
                                tz = (black_holes[tid-3000].q3-1)*10+black_holes[tid-3000].z;
                                found = true;
                            }
                            if (found) {
                                double cur_gx = (players[i].state.q1-1)*10+players[i].state.s1;
                                double cur_gy = (players[i].state.q2-1)*10+players[i].state.s2;
                                double cur_gz = (players[i].state.q3-1)*10+players[i].state.s3;
                                double dx = tx - cur_gx, dy = ty - cur_gy, dz = tz - cur_gz;
                                double d = sqrt(dx*dx + dy*dy + dz*dz);
                                if (d > target_dist) {
                                    double move_d = d - target_dist;
                                    double h = atan2(dx, -dy) * 180.0 / M_PI; if(h<0) h+=360;
                                    double m = asin(dz/d) * 180.0 / M_PI;
                                    players[i].target_h = h; players[i].target_m = m;
                                    players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m;
                                    players[i].dx = dx/d; players[i].dy = dy/d; players[i].dz = dz/d;
                                    players[i].target_gx = cur_gx + players[i].dx * move_d;
                                    players[i].target_gy = cur_gy + players[i].dy * move_d;
                                    players[i].target_gz = cur_gz + players[i].dz * move_d;
                                    players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = 60;
                                    send_server_msg(i, "HELMSMAN", "Autopilot engaged. Approaching target.");
                                } else send_server_msg(i, "COMPUTER", "Already at or within target distance.");
                            } else send_server_msg(i, "COMPUTER", "Target ID not found.");
                        }
                    } else if (strcmp(cmd, "bor") == 0) {
                        int tid = players[i].state.lock_target;
                        if (tid == 0) { send_server_msg(i, "COMPUTER", "No lock-on for boarding."); }
                        else if (players[i].state.system_health[6] < 50.0) { send_server_msg(i, "COMPUTER", "Transporters offline or damaged."); }
                        else {
                            double tx, ty, tz; bool found = false;
                            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                                tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true;
                            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                                tx=npcs[tid-100].x; ty=npcs[tid-100].y; tz=npcs[tid-100].z; found=true;
                            } else if (tid >= 500 && tid < 500+MAX_BASES && bases[tid-500].active) {
                                tx=bases[tid-500].x; ty=bases[tid-500].y; tz=bases[tid-500].z; found=true;
                            } else if (tid >= 1000 && tid < 1000+MAX_PLANETS && planets[tid-1000].active) {
                                tx=planets[tid-1000].x; ty=planets[tid-1000].y; tz=planets[tid-1000].z; found=true;
                            }
                            if (found) {
                                double d = sqrt(pow(tx-players[i].state.s1,2)+pow(ty-players[i].state.s2,2)+pow(tz-players[i].state.s3,2));
                                if (d < 1.0) {
                                    if (rand()%100 > 40) {
                                        players[i].state.energy += 1000; players[i].state.inventory[1] += 100;
                                        send_server_msg(i, "SECURITY", "Boarding successful! Captured: 1000 Energy, 100 Dilithium.");
                                        if (tid >= 100 && tid < 100+MAX_NPC) {
                                            npcs[tid-100].active = 0;
                                            players[i].state.dismantle = (NetDismantle){npcs[tid-100].x, npcs[tid-100].y, npcs[tid-100].z, npcs[tid-100].faction, 1};
                                        }
                                    } else send_server_msg(i, "SECURITY", "Boarding party repelled. Heavy casualties.");
                                } else send_server_msg(i, "COMPUTER", "Target too far for transporters.");
                            }
                        }
                    } else if (strcmp(cmd, "aux computer") == 0) {
                        char b[1024];
                        sprintf(b, "\n--- FEDERATION CENTRAL COMPUTER ---\n"
                                   "Current Mission: Eliminate all hostile entities in the galaxy.\n"
                                   "Hostiles Remaining: %d | Starbases Operational: %d\n"
                                   "Galactic Stability: %.1f%%\n"
                                   "System standard: C23 compliant subspace protocol.", 
                                   galaxy_master.k9, galaxy_master.b9, (1.0 - (float)galaxy_master.k9/200.0)*100.0);
                        send_server_msg(i, "COMPUTER", b);
                    } else if (strncmp(cmd, "aux probe ", 10) == 0) {
                        int qx, qy, qz;
                        if (sscanf(cmd + 10, "%d %d %d", &qx, &qy, &qz) == 3) {
                            if (qx>=1 && qx<=10 && qy>=1 && qy<=10 && qz>=1 && qz<=10) {
                                char b[512]; int val = galaxy_master.g[qx][qy][qz];
                                sprintf(b, "Probe Report Q[%d,%d,%d]: %05d (B:%d P:%d E:%d S:%d T:%d)", qx,qy,qz, val, (val/10000)%10, (val/1000)%10, (val/100)%10, (val/10)%10, val%10);
                                send_server_msg(i, "SCIENCE", b);
                            } else send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
                        }
                    } else if (strcmp(cmd, "xxx") == 0) {
                        send_server_msg(i, "SERVER", "Self-destruct sequence initiated. Goodbye, Captain.");
                        char b_msg[128]; sprintf(b_msg, "Massive explosion detected: Vessel %s has self-destructed.", players[i].name);
                        PacketMessage mpkt = {PKT_MESSAGE, "COMMUNICATIONS", 0, 0, 0, ""};
                        strcpy(mpkt.text, b_msg);
                        broadcast_message(&mpkt);
                        
                        /* Trigger explosion for others in the quadrant */
                        for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active && i!=j && players[j].state.q1==players[i].state.q1 && players[j].state.q2==players[i].state.q2 && players[j].state.q3==players[i].state.q3) {
                            players[j].state.dismantle = (NetDismantle){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1, 1};
                        }
                        players[i].active = 0; close(players[i].socket);
                    } else if (strcmp(cmd, "who") == 0) {
                        char b[4096] = "\033[1;37m\n--- ACTIVE CAPTAINS IN GALAXY ---\033[0m\n";
                        strncat(b, "ID  NAME             FACTION      CLASS           LOCATION      STATUS\n", sizeof(b)-strlen(b)-1);
                        for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) {
                            const char* f_names[] = {"Federation", "Klingon", "Romulan", "Borg", "Cardassian"};
                            const char* c_names[] = {"Constitution", "Miranda", "Excelsior", "Constellation", "Defiant", "Galaxy", "Sovereign", "Intrepid", "Akira", "Nebula", "Ambassador", "Oberth", "Steamrunner", "Generic Alien"};
                            char line[256];
                            snprintf(line, sizeof(line), "%-3d %-16s %-12s %-15s [%d,%d,%d]  %s\n", 
                                j+1, players[j].name, 
                                (players[j].faction >= 0 && players[j].faction < 5) ? f_names[players[j].faction] : "Unknown",
                                (players[j].ship_class >= 0 && players[j].ship_class <= 13) ? c_names[players[j].ship_class] : "Unknown",
                                players[j].state.q1, players[j].state.q2, players[j].state.q3,
                                players[j].state.is_cloaked ? "\033[1;35mCLOAKED\033[0m" : "\033[1;32mONLINE\033[0m");
                            strncat(b, line, sizeof(b)-strlen(b)-1);
                        }
                        strncat(b, "----------------------------------------------------------------------\n", sizeof(b)-strlen(b)-1);
                        send_server_msg(i, "COMPUTER", b);
                    } else if (strncmp(cmd, "cal ", 4) == 0) {
                        int qx,qy,qz; if(sscanf(cmd,"cal %d %d %d",&qx,&qy,&qz)==3) {
                            double dx=(qx-players[i].state.q1)*10.0, dy=(qy-players[i].state.q2)*10.0, dz=(qz-players[i].state.q3)*10.0;
                            double h=atan2(dx,-dy)*180.0/M_PI; if(h<0)h+=360.0; double dist=sqrt(dx*dx+dy*dy+dz*dz); double m=asin(dz/dist)*180.0/M_PI;
                            char b[128]; sprintf(b,"Course to Q[%d,%d,%d]: H:%.1f M:%.1f W:%.2f", qx,qy,qz,h,m,dist/10.0); send_server_msg(i,"COMPUTER",b);
                        }
                    } else {
                        send_server_msg(i, "COMPUTER", "Command unknown or pending implementation.");
                    }
                } else if (type == PKT_MESSAGE) { broadcast_message((PacketMessage*)buf); }
            }
        }
    }
    return 0;
}
