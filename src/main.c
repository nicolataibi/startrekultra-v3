#define _DEFAULT_SOURCE
/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi and Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <locale.h>
#include "ui.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "shared_state.h"
#include "game_state.h"

int shm_fd = -1;
GameState *g_shared_state = NULL;
StarTrekGame game;

/* Aliases to minimize logic changes while centralizing storage */
#define q1 game.q1
#define q2 game.q2
#define q3 game.q3
#define old_q1 game.old_q1
#define old_q2 game.old_q2
#define old_q3 game.old_q3
#define s1 game.s1
#define s2 game.s2
#define s3 game.s3
#define g game.g
#define z game.z
#define k game.k
#define stars_pos game.stars_pos
#define base_pos game.base_pos
#define planet_pos game.planet_pos
#define bh_pos game.bh_pos
#define inventory game.inventory
#define species_counts game.species_counts
#define k3 game.k3
#define b3 game.b3
#define st3 game.st3
#define p3 game.p3
#define bh3 game.bh3
#define k9 game.k9
#define b9 game.b9
#define e game.energy
#define p game.torpedoes
#define crew_count game.crew_count
#define shields game.shields
#define ent_h game.ent_h
#define ent_m game.ent_m
#define lock_target game.lock_target
#define power_dist game.power_dist
#define is_playing_dead game.is_playing_dead
#define show_axes game.show_axes
#define show_grid game.show_grid
#define corbomite_count game.corbomite_count
#define system_health game.system_health
#define life_support game.life_support
#define t game.t
#define t0 game.t0
#define t9 game.t9
#define captain_name game.captain_name

/* Missing Global Variables */
int beam_count = 0;
double beam_targets[MAX_BEAMS][3];
float cur_torp[3] = {-100, -100, -100};
float cur_boom[3] = {-100, -100, -100};
float cur_dismantle[3] = {-100, -100, -100};
int dismantle_species = 0;
bool game_ended = false;

/* Function Prototypes */
void ship_destroyed(void);
void save_game(void);
bool load_game(void);

void save_game(void) {
    if (game_ended) return;
    FILE *f = fopen("startrekultra.sav", "wb");
    if (f) {
        fwrite(&game, sizeof(StarTrekGame), 1, f);
        fclose(f);
    }
}

bool load_game(void) {
    FILE *f = fopen("startrekultra.sav", "rb");
    if (f) {
        size_t read = fread(&game, sizeof(StarTrekGame), 1, f);
        fclose(f);
        return (read == 1);
    }
    return false;
}

void cleanup(void) {
    save_game();
    if (g_shared_state) {
        munmap(g_shared_state, sizeof(GameState));
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
    }
}

void life_support_check(void) {
    if (life_support <= 0) {
        printf("Life support failure! Crew is dying...\n");
        crew_count -= 10;
        if (crew_count < 0) crew_count = 0;
        if (crew_count == 0) ship_destroyed();
    }
}

void init_shared_memory(void) {
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(GameState)) == -1) {
        perror("ftruncate failed");
        exit(1);
    }
    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shared_state == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shared_state->mutex, &attr);
    
    g_shared_state->object_count = 0;
    g_shared_state->beam_count = 0;
    g_shared_state->torp.active = 0;
    g_shared_state->boom.active = 0;
    g_shared_state->dismantle.active = 0;
}
#define MAXLEN 255
#define MAXROW 24
#define MAXCOL 80
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
typedef char line[MAXCOL];
typedef char string[MAXLEN];
/* Prototypes */
void intro(void);
void new_game(bool resume);
void initialize(void);
void new_quadrant(void);
void course_control(void);
void short_range_scan(void);
void long_range_scan(void);
void status_report(void);
void update_3d_view(void);
void safe_gets(char *s, int size);
int get_rand(int iSpread);
const char* get_species_name(int id);
void won_game(void);
void smooth_rotate(double target_h, double target_m);
void damage_report(void);
void inventory_report(void);
void repair_control(void);
void convert_resources(void);
void boarding_control(void);
void navigation_calculator(void);
void approach_control(void);
void scoop_control(void);
void harvest_control(void);
void toggle_grid(void);
void toggle_axes(void);
void help_command(char *arg);
void mine_control(void);

/* Global Variables (True 3D) */
pid_t visualizer_pid = 0;
volatile sig_atomic_t g_visualizer_ready = 0;

void handle_ack(int sig) {
    if (sig == SIGUSR2) g_visualizer_ready = 1;
}

void handle_exit_signal(int sig) {
    exit(0);
}

void wait_for_visualizer(void) {
    if (visualizer_pid <= 0) { usleep(33333); return; }
    int timeout = 0;
    while (!g_visualizer_ready && timeout < 100) {
        usleep(1000);
        timeout++;
    }
    g_visualizer_ready = 0; /* Reset for next handshake */
}
int main(void) {
    memset(&game, 0, sizeof(StarTrekGame));
    old_q1 = -1; old_q2 = -1; old_q3 = -1;
    show_axes = true;
    show_grid = false;
    strcpy(captain_name, "Kirk");

    setlocale(LC_ALL, "C");
    init_shared_memory();
    signal(SIGUSR2, handle_ack);
    signal(SIGINT, handle_exit_signal);
    signal(SIGTERM, handle_exit_signal);
    srand(time(NULL));
    atexit(cleanup);
    
    visualizer_pid = fork();
    if (visualizer_pid == 0) {
        /* Child process */
        execl("./ultra_view", "ultra_view", NULL);
        perror("Failed to launch visualizer");
        exit(1);
    }
    
    /* Wait for visualizer to initialize (up to 5 seconds) */
    printf("Initializing Visual Systems... "); fflush(stdout);
    int startup_timeout = 0;
    while (!g_visualizer_ready && startup_timeout < 5000) {
        usleep(1000);
        startup_timeout++;
    }
    if (!g_visualizer_ready) {
        printf(B_RED "FAILED.\n" RESET "Warning: Visualizer connection timed out. Proceeding in degraded mode.\n");
    } else {
        printf(B_GREEN "READY.\n" RESET);
        g_visualizer_ready = 0; /* Reset for future handshakes */
    }
    
    bool resumed = load_game();
    if (resumed) {
        printf(B_GREEN "\n--- RESTORING PREVIOUS SESSION ---\n" RESET);
        printf("Welcome back, Captain %s.\n\n", captain_name);
        sleep(1);
        new_game(true);
    } else {
        intro();
        new_game(false);
    }
    return 0;
}
void intro(void) {
    CLS;
    printf(B_BLUE "  ____________________________________________________________\n");
    printf(" /                                                            \\\n");
    printf(" |" B_WHITE "    Space: the final frontier. These are the voyages...     " B_BLUE "|\n");
    printf(" |                                                            |\n");
    printf(" |" B_YELLOW "               * * *  " B_RED "STAR TREK ULTRA" B_YELLOW "  * * *                " B_BLUE "|\n");
    printf(" |" B_CYAN "                  3D VOLUME SIMULATION                      " B_BLUE "|\n");
    printf(" |                                                            |\n");
    printf(" |" B_WHITE "    Developed by Nicola Taibi                               " B_BLUE "|\n");
    printf(" |" B_WHITE "    Supported by Google Gemini                              " B_BLUE "|\n");
    printf(" |" B_WHITE "    License: GNU GPL v3.0                                   " B_BLUE "|\n");
    printf(" \\____________________________________________________________/\n" RESET);
    printf("\n");
    printf(B_CYAN "ENTER COMMANDER'S NAME: " RESET);
    safe_gets(captain_name, 64);
    if (captain_name[0] == '\0') strcpy(captain_name, "Kirk");
    printf("\n" B_WHITE "Welcome aboard, Captain %s. The galaxy awaits your command." RESET "\n\n", captain_name);
    sleep(1);
}
void smooth_rotate(double target_h, double target_m) {
    int steps = 60; /* 2 seconds at 30 fps */
    double start_h = ent_h;
    double start_m = ent_m;

    /* Normalize heading difference to take shortest path */
    double diff_h = target_h - start_h;
    while (diff_h > 180.0) diff_h -= 360.0;
    while (diff_h < -180.0) diff_h += 360.0;

    double diff_m = target_m - start_m;

    if (fabs(diff_h) < 0.1 && fabs(diff_m) < 0.1) {
        update_3d_view();
        wait_for_visualizer();
        return;
    }

    printf("Adjusting ship attitude... [" );
    for (int i = 1; i <= steps; i++) {
        ent_h = start_h + diff_h * i / (double)steps;
        ent_m = start_m + diff_m * i / (double)steps;
        
        /* Ensure ent_h stays in 0-359 range */
        if (ent_h < 0) ent_h += 360.0;
        if (ent_h >= 360.0) ent_h -= 360.0;

        update_3d_view();
        wait_for_visualizer();
        if (i % 6 == 0) { printf("."); fflush(stdout); }
    }
    printf("] Confirmed.\n");
}

void initialize(void) {
    int i, j, l;
    k9 = 0; b9 = 0;
    t0 = 4000; t = t0; t9 = 30;
    e = 3000; p = 10;
    crew_count = 430;
    for(i=0; i<6; i++) shields[i] = 0;
    lock_target = 0;
    is_playing_dead = false;
        corbomite_count = 3;
        for(i=0; i<7; i++) inventory[i] = 0;
        for(i=0; i<11; i++) species_counts[i] = 0;
    
        /* Populate 3D Galaxy */
        for(i=1; i<=10; i++)
            for(j=1; j<=10; j++)
                for(l=1; l<=10; l++) {
                    int r = rand()%100;
                    int kling = (r > 96) ? 3 : (r > 92) ? 2 : (r > 85) ? 1 : 0;
                    
                    /* Track species counts across galaxy */
                    for(int c=0; c<kling; c++) {
                        species_counts[rand() % 11]++;
                    }
    
                    int base = (rand()%100 > 97) ? 1 : 0;
    
                int planets = (rand()%100 > 90) ? (rand()%2 + 1) : 0;
                int star = (rand()%100 < 40) ? 1 : 0;
                int blackhole = (rand()%100 < 5) ? 1 : 0;
                /* Encoding: BH*10000 + P*1000 + K*100 + B*10 + S */
                g[i][j][l] = blackhole * 10000 + planets * 1000 + kling * 100 + base * 10 + star;
                k9 += kling; b9 += base;
                z[i][j][l] = 0;
            }
    
    q1 = get_rand(10); q2 = get_rand(10); q3 = get_rand(10);
    s1 = get_rand(10); s2 = get_rand(10); s3 = get_rand(10);
    
    for(int i=0; i<8; i++) system_health[i] = 100.0f;
    system_health[7] = 100.0f; /* Explicit life support health */
    life_support = 100.0f;     /* Explicit air reserves */
    ent_h = 0; ent_m = 0;      /* Initial attitude */
    if (b9 == 0) { g[q1][q2][q3] += 10; b9++; }
}
void new_quadrant(void) {
    int i = 0;
    if (q1 < 1) q1 = 1; 
    if (q1 > 10) q1 = 10;
    if (q2 < 1) q2 = 1; 
    if (q2 > 10) q2 = 10;
    if (q3 < 1) q3 = 1; 
    if (q3 > 10) q3 = 10;
    int val = g[q1][q2][q3];
    bh3 = val / 10000;
    p3 = (val / 1000) % 10;
    k3 = (val / 100) % 10;
    b3 = (val / 10) % 10;
    st3 = val % 10;
    z[q1][q2][q3] = val;
    /* Only randomize positions if we ENTERED a different quadrant */
    if (q1 != old_q1 || q2 != old_q2 || q3 != old_q3) {
        lock_target = 0; /* Reset lock on quadrant change */
        /* Planets first */
        for (i = 0; i < p3; i++) {
            planet_pos[i][0] = get_rand(10);
            planet_pos[i][1] = get_rand(10);
            planet_pos[i][2] = get_rand(10);
            int r_type = rand()%100;
            if (r_type > 90) planet_pos[i][3] = 6;      /* Gases (O2/N2) */
            else if (r_type > 75) planet_pos[i][3] = 5; /* Isolinear */
            else if (r_type > 60) planet_pos[i][3] = 4; /* Monotanium */
            else if (r_type > 45) planet_pos[i][3] = 3; /* Verterium */
            else if (r_type > 20) planet_pos[i][3] = 2; /* Tritanium */
            else planet_pos[i][3] = 1;                  /* Dilithium */
            
            planet_pos[i][4] = (planet_pos[i][3] == 1) ? (800 + rand()%1200) :
                               (planet_pos[i][3] == 2) ? (400 + rand()%600) :
                               (planet_pos[i][3] == 3) ? (100 + rand()%200) : (50 + rand()%100);
        }
        /* Stars next - dist > 5 from planets */
        for(i=0; i<st3; i++) {
            bool valid = false; int attempts = 0;
            while (!valid && attempts < 50) {
                stars_pos[i][0] = get_rand(10); stars_pos[i][1] = get_rand(10); stars_pos[i][2] = get_rand(10);
                valid = true;
                for (int p_idx=0; p_idx<p3; p_idx++) {
                    double dx = stars_pos[i][0]-planet_pos[p_idx][0];
                    double dy = stars_pos[i][1]-planet_pos[p_idx][1];
                    double dz = stars_pos[i][2]-planet_pos[p_idx][2];
                    double d_sq = dx*dx + dy*dy + dz*dz;
                    if (d_sq < 25.0) { valid = false; break; }
                }
                attempts++;
            }
        }
        /* Black Hole - dist > 7 from planets and stars */
        for(i=0; i<bh3; i++) {
            bool valid = false; int attempts = 0;
            while (!valid && attempts < 100) {
                bh_pos[i][0] = get_rand(10); bh_pos[i][1] = get_rand(10); bh_pos[i][2] = get_rand(10);
                valid = true;
                for (int p_idx=0; p_idx<p3; p_idx++) {
                    double dx = bh_pos[i][0]-planet_pos[p_idx][0];
                    double dy = bh_pos[i][1]-planet_pos[p_idx][1];
                    double dz = bh_pos[i][2]-planet_pos[p_idx][2];
                    if ((dx*dx + dy*dy + dz*dz) < 49.0) { valid = false; break; }
                }
                for (int s_idx=0; s_idx<st3; s_idx++) {
                    double dx = bh_pos[i][0]-stars_pos[s_idx][0];
                    double dy = bh_pos[i][1]-stars_pos[s_idx][1];
                    double dz = bh_pos[i][2]-stars_pos[s_idx][2];
                    if ((dx*dx + dy*dy + dz*dz) < 49.0) { valid = false; break; }
                }
                attempts++;
            }
        }
        for(i=1; i<=3; i++) {
            if (i <= k3) {
                k[i][0] = get_rand(10); k[i][1] = get_rand(10); k[i][2] = get_rand(10);
                k[i][3] = 200 + rand()%200;
                
                /* Pick a species that still has remaining count if possible, 
                   otherwise fallback to Klingon (index 0) */
                int s_id = 0;
                int attempts = 0;
                do {
                    s_id = rand() % 11;
                    attempts++;
                } while (species_counts[s_id] <= 0 && attempts < 50);
                
                k[i][4] = 10 + s_id; 
            } else k[i][3] = 0;
        }
        for(i=0; i<b3; i++) {
            base_pos[i][0] = get_rand(10);
            base_pos[i][1] = get_rand(10);
            base_pos[i][2] = get_rand(10);
        }
        old_q1 = q1; old_q2 = q2; old_q3 = q3;
    }
    update_3d_view();
}
void update_3d_view(void) {
    if (!g_shared_state) return;

    pthread_mutex_lock(&g_shared_state->mutex);

    int avg_s = (shields[0] + shields[1] + shields[2] + shields[3] + shields[4] + shields[5]) / 6;
    g_shared_state->shm_energy = e + avg_s;
    for(int s=0; s<6; s++) g_shared_state->shm_shields[s] = shields[s];
    g_shared_state->klingons = k9;
    sprintf(g_shared_state->quadrant, "Q-%d-%d-%d", q1, q2, q3);
    g_shared_state->shm_show_axes = show_axes;
    g_shared_state->shm_show_grid = show_grid;

    /* Reset object list */
    g_shared_state->object_count = 0;
    int idx = 0;

    /* Player (Type 1) */
    g_shared_state->objects[idx].shm_x = s1;
    g_shared_state->objects[idx].shm_y = s2;
    g_shared_state->objects[idx].shm_z = s3;
    g_shared_state->objects[idx].type = 1;
    g_shared_state->objects[idx].h = (float)ent_h;
    g_shared_state->objects[idx].m = (float)ent_m;
    g_shared_state->objects[idx].active = 1;
    idx++;

    /* Active Beams (Phasers) */
    g_shared_state->beam_count = beam_count;
    for (int i=0; i<beam_count; i++) {
        g_shared_state->beams[i].shm_tx = (float)beam_targets[i][0];
        g_shared_state->beams[i].shm_ty = (float)beam_targets[i][1];
        g_shared_state->beams[i].shm_tz = (float)beam_targets[i][2];
        g_shared_state->beams[i].active = 1;
    }
    beam_count = 0; /* Clear logic side */

    /* Torpedo */
    if (cur_torp[0] > -10) {
        g_shared_state->torp.shm_x = cur_torp[0];
        g_shared_state->torp.shm_y = cur_torp[1];
        g_shared_state->torp.shm_z = cur_torp[2];
        g_shared_state->torp.active = 1;
    } else {
        g_shared_state->torp.active = 0;
    }

    /* Explosion (Boom) - Pulse once */
    if (cur_boom[0] > -10) {
        g_shared_state->boom.shm_x = cur_boom[0];
        g_shared_state->boom.shm_y = cur_boom[1];
        g_shared_state->boom.shm_z = cur_boom[2];
        g_shared_state->boom.active = 1; /* View will pick this up and start animation */
        cur_boom[0] = -100; /* Reset local trigger */
    } else {
        g_shared_state->boom.active = 0;
    }

    /* Dismantle - Pulse once */
    if (cur_dismantle[0] > -10) {
        g_shared_state->dismantle.shm_x = cur_dismantle[0];
        g_shared_state->dismantle.shm_y = cur_dismantle[1];
        g_shared_state->dismantle.shm_z = cur_dismantle[2];
        g_shared_state->dismantle.species = dismantle_species;
        g_shared_state->dismantle.active = 1;
        cur_dismantle[0] = -100;
    } else {
        g_shared_state->dismantle.active = 0;
    }

    /* Enemies (Type 10+) */
    for(int i=1; i<=3; i++) {
        if (k[i][3] > 0 && idx < MAX_OBJECTS) {
            g_shared_state->objects[idx].shm_x = k[i][0];
            g_shared_state->objects[idx].shm_y = k[i][1];
            g_shared_state->objects[idx].shm_z = k[i][2];
            g_shared_state->objects[idx].type = k[i][4];
            g_shared_state->objects[idx].active = 1;
            idx++;
        }
    }
    
    /* Bases (Type 3) */
    for(int i=0; i<b3; i++) {
        if (idx < MAX_OBJECTS) {
            g_shared_state->objects[idx].shm_x = base_pos[i][0];
            g_shared_state->objects[idx].shm_y = base_pos[i][1];
            g_shared_state->objects[idx].shm_z = base_pos[i][2];
            g_shared_state->objects[idx].type = 3;
            g_shared_state->objects[idx].active = 1;
            idx++;
        }
    }

    /* Planets (Type 5) */
    for(int i=0; i<p3; i++) {
        if (idx < MAX_OBJECTS) {
            g_shared_state->objects[idx].shm_x = planet_pos[i][0];
            g_shared_state->objects[idx].shm_y = planet_pos[i][1];
            g_shared_state->objects[idx].shm_z = planet_pos[i][2];
            g_shared_state->objects[idx].type = 5;
            g_shared_state->objects[idx].active = 1;
            idx++;
        }
    }

    /* Black Holes (Type 6) */
    for(int i=0; i<bh3; i++) {
        if (idx < MAX_OBJECTS) {
            g_shared_state->objects[idx].shm_x = bh_pos[i][0];
            g_shared_state->objects[idx].shm_y = bh_pos[i][1];
            g_shared_state->objects[idx].shm_z = bh_pos[i][2];
            g_shared_state->objects[idx].type = 6;
            g_shared_state->objects[idx].active = 1;
            idx++;
        }
    }

    /* Stars (Type 4) */
    for(int i=0; i<st3; i++) {
        if (idx < MAX_OBJECTS) {
            g_shared_state->objects[idx].shm_x = stars_pos[i][0];
            g_shared_state->objects[idx].shm_y = stars_pos[i][1];
            g_shared_state->objects[idx].shm_z = stars_pos[i][2];
            g_shared_state->objects[idx].type = 4;
            g_shared_state->objects[idx].active = 1;
            idx++;
        }
    }

    g_shared_state->object_count = idx;
    g_shared_state->frame_id++;

    pthread_mutex_unlock(&g_shared_state->mutex);
    
    if (visualizer_pid > 0) kill(visualizer_pid, SIGUSR1);
}

void course_control(void) {

    char buf[MAXLEN];

    double heading, mark, warp;

    

    printf(B_CYAN "\n--- 3D NAVIGATIONAL HELM ---\n" RESET);

    printf("HEADING (0-359): "); safe_gets(buf, MAXLEN); heading = atof(buf);

    printf("MARK (-90 to +90): "); safe_gets(buf, MAXLEN); mark = atof(buf);

    printf("WARP FACTOR (0-8): "); safe_gets(buf, MAXLEN); warp = atof(buf);

    double dist = warp * 10.0;

    if (e < dist) { printf("Insufficient energy!\n"); return; }

    

    e -= (int)dist;

    smooth_rotate(heading, mark);

    

    /* Calculate final absolute target position */

    double rad_h = heading * M_PI / 180.0;

    double rad_m = mark * M_PI / 180.0;

    double dx = cos(rad_m) * sin(rad_h);

    double dy = cos(rad_m) * -cos(rad_h);

    double dz = sin(rad_m);



    double current_gx = (q1 - 1) * 10.0 + s1;

    double current_gy = (q2 - 1) * 10.0 + s2;

    double current_gz = (q3 - 1) * 10.0 + s3;

    

    double target_gx = current_gx + dx * dist;

    double target_gy = current_gy + dy * dist;

    double target_gz = current_gz + dz * dist;



    /* Clamp target within galactic bounds */

    if (target_gx < 1.0) target_gx = 1.0; 

    if (target_gx >= 101.0) target_gx = 100.9;

    if (target_gy < 1.0) target_gy = 1.0; 

    if (target_gy >= 101.0) target_gy = 100.9;

    if (target_gz < 1.0) target_gz = 1.0; 

    if (target_gz >= 101.0) target_gz = 100.9;



    printf(B_WHITE "\n--- ASTROMETRIC CALCULATION REPORT ---\n" RESET);

    printf(" ORIGIN (Galactic): [%.2f, %.2f, %.2f]\n", current_gx, current_gy, current_gz);

    printf(" TARGET (Galactic): [%.2f, %.2f, %.2f]\n", target_gx, target_gy, target_gz);

    printf(" DISPLACEMENT VEC:  [dx:%.3f, dy:%.3f, dz:%.3f]\n", dx * dist, dy * dist, dz * dist);

    printf(" TRIGONOMETRY:      [H:%.1f deg, M:%.1f deg, Dist:%.2f]\n", heading, mark, dist);

    printf(" TRAVEL TIME:       %.1f seconds (3s/Quadrant)\n", warp * 3.0);

    printf(" STATUS:            Trajectory verified. Engaging Warp drive...\n\n");



    int steps = (int)(warp * 3.0 * 30.0);

    if (steps < 30) steps = 30;

    

    printf("Warp jump engaged! [" );

    for (int i=1; i<=steps; i++) {

        double gx = current_gx + (target_gx - current_gx) * i / (double)steps;

        double gy = current_gy + (target_gy - current_gy) * i / (double)steps;

        double gz = current_gz + (target_gz - current_gz) * i / (double)steps;

        

        bool boundary = false;

        if (gx < 1.0) { gx = 1.0; boundary = true; } if (gx >= 101.0) { gx = 100.9; boundary = true; }

        if (gy < 1.0) { gy = 1.0; boundary = true; } if (gy >= 101.0) { gy = 100.9; boundary = true; }

        if (gz < 1.0) { gz = 1.0; boundary = true; } if (gz >= 101.0) { gz = 100.9; boundary = true; }

        if (boundary && i % 30 == 0) printf(B_RED "\n[ALERT] GALACTIC BOUNDARY REACHED. Safety clamp active.\n" RESET);



                int nq1 = (int)((gx - 0.001) / 10.0) + 1;



                int nq2 = (int)((gy - 0.001) / 10.0) + 1;



                int nq3 = (int)((gz - 0.001) / 10.0) + 1;



        



                if (nq1 < 1) nq1 = 1; 



                if (nq1 > 10) nq1 = 10;



                if (nq2 < 1) nq2 = 1; 



                if (nq2 > 10) nq2 = 10;



                if (nq3 < 1) nq3 = 1; 



                if (nq3 > 10) nq3 = 10;



        



                

        

        s1 = fmod(gx, 10.0); if (s1 == 0 && gx > 0) s1 = 10.0;

        s2 = fmod(gy, 10.0); if (s2 == 0 && gy > 0) s2 = 10.0;

        s3 = fmod(gz, 10.0); if (s3 == 0 && gz > 0) s3 = 10.0;



        if (nq1 != q1 || nq2 != q2 || nq3 != q3) {

            q1 = nq1; q2 = nq2; q3 = nq3;

            new_quadrant();

        }

        update_3d_view();

        wait_for_visualizer();

        if (i % 6 == 0) { printf("."); fflush(stdout); }

    }

    printf("] Arrived.\n");

    printf("Stabilizing flight attitude (Horizontal)...\n");

    smooth_rotate(ent_h, 0);

    t += (warp < 1.0) ? warp : 1.0;

    new_quadrant();

    short_range_scan();

}


const char* get_species_name(int id) {
    switch(id) {
        case 10: return "Klingon";
        case 11: return "Romulan";
        case 12: return "Borg";
        case 13: return "Cardassian";
        case 14: return "Jem'Hadar";
        case 15: return "Tholian";
        case 16: return "Gorn";
        case 17: return "Ferengi";
        case 18: return "Species 8472";
        case 19: return "Breen";
        case 20: return "Hirogen";
        default: return "Unknown";
    }
}

void short_range_scan(void) {
    printf(B_BLUE "\n--- SENSOR READOUT (3D) ---\n" RESET);
    printf("Position: Quadrant [%d,%d,%d] Sector [%.1f, %.1f, %.1f]\n", q1,q2,q3,s1,s2,s3);
    printf("Energy: %d  Torpedoes: %d\n", e, p);
    printf("\033[1;37m--- DEFLECTOR STATUS ---\033[0m\n");
    printf(" FRONT: %-5d  REAR:  %-5d  TOP:    %-5d\n", shields[0], shields[1], shields[2]);
    printf(" LEFT:  %-5d  RIGHT: %-5d  BOTTOM: %-5d\n", shields[4], shields[5], shields[3]);
    printf("Condition: %s  Enemies in Galaxy: %d  Lock: %s\n", (k3>0)?B_RED"*RED*":B_GREEN"GREEN", k9, (lock_target==0?"NONE":get_species_name(k[lock_target][4])));
    
    if (k3 > 0) {
        printf(B_YELLOW "\n--- TACTICAL TARGETING ---\n" RESET);
        for (int i = 1; i <= 3; i++) {
            if (k[i][3] > 0) {
                double dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
                printf("ENEMY %d: %s at [%d, %d, %d] | Dist: %.2f | Power: %d\n", i, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2], dist, k[i][3]);
            }
        }
    }
    if (b3 > 0) {
        printf(B_GREEN "\n--- STARBASE PROXIMITY ---\n" RESET);
        for (int i = 0; i < b3; i++) {
            double dist = sqrt(pow(base_pos[i][0]-s1, 2) + pow(base_pos[i][1]-s2, 2) + pow(base_pos[i][2]-s3, 2));
            printf("BASE %d: Located at [%d, %d, %d] | Dist: %.2f %s\n", i+1, base_pos[i][0], base_pos[i][1], base_pos[i][2], dist, (dist < 2.0 ? "(READY FOR DOCK)" : ""));
        }
    }
    if (p3 > 0) {
        printf(B_CYAN "\n--- PLANETARY SURVEY & GEOLOGICAL ANALYSIS ---\n" RESET);
        for (int i = 0; i < p3; i++) {
            double dist = sqrt(pow(planet_pos[i][0]-s1, 2) + pow(planet_pos[i][1]-s2, 2) + pow(planet_pos[i][2]-s3, 2));
            char r_name[32], r_detail[128];
            if (planet_pos[i][3] == 1) {
                strcpy(r_name, "Dilithium Crystals");
                sprintf(r_detail, "High-purity matrix. Yield: Energy.");
            } else if (planet_pos[i][3] == 2) {
                strcpy(r_name, "Tritanium Ore");
                sprintf(r_detail, "High-density metallic strata. Yield: Hull/Shield material.");
            } else if (planet_pos[i][3] == 3) {
                strcpy(r_name, "Verterium-Nitrate");
                sprintf(r_detail, "Tactical chemical compound. Yield: Torpedoes.");
            } else if (planet_pos[i][3] == 4) {
                strcpy(r_name, "Monotanium Alloy");
                sprintf(r_detail, "Heavy structural composite. Yield: Structural repairs.");
            } else if (planet_pos[i][3] == 5) {
                strcpy(r_name, "Isolinear Crystals");
                sprintf(r_detail, "Rare computational lattice. Yield: Electronic repairs.");
            } else if (planet_pos[i][3] == 6) {
                strcpy(r_name, "Atmospheric Gases");
                sprintf(r_detail, "Oxygen/Nitrogen mixture. Yield: Air replenishment.");
            } else {
                strcpy(r_name, "Depleted");
                strcpy(r_detail, "No usable mineral signatures remaining.");
            }
            
            printf("PLANET %d: at [%d, %d, %d] | Resources: %s\n", i+1, planet_pos[i][0], planet_pos[i][1], planet_pos[i][2], r_name);
            printf("         Analysis: %s\n", r_detail);
            printf("         Distance: %.2f %s\n", dist, (dist < 2.0 && planet_pos[i][3] > 0 ? B_GREEN "(ORBITAL LOCK - READY)" RESET : ""));
        }
    }
    if (bh3 > 0) {
        printf(B_RED "\n--- BLACK HOLE SINGULARITY DETECTED ---\n" RESET);
        for (int i = 0; i < bh3; i++) {
            double dist = sqrt(pow(bh_pos[i][0]-s1, 2) + pow(bh_pos[i][1]-s2, 2) + pow(bh_pos[i][2]-s3, 2));
            printf("SINGULARITY: at [%d, %d, %d] | Class: Kerr-Newman | Dist: %.2f %s\n", 
                bh_pos[i][0], bh_pos[i][1], bh_pos[i][2], dist, 
                (dist < 1.5 ? B_RED "(EXTREME DANGER - HARVESTING POSSIBLE)" RESET : ""));
        }
    }
    if (st3 > 0) {
        printf(B_YELLOW "\n--- STELLAR BODIES ---\n" RESET);
        for (int i = 0; i < st3; i++) {
            double dist = sqrt(pow(stars_pos[i][0]-s1, 2) + pow(stars_pos[i][1]-s2, 2) + pow(stars_pos[i][2]-s3, 2));
            printf("STAR %d: at [%d, %d, %d] | Class: G-Type Yellow | Dist: %.2f %s\n", 
                i+1, stars_pos[i][0], stars_pos[i][1], stars_pos[i][2], dist, 
                (dist < 1.2 ? B_RED "(CORONA ENTRY - READY FOR SCOOP)" RESET : ""));
        }
    }
    printf("\n");
    update_3d_view();
}
         
void long_range_scan(void) {
    int i, j, l;
    printf(B_CYAN "\n--- 3D LONG RANGE SENSOR SCAN ---\n" RESET);
    for (l = q3 + 1; l >= q3 - 1; l--) {
        if (l < 1 || l > 10) continue;
        
        printf(B_WHITE "\n[ DECK Z:%d ]\n" RESET, l);
        printf("         X-1 (West)               X (Center)               X+1 (East)\n");
        
        for (i = q2 - 1; i <= q2 + 1; i++) {
            if (i == q2 - 1) printf("Y-1 (N) ");
            else if (i == q2) printf("Y   (C) ");
            else printf("Y+1 (S) ");
            for (j = q1 - 1; j <= q1 + 1; j++) {
                if (i >= 1 && i <= 10 && j >= 1 && j <= 10) {
                    z[j][i][l] = g[j][i][l];
                    
                    /* Accurate Ballistic Heading for this cell (Standard Compass: 0=N, 90=E, 180=S, 270=W) */
                    int h = -1;
                    if (i == q2 - 1) { /* North */
                        if (j == q1 - 1) h = 315; else if (j == q1) h = 0; else h = 45;
                    } else if (i == q2) { /* Center */
                        if (j == q1 - 1) h = 270; else if (j == q1 + 1) h = 90;
                    } else if (i == q2 + 1) { /* South */
                        if (j == q1 - 1) h = 225; else if (j == q1) h = 180; else h = 135;
                    }
                    /* Dynamic Mark and Warp Calculation */
                    double dx_s = (j - q1) * 10.0 + (5.5 - s1);
                    double dy_s = (q2 - i) * 10.0 + (s2 - 5.5); /* Inverted Y for North=0 */
                                        double dz_s = (l - q3) * 10.0 + (5.5 - s3);
                                        double dist_s = sqrt(dx_s*dx_s + dy_s*dy_s + dz_s*dz_s);
                                        double w_req = dist_s / 10.0;
                                        
                                        /* Exact Mark calculation */
                                        int m = 0;
                                        if (dist_s > 0) {
                                            m = (int)(asin(dz_s / dist_s) * 180.0 / M_PI);
                                        }
                    
                                        if (j == q1 && i == q2 && l == q3) {
                    
                        printf(":[        " B_BLUE "YOU" RESET "         ]: ");
                    } else if (h != -1 || l != q3) {
                        printf("[%05d/H%03d/M%+03d/W%.1f]: ", g[j][i][l], (h==-1?0:h), m, w_req); 
                    }
                } else {
                    printf("[:        ***         ]: ");
                }
            }
            printf("\n");
        }
    }
}
void phaser_control(void) {
    char buf[MAXLEN];
    int i, energy_to_fire, target_idx = 0;
        if (k3 <= 0) { printf("No enemies in range.\n"); return; }
        printf(B_YELLOW "\n--- 3D PHASER CONTROL ---\n" RESET);
                                    if (lock_target > 0 && k[lock_target][3] > 0) {
                                        printf("Using lock on %s %d.\n", get_species_name(k[lock_target][4]), lock_target);
                                        target_idx = lock_target;
                                    } else {
                            
                                    printf("Enemies available:\n");
                for (i = 1; i <= 3; i++) {
                    if (k[i][3] > 0) printf("%d: %s at [%d,%d,%d] (Power: %d)\n", i, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2], k[i][3]);
                }
                printf("Enter target number (1-3) or 0 for ALL: ");
                safe_gets(buf, MAXLEN); target_idx = atoi(buf);
            }
        
        printf("Energy available: %d. Units to fire: ", e);
    
    safe_gets(buf, MAXLEN); energy_to_fire = atoi(buf);
    
    if (energy_to_fire <= 0 || energy_to_fire > e) { printf("Invalid energy levels.\n"); return; }
    e -= energy_to_fire;
    int num_targets = 0;
    if (target_idx >= 1 && target_idx <= 3 && k[target_idx][3] > 0) num_targets = 1;
    else { target_idx = 0; num_targets = k3; }
    beam_count = 0;
    for (i = 1; i <= 3; i++) {
        if (k[i][3] > 0 && (target_idx == 0 || target_idx == i)) {
            /* 3D Distance Calculation */
            double dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
            if (dist < 0.1) dist = 0.1; /* Prevent division by zero */
            int hit = (int)((energy_to_fire / num_targets) / dist * (1.0 + (rand()%10)/10.0));
            
                        k[i][3] -= hit;
            
                        printf("Hit on %s at [%d,%d,%d]: %d units. ", get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2], hit);
            
                        if (k[i][3] > 0) printf("(Remaining Power: %d)\n", k[i][3]);
            
                        else printf("\n");
            
                        
            
                        /* Record for Visualizer */
            
                        beam_targets[beam_count][0] = k[i][0];
            
                        beam_targets[beam_count][1] = k[i][1];
            
                        beam_targets[beam_count][2] = k[i][2];
            
                        beam_count++;
            
            
            
                                    if (k[i][3] <= 0) {
            
                                                                    printf(B_GREEN "*** %s DESTROYED ***\n" RESET, get_species_name(k[i][4]));
            
                                                                    species_counts[k[i][4] - 10]--;
            
                                                                    if (lock_target == i) {
            
                                        
            
                                            printf(B_CYAN "COMPUTER: Target lost. Lock released.\n" RESET);
            
                                            lock_target = 0;
            
                                        }
            
            
                            /* Trigger Explosion in Visualizer */
                            cur_boom[0] = k[i][0]; cur_boom[1] = k[i][1]; cur_boom[2] = k[i][2];
                            
                            k3--; k9--;
                            g[q1][q2][q3] -= 100;
                        }
                    }
                }
                update_3d_view();
                usleep(100000); /* 100ms delay to ensure visualizer sees the beams/boom */
                update_3d_view(); /* Clear transient signals */
            }
            
void torpedo_control(void) {
    char buf[MAXLEN];
    double h, m;
    if (p <= 0) { printf("No torpedoes left!\n"); return; }
    printf(B_RED "\n--- 3D PHOTON TORPEDO LAUNCHER ---\n" RESET);
    if (lock_target > 0 && k[lock_target][3] > 0) {
        float tx_in = (float)k[lock_target][0];
        float ty_in = (float)k[lock_target][1];
        float tz_in = (float)k[lock_target][2];
        printf("Using lock on %s %d at [%.1f, %.1f, %.1f].\n", get_species_name(k[lock_target][4]), lock_target, tx_in, ty_in, tz_in);
        double dx = tx_in - s1;
        double dy = ty_in - s2;
        double dz = tz_in - s3;
        h = atan2(dx, -dy) * 180.0 / M_PI;
        if (h < 0) h += 360.0;
        double dist_bal = sqrt(dx*dx + dy*dy + dz*dz);
        m = asin(dz / dist_bal) * 180.0 / M_PI;
    } else {
        printf("Enter target Sector X,Y,Z (e.g. 5,7,3) or press ENTER for manual: ");
        safe_gets(buf, MAXLEN);
        if (buf[0] != '\0') {
            float tx_in, ty_in, tz_in;
            if (sscanf(buf, "%f,%f,%f", &tx_in, &ty_in, &tz_in) == 3 || sscanf(buf, "%f %f %f", &tx_in, &ty_in, &tz_in) == 3) {
                double dx = tx_in - s1;
                double dy = ty_in - s2;
                double dz = tz_in - s3;
                double horizontal_dist = sqrt(dx*dx + dy*dy);
                h = atan2(dx, -dy) * 180.0 / M_PI;
                if (h < 0) h += 360.0;
                m = atan2(dz, horizontal_dist) * 180.0 / M_PI;
                printf(B_CYAN "COMPUTER: Ballistic solution calculated.\n" RESET);
                printf("TARGET: [%.1f, %.1f, %.1f] -> HEADING: %.1f, MARK: %.1f\n", tx_in, ty_in, tz_in, h, m);
            } else {
                printf("Invalid input. Reverting to manual control.\n");
                printf("Heading (0-359): "); safe_gets(buf, MAXLEN); h = atof(buf);
                printf("Mark (-90 to +90): "); safe_gets(buf, MAXLEN); m = atof(buf);
            }
        } else {
            printf("Heading (0-359): "); safe_gets(buf, MAXLEN); h = atof(buf);
            printf("Mark (-90 to +90): "); safe_gets(buf, MAXLEN); m = atof(buf);
        }
    }
    p--; e -= 10;
    double rad_h = h * M_PI / 180.0;
    double rad_m = m * M_PI / 180.0;
    double dx = cos(rad_m) * sin(rad_h);
    double dy = cos(rad_m) * -cos(rad_h); /* Corrected: North (0) must decrease Y */
    double dz = sin(rad_m);

    printf(B_WHITE "\n--- TACTICAL BALLISTIC ANALYSIS ---\n" RESET);
    printf(" INTERCEPT VECTOR: [dx:%.3f, dy:%.3f, dz:%.3f]\n", dx, dy, dz);
    printf(" PARAMETERS:       [Heading:%.1f, Mark:%.1f]\n", h, m);
    printf(" VELOCITY:         Sublight constant (0.2 sector/step)\n");
    printf(" STATUS:           Torpedo armed. Launching...\n\n");

    double tx = s1, ty = s2, tz = s3;
    int steps = 80; /* Increased steps for higher precision */
    double step_x = dx * 0.2; /* Smaller steps for better collision detection */
    double step_y = dy * 0.2;
    double step_z = dz * 0.2;
    printf("Torpedo away! [" );
    for (int step = 0; step < steps; step++) {
        tx += step_x; ty += step_y; tz += step_z;
        if (step % 8 == 0) { printf("."); fflush(stdout); }
        /* Update Global State for Visualizer */
        cur_torp[0] = tx; cur_torp[1] = ty; cur_torp[2] = tz;
        update_3d_view();
        for (int i = 1; i <= 3; i++) {
            if (k[i][3] > 0 && fabs(tx - k[i][0]) < 0.4 && fabs(ty - k[i][1]) < 0.4 && fabs(tz - k[i][2]) < 0.4) {
                printf("] " B_RED "HIT!\n*** %s DESTROYED ***\n" RESET, get_species_name(k[i][4]));
                species_counts[k[i][4] - 10]--;
                if (lock_target == i) {
                    printf(B_CYAN "COMPUTER: Target lost. Lock released.\n" RESET);
                    lock_target = 0;
                }
                k[i][3] = 0; k3--; k9--;
                g[q1][q2][q3] -= 100;
                
                /* Trigger Explosion in Visualizer */
                cur_torp[0] = -100;
                cur_boom[0] = tx; cur_boom[1] = ty; cur_boom[2] = tz;
                update_3d_view();
                usleep(33333); /* 30 fps */
                update_3d_view(); /* Call again to clear BOOM from file */
                return;
            }
        }
        if (tx < 0.5 || tx > 11.0 || ty < 0.5 || ty > 11.0 || tz < 0.5 || tz > 11.0) break;
        usleep(33333); /* 30 fps */
    }
    printf("] Torpedo missed.\n");
    cur_torp[0] = -100;
    update_3d_view();
}
void shield_control(void) {
    char buf[MAXLEN];
    int i, amount;
    printf(B_CYAN "\n--- DIRECTIONAL SHIELD CONTROL ---\n" RESET);
    printf("Total Energy: %d\n", e);
    printf("1: Front [%d]  2: Rear [%d]  3: Top [%d]  4: Bottom [%d]  5: Left [%d]  6: Right [%d]\n", shields[0], shields[1], shields[2], shields[3], shields[4], shields[5]);
    printf("Select shield to charge (1-6) or 0 to balance: ");
    safe_gets(buf, MAXLEN); i = atoi(buf);
    
    printf("Units to transfer (positive to charge, negative to drain): ");
    safe_gets(buf, MAXLEN); amount = atoi(buf);
    if (i == 0) {
        /* Balance */
        int total_s = shields[0] + shields[1] + shields[2] + shields[3] + shields[4] + shields[5] + amount;
        if (total_s < 0) total_s = 0;
        if (amount > e) amount = e;
        e -= amount;
        for(int j=0; j<6; j++) shields[j] = total_s / 6;
    } else if (i >= 1 && i <= 6) {
        i--; /* 0-indexed */
        if (amount > e) amount = e;
        if (-amount > shields[i]) amount = -shields[i];
        shields[i] += amount;
        e -= amount;
    }
    printf("New Shield Status - F:%d R:%d T:%d B:%d L:%d RI:%d\n", shields[0], shields[1], shields[2], shields[3], shields[4], shields[5]);
}
void dock_control(void) {
    if (b3 <= 0) { printf("No starbases in this quadrant.\n"); return; }
    
    bool near_base = false;
    for (int i=0; i<b3; i++) {
        double dist = sqrt(pow(base_pos[i][0]-s1, 2) + pow(base_pos[i][1]-s2, 2) + pow(base_pos[i][2]-s3, 2));
        if (dist < 2.0) { near_base = true; break; }
    }
    
    if (near_base) {
        printf(B_GREEN "\n--- DOCKING AT STARBASE ---\n" RESET);
        printf("Resupplying and performing emergency repairs...\n");
        
        /* Restore energy only if below max */
        if (e < 5000) { printf("Energy cells recharged to 5000.\n"); e = 5000; }
        else { printf("Energy levels optimal. Maintained current charge.\n"); }
        
        /* Restore torpedoes only if below standard stock (10) */
        if (p < 10) { printf("Photon torpedo tubes replenished to 10.\n"); p = 10; }
        else { printf("Weapon inventory optimal. Maintained current stock.\n"); }
        
                /* Repair all systems to 100% */
        
                for(int i=0; i<8; i++) system_health[i] = 100.0f;
        
                life_support = 100.0f;
        
        
        for(int i=0; i<6; i++) shields[i] = 0; /* Shields are always lowered for docking */
        update_3d_view();
    } else {
        printf("Not near enough to a starbase to dock. Move closer!\n");
    }
}
void enemy_ai(void) {
    if (k3 <= 0 || is_playing_dead) return;
    for (int i = 1; i <= 3; i++) {
        if (k[i][3] > 0) {
            double dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
            if (dist > 4.0 && (rand()%100 > 50)) {
                if (k[i][0] < s1) k[i][0]++; else if (k[i][0] > s1) k[i][0]--;
                if (k[i][1] < s2) k[i][1]++; else if (k[i][1] > s2) k[i][1]--;
                if (k[i][2] < s3) k[i][2]++; else if (k[i][2] > s3) k[i][2]--;
                printf(B_YELLOW "%s at [%d,%d,%d] is closing in...\n" RESET, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2]);
                /* Recalculate distance after movement */
                dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
            }
            if (dist < 0.1) dist = 0.1;
            int hit = (int)(k[i][3] / dist * (0.5 + (rand()%10)/10.0));
            
            /* Determine which shield is hit */
            int s_idx = 0; /* 0:F, 1:R, 2:T, 3:B, 4:L, 5:RI */
            double dx = k[i][0] - s1;
            double dy = k[i][1] - s2;
            double dz = k[i][2] - s3;
            
            if (fabs(dz) >= fabs(dx) && fabs(dz) >= fabs(dy)) {
                s_idx = (dz > 0) ? 2 : 3;
            } else if (fabs(dy) >= fabs(dx) && fabs(dy) >= fabs(dz)) {
                s_idx = (dy < 0) ? 0 : 1; 
            } else {
                s_idx = (dx < 0) ? 4 : 5;
            }
            char *s_names[] = {"FRONT", "REAR", "TOP", "BOTTOM", "LEFT", "RIGHT"};
            printf(B_RED "Hit from %s at [%d,%d,%d] on %s shields: %d units!\n" RESET, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2], s_names[s_idx], hit);
            if (shields[s_idx] > 0) {
                shields[s_idx] -= hit;
                if (shields[s_idx] < 0) { e += shields[s_idx]; shields[s_idx] = 0; }
            } else {
                e -= hit;
                if (rand()%100 > 60) {
                    int sys = rand()%8;
                    float dmg = (float)(rand()%15 + 5);
                    system_health[sys] -= dmg;
                    if (system_health[sys] < 0) system_health[sys] = 0;
                    char *sys_names[] = {"Warp Engines", "SRS", "LRS", "Phasers", "Photon Tubes", "Shields", "Transporters", "Life Support"};
                    printf(B_RED "CRITICAL: Damage to %s reported!\n" RESET, sys_names[sys]);
                }
            }
            if (e <= 0) ship_destroyed();
        }
    }
    update_3d_view();
}
void status_report(void) {
    printf(B_CYAN "\n--- STATUS REPORT ---\n" RESET);
    printf("Stardate: %.1f\n", t);
    printf("Condition: %s\n", (k3 > 0) ? B_RED "RED ALERT" : (e < 1000) ? B_YELLOW "YELLOW ALERT" : B_GREEN "GREEN");
    printf("Position: Quadrant [%d,%d,%d] Sector [%.1f, %.1f, %.1f]\n", q1, q2, q3, s1, s2, s3);
    printf("Energy: %d  Torpedoes: %d  Crew: %d\n", e, p, crew_count);
    printf("Shields - FRONT:%d REAR:%d TOP:%d BOTTOM:%d LEFT:%d RIGHT:%d\n", shields[0], shields[1], shields[2], shields[3], shields[4], shields[5]);
    printf("Lock-on Status: %s\n", (lock_target == 0 ? "NONE" : get_species_name(k[lock_target][4])));
    if (k3 > 0) {
        printf(B_YELLOW "---QUADRANT TACTICAL DATA ---\n" RESET);
        for (int i = 1; i <= 3; i++) {
            if (k[i][3] > 0) {
                double dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
                printf(" %s %d: at [%d,%d,%d] | Power: %d | Dist: %.2f\n", get_species_name(k[i][4]), i, k[i][0], k[i][1], k[i][2], k[i][3], dist);
            }
        }
    }
    printf("Total Enemies in Galaxy: %d  Starbases: %d\n", k9, b9);
    printf(B_CYAN "--- GALAXY ENEMY INTELLIGENCE BREAKDOWN ---\n" RESET);
    for(int i=0; i<11; i++) {
        if (species_counts[i] > 0) {
            printf(" %-15s: %d remaining\n", get_species_name(10+i), species_counts[i]);
        }
    }
}
void lock_on_control(void) {
    char buf[MAXLEN];
    printf(B_RED "\n--- TACTICAL LOCK-ON SYSTEM ---\n" RESET);
    if (k3 <= 0) { printf("No targets available.\n"); lock_target = 0; return; }
    for (int i = 1; i <= 3; i++) {
        if (k[i][3] > 0) printf("%d: %s at [%d,%d,%d]\n", i, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2]);
    }
    printf("Enter target number (1-3) or 0 to release: ");
    safe_gets(buf, MAXLEN); lock_target = atoi(buf);
    if (lock_target < 0 || lock_target > 3 || (lock_target > 0 && k[lock_target][3] <= 0)) {
        printf("Invalid target. Lock released.\n");
        lock_target = 0;
    } else if (lock_target > 0) {
        printf("Target %d locked.\n", lock_target);
    } else {
        printf("Lock released.\n");
    }
}
void power_distribution_control(void) {
    char buf[MAXLEN];
    printf(B_YELLOW "\n--- POWER DISTRIBUTION ---\n" RESET);
    printf("Current allocation: Engines: %.0f%%  Shields: %.0f%%  Weapons: %.0f%%\n", power_dist[0]*100, power_dist[1]*100, power_dist[2]*100);
    printf("Enter new percentage for Engines (0-100): ");
    safe_gets(buf, MAXLEN); power_dist[0] = atof(buf) / 100.0f;
    printf("Enter new percentage for Shields (0-100): ");
    safe_gets(buf, MAXLEN); power_dist[1] = atof(buf) / 100.0f;
    power_dist[2] = 1.0f - power_dist[0] - power_dist[1];
    if (power_dist[2] < 0) {
        printf("Invalid distribution. Resetting to balanced.\n");
        power_dist[0] = 0.33f; power_dist[1] = 0.33f; power_dist[2] = 0.34f;
    }
    printf("New allocation: Engines: %.0f%%  Shields: %.0f%%  Weapons: %.0f%%\n", power_dist[0]*100, power_dist[1]*100, power_dist[2]*100);
}
void psychological_warfare(void) {
    char buf[MAXLEN];
    printf(B_MAGENTA "\n--- PSYCHOLOGICAL WARFARE ---\n" RESET);
    printf("1: Request Surrender\n2: Corbomite Bluff (Remaining: %d)\n3: Play Dead\nSelect option: ", corbomite_count);
    safe_gets(buf, MAXLEN); int opt = atoi(buf);
    
        if (opt == 1) {
    
            if (k3 > 0) {
    
                printf("Transmitting surrender request...\n");
    
                            if (rand()%100 > 90) {
    
                                printf("Enemy Commander: \"We accept your terms. We are standing down.\"\n");
    
                                for(int i=1; i<=3; i++) if (k[i][3]>0) { 
    
                                    species_counts[k[i][4] - 10]--;
    
                                    k[i][3]=0; k3--; k9--; g[q1][q2][q3]-=100; 
    
                                }
    
                                lock_target = 0;
    
                
    
                    won_game();
    
                } else printf("Enemy Commander: \"Death to the Federation!\"\n");
    
            } else printf("No one to talk to.\n");
    
        } else if (opt == 2) {
    
            if (corbomite_count > 0) {
    
                corbomite_count--;
    
                printf("Broadcasting Corbomite threat...\n");
    
                if (rand()%100 > 60) {
    
                    printf("Enemies are retreating to a safe distance!\n");
    
                    for(int i=1; i<=3; i++) if (k[i][3]>0) { k[i][0]+=2; k[i][1]+=2; } /* Move away */
    
                } else printf("Enemies are not impressed.\n");
    
            } else printf("No Corbomite left.\n");
    
        } else if (opt == 3) {
    
            is_playing_dead = !is_playing_dead;
    
            printf("Ship status: %s\n", (is_playing_dead ? "EMISSIONS MASKED (PLAYING DEAD)" : "ACTIVE"));
    
        }
    
    }
    
    
    
    void auxiliary_systems(void) {
    
    
    char buf[MAXLEN];
    printf(B_WHITE "\n--- AUXILIARY SYSTEMS ---\n" RESET);
    printf("1: Launch Antimatter Probe\n2: Jettison Engineering\nSelect option: ");
    safe_gets(buf, MAXLEN); int opt = atoi(buf);
    
    if (opt == 1) {
        if (e < 500) { printf("Insufficient energy.\n"); return; }
        e -= 500;
        printf("Probe launched. Scanning deep space...\n");
        long_range_scan(); /* Enhanced LRS effect */
    } else if (opt == 2) {
        printf("WARNING: JETTISONING ENGINEERING WILL LEAVE SHIP IMMOBILE!\nConfirm (y/n): ");
        safe_gets(buf, MAXLEN);
        if (buf[0] == 'y') {
            printf("Engineering section jettisoned!\n");
            system_health[0] = 0; /* Warp engines destroyed */
            /* Massive explosion damages nearby enemies */
            for(int i=1; i<=3; i++) {
                if (k[i][3]>0) { 
                    k[i][3] -= 500;
                    if (k[i][3]<=0) { 
                        k3--; k9--; 
                        if (lock_target == i) {
                            printf(B_CYAN "COMPUTER: Target lost. Lock released.\n" RESET);
                            lock_target = 0;
                        }
                    } 
                }
            }
        }
    }
}
void convert_resources(void) {
    char buf[MAXLEN];
    printf(B_CYAN "\n--- RESOURCE CONVERSION ---\n" RESET);
    inventory_report();
    printf("\n1: Convert Dilithium to Energy (1:1)\n2: Convert Verterium to Torpedoes (100:1)\n3: Use Gases to replenish Air (10:1%%)\nSelect option: ");
    safe_gets(buf, MAXLEN); int opt = atoi(buf);
    
    if (opt == 1) {
        printf("Dilithium to convert: "); safe_gets(buf, MAXLEN); int amt = atoi(buf);
        if (amt > inventory[1]) amt = inventory[1];
        if (amt > 0) {
            inventory[1] -= amt; e += amt; if (e > 5000) e = 5000;
            printf(B_GREEN "Energy replenished.\n" RESET);
        }
    } else if (opt == 2) {
        printf("Verterium to convert: "); safe_gets(buf, MAXLEN); int amt = atoi(buf);
        if (amt > inventory[3]) amt = inventory[3];
        int t_gain = amt / 100;
        if (t_gain > 0) {
            inventory[3] -= t_gain * 100; p += t_gain; if (p > 20) p = 20;
            printf(B_GREEN "Torpedoes assembled.\n" RESET);
        }
    } else if (opt == 3) {
        printf("Gases to use: "); safe_gets(buf, MAXLEN); int amt = atoi(buf);
        if (amt > inventory[6]) amt = inventory[6];
        float air_gain = (float)amt / 10.0f;
        if (air_gain > 0) {
            inventory[6] -= amt; life_support += air_gain; if (life_support > 100.0f) life_support = 100.0f;
            printf(B_GREEN "Air reserves replenished.\n" RESET);
        }
    }
}
void inventory_report(void) {
    printf(B_CYAN "\n--- SHIP'S INVENTORY ---\n" RESET);
    printf("1: Dilithium Crystals: %d\n", inventory[1]);
    printf("2: Tritanium Ore:      %d\n", inventory[2]);
    printf("3: Verterium-Nitrate:  %d\n", inventory[3]);
    printf("4: Monotanium Alloy:   %d\n", inventory[4]);
    printf("5: Isolinear Crystals: %d\n", inventory[5]);
    printf("6: Atmospheric Gases:  %d\n", inventory[6]);
}
void repair_control(void) {
    char buf[MAXLEN];
    char *names[] = {"Warp Engines", "SRS", "LRS", "Phasers", "Photon Tubes", "Shield Generator", "Transporters", "Life Support"};
    
    damage_report();
    inventory_report();
    
    printf("\nSelect system to repair (1-8) or 0 to exit: ");
    safe_gets(buf, MAXLEN); int sys = atoi(buf);
    if (sys < 1 || sys > 8) return;
    sys--; /* 0-indexed */
    
    if (system_health[sys] >= 100.0f) {
        printf("System is already at 100%% integrity.\n");
        return;
    }
    /* Cost calculation: 1 mineral unit per 0.1% repair */
    float missing = 100.0f - system_health[sys];
    int cost = (int)(missing * 10);
    
    /* Determine required mineral */
    int m_type = 0;
    if (sys == 0 || sys == 5 || sys == 7 || sys == 4) m_type = 4; /* Structural -> Monotanium */
    else m_type = 5; /* Electronic -> Isolinear */
    
    char *m_names[] = {"", "", "", "", "Monotanium", "Isolinear"};
    printf("Repairing %s requires %d units of %s.\n", names[sys], cost, m_names[m_type]);
    printf("Available: %d. Proceed (y/n)? ", inventory[m_type]);
    safe_gets(buf, MAXLEN);
    
    if (buf[0] == 'y') {
        if (inventory[m_type] >= cost) {
            inventory[m_type] -= cost;
            system_health[sys] = 100.0f;
            printf(B_GREEN "Repairs complete. %s is now fully operational.\n" RESET, names[sys]);
        } else {
            /* Partial repair */
            float repair_amt = (float)inventory[m_type] / 10.0f;
            system_health[sys] += repair_amt;
            inventory[m_type] = 0;
            printf(B_YELLOW "Insufficient minerals for full repair. %s restored to %.1f%%.\n" RESET, names[sys], system_health[sys]);
        }
    }
}
void damage_report(void) {
    char *names[] = {"Warp Engines", "SRS", "LRS", "Phasers", "Photon Tubes", "Shields", "Transporters", "Life Support"};
    printf(B_CYAN "\n--- DAMAGE CONTROL REPORT ---\n" RESET);
    for (int i = 0; i < 8; i++) {
        char *color = (system_health[i] >= 100.0f) ? B_GREEN : (system_health[i] > 30.0f) ? B_YELLOW : B_RED;
        printf("%-20s: %s%.1f%%" RESET " %s\n", names[i], color, system_health[i], (system_health[i] >= 100.0f ? "OPERATIONAL" : "DAMAGED"));
    }
}
void help_command(char *arg) {
    if (arg == NULL || strlen(arg) == 0) {
        printf(B_CYAN "\n--- COMPUTER COMMAND DATABASE ---\n" RESET);
        printf(B_YELLOW "nav" RESET ": Navigational Helm (Warp drive). Enter heading, mark, and warp factor.\n");
        printf(B_YELLOW "srs" RESET ": Short Range Sensors. Current quadrant view and ship status.\n");
        printf(B_YELLOW "lrs" RESET ": Long Range Sensors. Scans adjacent quadrants (Z is vertical).\n");
        printf(B_YELLOW "pha" RESET ": Phaser Control. Fire energy banks at enemies.\n");
        printf(B_YELLOW "tor" RESET ": Torpedo Control. Launch photon torpedoes.\n");
        printf(B_YELLOW "she" RESET ": Shield Control. Transfer energy to Front, Rear, Top, or Bottom shields.\n");
        printf(B_YELLOW "lock" RESET ": Tactical Lock-on. Acquire a target for phasers and torpedoes.\n");
        printf(B_YELLOW "pow" RESET ": Power Distribution. Allocate energy between Engines, Shields, and Weapons.\n");
        printf(B_YELLOW "psy" RESET ": Psychological Warfare. Resignation requests, Corbomite bluffs, or Play dead.\n");
        printf(B_YELLOW "aux" RESET ": Auxiliary Systems. Probes and Engineering Jettison.\n");
            printf(B_YELLOW "doc" RESET ": Dock at Starbase. Must be near a starbase.\n");
            printf(B_YELLOW "bor" RESET ": Board Enemy. Send security teams to capture disabled ships.\n");
            printf(B_YELLOW "min" RESET ": Planetary Mining. Extract minerals from nearby planets.\n");
        
        printf(B_YELLOW "rep" RESET ": Repair Ship. Use minerals to fix damaged subsystems.\n");
        printf(B_YELLOW "inv" RESET ": Inventory. View collected minerals and resources.\n");
            printf(B_YELLOW "con" RESET ": Convert. Refine minerals into Energy or Torpedoes.\n");
            printf(B_YELLOW "cal" RESET ": Nav-Calculator. Compute Heading/Mark/Warp to any coordinates.\n");
            printf(B_YELLOW "apr" RESET ": Approach Target. Move to a specific distance from an object.\n");
        
        printf(B_YELLOW "sco" RESET ": Solar Scooping. Collect hydrogen from stars (Risk: Heat damage).\n");
        printf(B_YELLOW "har" RESET ": Antimatter Harvest. Extract fuel from black holes (Risk: Extreme).\n");
        printf(B_YELLOW "sta" RESET ": Status Report. Mission progress and tactical quadrant data.\n");
        printf(B_YELLOW "dam" RESET ": Damage Report. Check integrity of all subsystems.\n");
        printf(B_YELLOW "axs" RESET ": Toggle Cartesian Axes. Show/hide X, Y, Z guides in 3D view.\n");
        printf(B_YELLOW "grd" RESET ": Toggle Sector Grid. Show/hide the 3D sector boundaries.\n");
        printf(B_YELLOW "help [cmd]" RESET ": Display this guide or detailed help for a command.\n");
        printf(B_YELLOW "xxx" RESET ": Self-destruct / Exit game.\n");
        printf("\nType " B_YELLOW "help <command>" RESET " for detailed information (e.g., 'help nav').\n");
        return;
    }
    printf(B_CYAN "\n--- DETAILED COMPUTER ARCHIVE: %s ---\n" RESET, arg);
    if (!strcmp(arg, "nav")) {
        printf("The Navigational Helm controls the ship's Warp Engines.\n");
        printf("- " B_WHITE "Heading" RESET " (0-359): Horizontal direction (0=N, 90=E, 180=S, 270=W).\n");
        printf("- " B_WHITE "Mark" RESET " (-90 to +90): Vertical inclination. +90 is UP, -90 is DOWN.\n");
        printf("- " B_WHITE "Warp Factor" RESET " (0-8): Speed. Warp 1.0 crosses one quadrant.\n");
        printf("Note: Movement consumes energy proportional to distance.\n");
    } else if (!strcmp(arg, "srs")) {
        printf("Short Range Sensors provide a tactical overview of the current quadrant.\n");
        printf("Displays positions of the Enterprise, Enemies, Stars, and Starbases.\n");
        printf("Also reports current energy, shields, and torpedo inventory.\n");
    } else if (!strcmp(arg, "lrs")) {
        printf("Long Range Sensors scan the 26 quadrants surrounding your current location.\n");
        printf("The scan is 3D, covering the current deck (Z), the one above, and the one below.\n");
        printf("Format: [BH P K B S] where K=Enemies, B=Bases, S=Stars, P=Planets, BH=Black Hole.\n");
        printf("Cells also display calculated Heading, Mark, and Warp to reach that quadrant.\n");
    } else if (!strcmp(arg, "pha")) {
        printf("Phasers are rapid-fire energy weapons.\n");
        printf("Damage is inversely proportional to 3D distance. Closer is better.\n");
        printf("If a " B_YELLOW "lock" RESET " is active, phasers will automatically target the locked ship.\n");
        printf("If no lock, you can choose a target or fire at ALL enemies in range.\n");
    } else if (!strcmp(arg, "tor")) {
        printf("Photon Torpedoes are high-yield ballistic projectiles.\n");
        printf("They travel in a straight 3D line. Precise Heading and Mark are required.\n");
        printf("If a " B_YELLOW "lock" RESET " is active, the computer calculates the trajectory automatically.\n");
        printf("One hit is usually sufficient to destroy a standard enemy cruiser.\n");
    } else if (!strcmp(arg, "she")) {
        printf("Deflector Shields are divided into four directional quadrants: FRONT, REAR, TOP, BOTTOM.\n");
        printf("Energy must be manually allocated to each sector.\n");
        printf("Incoming fire impacts the shield closest to the enemy's relative position.\n");
        printf("If a shield sector is depleted, damage is taken by ship's systems and hull.\n");
    } else if (!strcmp(arg, "lock")) {
        printf("The Tactical Lock-on system synchronizes sensors with weapons.\n");
        printf("Once locked on an enemy ship, Phasers and Torpedoes use automated guidance.\n");
        printf("Lock is automatically lost if the target is destroyed or moves out of range.\n");
    } else if (!strcmp(arg, "pow")) {
        printf("Main Power Distribution manages the ship's reactor output.\n");
        printf("You can allocate percentage of power to Engines, Shields, and Weapons.\n");
        printf("Higher allocation improves efficiency and recovery rates for that system.\n");
    } else if (!strcmp(arg, "psy")) {
        printf("Psychological Warfare subroutines for non-kinetic engagement:\n");
        printf("- " B_WHITE "Request Surrender" RESET ": High risk, depends on enemy morale.\n");
        printf("- " B_WHITE "Corbomite Bluff" RESET ": Tricks Klingons into retreating (limited uses).\n");
        printf("- " B_WHITE "Play Dead" RESET ": Masks emissions. Klingons stop attacking, but you remain immobile.\n");
    } else if (!strcmp(arg, "aux")) {
        printf("Auxiliary Systems for special operations:\n");
        printf("- " B_WHITE "Antimatter Probe" RESET ": Performs a high-resolution deep space scan.\n");
        printf("- " B_WHITE "Jettison Engineering" RESET ": Emergency maneuver. Causes a massive explosion but disables Warp drive.\n");
    } else if (!strcmp(arg, "doc")) {
        printf("Docking at a Starbase restores all energy and torpedoes.\n");
        printf("Requires proximity (Distance < 2.0) to a Starbase in the current quadrant.\n");
        printf("Shields must be lowered for docking procedures.\n");
    } else if (!strcmp(arg, "bor")) {
        printf("The Boarding command allows you to send security teams to a nearby enemy ship.\n");
        printf("- " B_WHITE "Proximity" RESET ": Distance must be < 1.0 units.\n");
        printf("- " B_WHITE "Condition" RESET ": Target is easier to capture if its Power is < 150.\n");
        printf("- " B_WHITE "Transporters" RESET ": System must be operational.\n");
        printf("A successful capture provides large amounts of Energy, Torpedoes, and Minerals.\n");
        printf("Warning: Casualties among security personnel are common.\n");
    } else if (!strcmp(arg, "min")) {
        printf("Planetary Mining extracts resources from planets in the current quadrant.\n");
        printf("Requires orbit (Distance < 2.0). Extracted minerals are stored in Inventory.\n");
        printf("Minerals found: Dilithium, Tritanium, Verterium, Monotanium, Isolinear, Gases.\n");
    } else if (!strcmp(arg, "rep")) {
        printf("On-board repair teams can fix damaged systems using raw materials.\n");
        printf("- " B_WHITE "Structural Systems" RESET " (Warp, Shields, Life Support) require " B_WHITE "Monotanium" RESET ".\n");
        printf("- " B_WHITE "Electronic Systems" RESET " (Sensors, Phasers, Transporters) require " B_WHITE "Isolinear Crystals" RESET ".\n");
    } else if (!strcmp(arg, "con")) {
        printf("Resource Conversion and Refining:\n");
        printf("- " B_WHITE "Dilithium" RESET " -> Emergency Energy.\n");
        printf("- " B_WHITE "Verterium" RESET " -> Photon Torpedo assembly.\n");
        printf("- " B_WHITE "Atmospheric Gases" RESET " -> Oxygen/Nitrogen replenishment for Life Support.\n");
    } else if (!strcmp(arg, "cal")) {
        printf("The Navigational Calculator computes vectors to remote galactic coordinates.\n");
        printf("Input the target Quadrant [X,Y,Z] and Sector [X,Y,Z).\n");
        printf("The computer will return the precise Heading, Mark, and Warp factor needed.\n");
        printf("Essential for long-range planning beyond the reach of LRS.\n");
    } else if (!strcmp(arg, "apr")) {
        printf("Automatic Approach System uses the helm to reach a specific target.\n");
        printf("Select a target (Star, Base, Planet, Enemy) and a desired distance.\n");
        printf("The ship will automatically compute the 3D vector and move there.\n");
    } else if (!strcmp(arg, "sco") || !strcmp(arg, "har")) {
        printf("Hazardous Resource Collection:\n");
        printf("- " B_YELLOW "sco" RESET " (Solar Scooping): Collect hydrogen from stars. High thermal stress on shields.\n");
        printf("- " B_RED "har" RESET " (Antimatter Harvest): Extract fuel from Black Holes. High gravitational shearing risk.\n");
    } else if (!strcmp(arg, "dam")) {
        printf("Damage Report lists the health percentage of all 8 critical ship systems.\n");
        printf("Systems below 100%% function with reduced efficiency or drain resources (Life Support).\n");
    } else {
        printf("Command not found in database. Type 'help' for a full list.\n");
    }
}
void mine_control(void) {
    if (p3 <= 0) { printf("No planets in this quadrant.\n"); return; }
    
    int planet_idx = -1;
    for (int i=0; i<p3; i++) {
        double dist = sqrt(pow(planet_pos[i][0]-s1, 2) + pow(planet_pos[i][1]-s2, 2) + pow(planet_pos[i][2]-s3, 2));
        if (dist < 2.0) { planet_idx = i; break; }
    }
    
    if (planet_idx != -1) {
        if (planet_pos[planet_idx][3] == 0) {
            printf("This planet's resources have already been depleted.\n");
        } else {
            printf(B_CYAN "\n--- PLANETARY MINING OPERATIONS ---\n" RESET);
            printf("Transporters locked on mineral signatures... Commencing beam-up.\n");
            sleep(1);
            
            int type = planet_pos[planet_idx][3];
            int qty = planet_pos[planet_idx][4];
            inventory[type] += qty;
            
            char *m_names[] = {"", "Dilithium", "Tritanium", "Verterium", "Monotanium", "Isolinear", "Atmospheric Gases"};
            printf(B_GREEN "RESULT: %d units of %s beamed to inventory.\n" RESET, qty, m_names[type]);
            
            planet_pos[planet_idx][3] = 0; /* Deplete */
            update_3d_view();
        }
    } else {
        printf("Not in stable orbit. Move closer to a planet!\n");
    }
}
void navigation_calculator(void) {
    char buf[MAXLEN];
    int tq1, tq2, tq3;
    double ts1, ts2, ts3;

    printf(B_CYAN "\n--- GALACTIC NAVIGATIONAL CALCULATOR ---\n" RESET);
    printf("TARGET QUADRANT X,Y,Z (e.g. 1,5,3): ");
    safe_gets(buf, MAXLEN);
    if (sscanf(buf, "%d,%d,%d", &tq1, &tq2, &tq3) != 3 && sscanf(buf, "%d %d %d", &tq1, &tq2, &tq3) != 3) {
        printf("Invalid quadrant format.\n"); return;
    }
    printf("TARGET SECTOR X,Y,Z (e.g. 5.5,5.5,5.5): ");
    safe_gets(buf, MAXLEN);
    if (sscanf(buf, "%lf,%lf,%lf", &ts1, &ts2, &ts3) != 3 && sscanf(buf, "%lf %lf %lf", &ts1, &ts2, &ts3) != 3) {
        printf("Invalid sector format.\n"); return;
    }

    if (tq1 < 1 || tq1 > 10 || tq2 < 1 || tq2 > 10 || tq3 < 1 || tq3 > 10 ||
        ts1 < 1.0 || ts1 >= 11.0 || ts2 < 1.0 || ts2 >= 11.0 || ts3 < 1.0 || ts3 >= 11.0) {
        printf("Error: Target coordinates outside galactic boundaries.\n");
        return;
    }

    /* Absolute positions */
    double cur_gx = (q1 - 1) * 10.0 + s1;
    double cur_gy = (q2 - 1) * 10.0 + s2;
    double cur_gz = (q3 - 1) * 10.0 + s3;
    
    double tar_gx = (tq1 - 1) * 10.0 + ts1;
    double tar_gy = (tq2 - 1) * 10.0 + ts2;
    double tar_gz = (tq3 - 1) * 10.0 + ts3;

    double dx = tar_gx - cur_gx;
    double dy = tar_gy - cur_gy;
    double dz = tar_gz - cur_gz;
    double dist = sqrt(dx*dx + dy*dy + dz*dz);
    
    if (dist < 0.01) {
        printf("Target is at current position.\n");
        return;
    }

    /* Heading: 0=N (-Y), 90=E (+X) */
    double heading = atan2(dx, -dy) * 180.0 / M_PI;
    if (heading < 0) heading += 360.0;
    
    /* Mark: Vertical angle */
    double mark = asin(dz / dist) * 180.0 / M_PI;
    double warp = dist / 10.0;

    printf(B_WHITE "\n--- CALCULATION RESULTS ---\n" RESET);
    printf(" DESTINATION:  Quadrant [%d,%d,%d] Sector [%.1f,%.1f,%.1f]\n", tq1, tq2, tq3, ts1, ts2, ts3);
    printf(" TOTAL DIST:   %.2f units\n", dist);
    printf(B_YELLOW " HEADING:      %.1f\n" RESET, heading);
    printf(B_YELLOW " MARK:         %.1f\n" RESET, mark);
    printf(B_YELLOW " WARP FACTOR:  %.2f\n" RESET, warp);
    printf(" ENERGY REQ:   %d units\n", (int)dist);
    printf(" TRAVEL TIME:  %.1f seconds\n", warp * 3.0);
    printf("---------------------------\n");
}

void approach_control(void) {
    char buf[MAXLEN];
    float tx = -1, ty = -1, tz = -1;
    char target_name[32] = "";
    printf(B_CYAN "\n--- AUTOMATIC APPROACH SYSTEM ---\n" RESET);
    int count = 1;
    for (int i=1; i<=3; i++) if (k[i][3] > 0) printf("%d: %s at [%d,%d,%d]\n", count++, get_species_name(k[i][4]), k[i][0], k[i][1], k[i][2]);
    for (int i=0; i<b3; i++) printf("%d: Starbase at [%d,%d,%d]\n", count++, base_pos[i][0], base_pos[i][1], base_pos[i][2]);
    for (int i=0; i<p3; i++) printf("%d: Planet (%s) at [%d,%d,%d]\n", count++, (planet_pos[i][3]==1?"Dilithium":planet_pos[i][3]==2?"Tritanium":planet_pos[i][3]==3?"Verterium":"Depleted"), planet_pos[i][0], planet_pos[i][1], planet_pos[i][2]);
    for (int i=0; i<st3; i++) printf("%d: Star at [%d,%d,%d]\n", count++, stars_pos[i][0], stars_pos[i][1], stars_pos[i][2]);
    for (int i=0; i<bh3; i++) printf("%d: Black Hole (Singularity) at [%d,%d,%d]\n", count++, bh_pos[i][0], bh_pos[i][1], bh_pos[i][2]);
    if (count == 1) { printf("No targets in range.\n"); return; }
    printf("Select target number (1-%d): ", count-1);
    safe_gets(buf, MAXLEN); int sel = atoi(buf);
    
    /* Locate coordinates */
    int curr = 1;
    for (int i=1; i<=3; i++) if (k[i][3] > 0) { if (curr++ == sel) { tx=k[i][0]; ty=k[i][1]; tz=k[i][2]; strcpy(target_name, "Enemy"); } }
    for (int i=0; i<b3; i++) { if (curr++ == sel) { tx=base_pos[i][0]; ty=base_pos[i][1]; tz=base_pos[i][2]; strcpy(target_name, "Starbase"); } }
    for (int i=0; i<p3; i++) { if (curr++ == sel) { tx=planet_pos[i][0]; ty=planet_pos[i][1]; tz=planet_pos[i][2]; strcpy(target_name, "Planet"); } }
    for (int i=0; i<st3; i++) { if (curr++ == sel) { tx=stars_pos[i][0]; ty=stars_pos[i][1]; tz=stars_pos[i][2]; strcpy(target_name, "Star"); } }
    for (int i=0; i<bh3; i++) { if (curr++ == sel) { tx=bh_pos[i][0]; ty=bh_pos[i][1]; tz=bh_pos[i][2]; strcpy(target_name, "Black Hole"); } }
    if (tx == -1) { printf("Invalid selection.\n"); return; }
    printf("Enter desired distance from %s (e.g., 1.5 for Docking/Mining): ", target_name);
    safe_gets(buf, MAXLEN); double target_dist = atof(buf);
    double dx = tx - s1; double dy = ty - s2; double dz = tz - s3;
    double current_range = sqrt(dx*dx + dy*dy + dz*dz);
    double move_dist = current_range - target_dist;
    if (move_dist <= 0) { printf("Already within range.\n"); return; }
    if (e < move_dist) { printf("Insufficient energy for this maneuver!\n"); return; }

    printf(B_WHITE "\n--- APPROACH TRAJECTORY CALCULATION ---\n" RESET);
    printf(" TARGET POSITION (Quadrant-relative): [%.1f, %.1f, %.1f]\n", tx, ty, tz);
    printf(" CURRENT RANGE:  %.2f units\n", current_range);
    printf(" DESIRED RANGE:  %.2f units\n", target_dist);
    printf(" TRAVEL DIST:    %.2f units\n", move_dist);
    printf(" TRAVEL TIME:    %.1f seconds (3s/Quadrant)\n", (move_dist/10.0) * 3.0);
    printf(" STATUS:         Vector locked. Engaging thrusters...\n\n");

    /* Rotate to target */
    double target_h = atan2(dx, -dy) * 180.0 / M_PI; if (target_h < 0) target_h += 360.0;
    double target_m = asin(dz / current_range) * 180.0 / M_PI;
    smooth_rotate(target_h, target_m);

    /* Execute Move */
    double ux = dx / current_range; double uy = dy / current_range; double uz = dz / current_range;
    double start_s1 = s1, start_s2 = s2, start_s3 = s3;
    double end_s1 = s1 + ux * move_dist;
    double end_s2 = s2 + uy * move_dist;
    double end_s3 = s3 + uz * move_dist;

    int steps = (int)((move_dist/10.0) * 3.0 * 30.0);
    if (steps < 30) steps = 30; /* Min 1 second for visual smoothness */

    printf("Approaching target... [" );
    for (int i=1; i<=steps; i++) {
        s1 = start_s1 + (end_s1 - start_s1) * i / (double)steps;
        s2 = start_s2 + (end_s2 - start_s2) * i / (double)steps;
        s3 = start_s3 + (end_s3 - start_s3) * i / (double)steps;
        update_3d_view();
        wait_for_visualizer();
        if (i % 6 == 0) { printf("."); fflush(stdout); }
    }
    printf("] Done.\n");
    e -= (int)move_dist;
    t += (move_dist/10.0 < 1.0) ? (move_dist/10.0) : 1.0;
    printf(B_GREEN "Maneuver complete. New position: Sector [%.1f, %.1f, %.1f] | Dist to %s: %.2f\n" RESET, s1, s2, s3, target_name, target_dist);
    
    printf("Stabilizing ship attitude (Horizontal)...\n");
    smooth_rotate(ent_h, 0);

    update_3d_view();
}
void scoop_control(void) {
    if (st3 <= 0) { printf("No stars in this quadrant.\n"); return; }
    
    int star_idx = -1;
    for (int i=0; i<st3; i++) {
        double dist = sqrt(pow(stars_pos[i][0]-s1, 2) + pow(stars_pos[i][1]-s2, 2) + pow(stars_pos[i][2]-s3, 2));
        if (dist < 1.2) { star_idx = i; break; }
    }
    
    if (star_idx != -1) {
        printf(B_YELLOW "\n--- SOLAR SCOOPING SEQUENCE ---\n" RESET);
        printf("Descending into stellar corona... Hull temperature rising!\n");
        sleep(1);
        
        /* Determine facing shield */
        int s_idx = 0;
        double dx = stars_pos[star_idx][0] - s1;
        double dy = stars_pos[star_idx][1] - s2;
        double dz = stars_pos[star_idx][2] - s3;
        if (fabs(dz) > fabs(dx) && fabs(dz) > fabs(dy)) s_idx = (dz > 0) ? 2 : 3;
        else s_idx = (dy < 0) ? 0 : 1;
                    char *s_names[] = {"FRONT", "REAR", "TOP", "BOTTOM", "LEFT", "RIGHT"};
                    printf(B_WHITE "Thermal stress impacting %s shields.\n" RESET, s_names[s_idx]);        int heat_dmg = 100;
        if (shields[s_idx] >= heat_dmg) {
            shields[s_idx] -= heat_dmg;
            printf(B_GREEN "Shields held. Minor energy drain recorded.\n" RESET);
        } else {
            int leak = heat_dmg - shields[s_idx];
            shields[s_idx] = 0;
            printf(B_RED "WARNING: %s shields failed! Heat causing direct energy loss (-%d units).\n" RESET, s_names[s_idx], leak*2);
            e -= leak * 2;
        }
        
        int gain = 500 + rand()%500;
        e += gain; if (e > 5000) e = 5000;
        printf(B_GREEN "Hydrogen collection successful. Gained %d energy units.\n" RESET, gain);
        update_3d_view();
    } else {
        printf("Too far from any star. Danger: stay outside the 1.2 distance threshold to avoid incineration!\n");
    }
}
void harvest_control(void) {
    if (bh3 <= 0) { printf("No singularities in this quadrant.\n"); return; }
    
    int bh_idx = -1;
    for (int i=0; i<bh3; i++) {
        double dist = sqrt(pow(bh_pos[i][0]-s1, 2) + pow(bh_pos[i][1]-s2, 2) + pow(bh_pos[i][2]-s3, 2));
        if (dist < 1.5) { bh_idx = i; break; }
    }
    
    if (bh_idx != -1) {
        printf(B_RED "\n--- ANTIMATTER HARVESTING SEQUENCE ---\n" RESET);
        printf("Caution: Event Horizon proximity detected.\n");
        printf("Activating quantum gravity collectors...\n");
        sleep(1);
        
        /* Determine facing shield */
        int s_idx = 0;
        double dx = bh_pos[bh_idx][0] - s1;
        double dy = bh_pos[bh_idx][1] - s2;
        double dz = bh_pos[bh_idx][2] - s3;
        if (fabs(dz) > fabs(dx) && fabs(dz) > fabs(dy)) s_idx = (dz > 0) ? 2 : 3;
        else s_idx = (dy < 0) ? 0 : 1;
                    char *s_names[] = {"FRONT", "REAR", "TOP", "BOTTOM", "LEFT", "RIGHT"};
                    printf(B_WHITE "Gravitational shear impacting %s shields.\n" RESET, s_names[s_idx]);        int shear_dmg = 500;
        if (shields[s_idx] >= shear_dmg) {
            shields[s_idx] -= shear_dmg;
            printf(B_GREEN "Shields held against gravitational shearing.\n" RESET);
        } else {
            int leak = shear_dmg - shields[s_idx];
            shields[s_idx] = 0;
            printf(B_RED "CRITICAL: %s shields failed! Gravitational shearing damaging systems.\n" RESET, s_names[s_idx]);
            e -= leak * 2;
            int sys = rand()%8; system_health[sys] -= (float)(rand()%20 + 10);
        }
        
        printf(B_WHITE "STATUS: Harvesting Hawking Radiation and Antimatter particles...\n" RESET);
        e += 2000; if (e > 5000) e = 5000;
        p += 5; if (p > 20) p = 20;
        printf(B_GREEN "RESULT: Success. +2000 Energy | +5 Torpedoes assembled.\n" RESET);
        update_3d_view();
    } else {
        printf("Too far from the singularity. Orbit must be within 1.5 for collection.\n");
    }
}
void toggle_grid(void) {
    show_grid = !show_grid;
    printf("Sector grid: %s\n", (show_grid ? "ON" : "OFF"));
    update_3d_view();
}
void toggle_axes(void) {
    show_axes = !show_axes;
    printf("Cartesian axes: %s\n", (show_axes ? "ON" : "OFF"));
    update_3d_view();
}
void new_game(bool resume) {
    if (!resume) {
        initialize();
        new_quadrant();
    }
    update_3d_view();
    help_command("");
    char line[MAXLEN], cmd[MAXLEN], arg[MAXLEN];

    while(1) {
        if (k9 <= 0) won_game();
        life_support_check();

        printf("\nCommand (nav, srs, lrs, pha, tor, she, lock, pow, psy, aux, doc, bor, min, rep, inv, con, cal, apr, sco, har, sta, dam, axs, grd, help, xxx)? ");
        if (fgets(line, MAXLEN, stdin) == NULL) break;
        
        cmd[0] = '\0'; arg[0] = '\0';
        sscanf(line, "%254s %254s", cmd, arg);

        if (!strcmp(cmd, "nav")) { course_control(); enemy_ai(); }
        else if (!strcmp(cmd, "srs")) short_range_scan();
        else if (!strcmp(cmd, "lrs")) long_range_scan();
        else if (!strcmp(cmd, "pha")) { phaser_control(); enemy_ai(); }
        else if (!strcmp(cmd, "tor")) { torpedo_control(); enemy_ai(); }
        else if (!strcmp(cmd, "she")) shield_control();
        else if (!strcmp(cmd, "lock")) lock_on_control();
        else if (!strcmp(cmd, "pow")) power_distribution_control();
        else if (!strcmp(cmd, "psy")) psychological_warfare();
        else if (!strcmp(cmd, "aux")) auxiliary_systems();
        else if (!strcmp(cmd, "doc")) dock_control();
        else if (!strcmp(cmd, "bor")) { boarding_control(); enemy_ai(); }
        else if (!strcmp(cmd, "min")) { mine_control(); enemy_ai(); }
        else if (!strcmp(cmd, "rep")) repair_control();
        else if (!strcmp(cmd, "inv")) inventory_report();
        else if (!strcmp(cmd, "con")) convert_resources();
        else if (!strcmp(cmd, "cal")) navigation_calculator();
        else if (!strcmp(cmd, "apr")) { approach_control(); enemy_ai(); }
        else if (!strcmp(cmd, "sco")) { scoop_control(); enemy_ai(); }
        else if (!strcmp(cmd, "har")) { harvest_control(); enemy_ai(); }
        else if (!strcmp(cmd, "sta")) status_report();
        else if (!strcmp(cmd, "dam")) damage_report();
        else if (!strcmp(cmd, "axs")) toggle_axes();
        else if (!strcmp(cmd, "grd")) toggle_grid();
        else if (!strcmp(cmd, "help")) help_command(arg);
        else if (!strcmp(cmd, "xxx")) ship_destroyed();
        else if (strlen(cmd) > 0) printf("Available: nav, srs, lrs, pha, tor, she, lock, pow, psy, aux, doc, bor, min, rep, inv, con, cal, apr, sco, har, sta, dam, axs, grd, help, xxx\n");
    }
}

void ship_destroyed(void) {
    game_ended = true;
    unlink("startrekultra.sav");
    printf(B_RED "\n*** THE ENTERPRISE HAS BEEN DESTROYED ***\n" RESET);
    printf("The Federation will fall...\n");
    exit(0);
}
void boarding_control(void) {
    if (k3 <= 0) { printf("No enemies in range for boarding.\n"); return; }
    if (system_health[6] < 50.0f) { printf("Transporter system too damaged for boarding operations!\n"); return; }

    int target_idx = -1;
    double min_dist = 100.0;
    for (int i = 1; i <= 3; i++) {
        if (k[i][3] > 0) {
            double dist = sqrt(pow(k[i][0]-s1, 2) + pow(k[i][1]-s2, 2) + pow(k[i][2]-s3, 2));
            if (dist < min_dist) { min_dist = dist; target_idx = i; }
        }
    }

    if (min_dist > 1.0) {
        printf("Target is too far for transporter lock. Distance must be < 1.0 (Current: %.2f).\n", min_dist);
        return;
    }

    printf(B_MAGENTA "\n--- COMMENCING BOARDING OPERATION ---\n" RESET);
    printf("Target: %s at distance %.2f.\n", get_species_name(k[target_idx][4]), min_dist);
    printf("Sending Security Teams... "); fflush(stdout);
    sleep(1);

    int enemy_power = k[target_idx][3];
    
    if (enemy_power > 150) {
        printf("ABORTED!\n");
        printf(B_RED "Enemy internal defenses are too strong! Teams forced to retreat.\n" RESET);
        int losses = rand()%10;
        crew_count -= losses;
        printf("Casualties: %d security personnel.\n", losses);
    } else {
        printf("SUCCESS!\n");
        int losses = rand()%15;
        crew_count -= losses;
        
        /* Capture rewards */
        int e_gain = 500 + rand()%1000;
        int t_gain = 2 + rand()%5;
        int m_gain = 50 + rand()%100;
        
        e += e_gain; if (e > 5000) e = 5000;
        p += t_gain; if (p > 20) p = 20;
        inventory[rand()%5 + 1] += m_gain;

        printf(B_GREEN "*** %s CAPTURED AND DISMANTLED ***\n" RESET, get_species_name(k[target_idx][4]));
        printf("Recovered: %d Energy | %d Torpedoes | %d Minerals.\n", e_gain, t_gain, m_gain);
        printf("Casualties: %d security personnel.\n", losses);

        species_counts[k[target_idx][4] - 10]--;
        if (lock_target == target_idx) {
            printf(B_CYAN "COMPUTER: Target lost. Lock released.\n" RESET);
            lock_target = 0;
        }
        
        /* Trigger Dismantle in Visualizer */
        cur_dismantle[0] = k[target_idx][0]; 
        cur_dismantle[1] = k[target_idx][1]; 
        cur_dismantle[2] = k[target_idx][2];
        dismantle_species = k[target_idx][4];
        
        k[target_idx][3] = 0; k3--; k9--;
        g[q1][q2][q3] -= 100;
        update_3d_view();
    }
}

void won_game(void) {
    game_ended = true;
    unlink("startrekultra.sav");
    printf(B_GREEN "\n*** CONGRATULATIONS CAPTAIN ***\n" RESET);
    printf("The galaxy is safe from the enemy threat.\n");
    exit(0);
}

void safe_gets(char *s, int size) {
    if (fgets(s, size, stdin) != NULL) {
        size_t len = strlen(s);
        if (len > 0 && s[len-1] == '\n') {
            s[len-1] = '\0';
        }
    } else {
        s[0] = '\0';
    }
}

int get_rand(int iSpread) {
    return (rand() % iSpread) + 1;
}
