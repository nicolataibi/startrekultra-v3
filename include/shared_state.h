#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>

#define MAX_OBJECTS 200
#define MAX_BEAMS 10
#define SHM_NAME "/startrek_ultra_shm"

/* 
 * Shared State Structure
 * Replaces the textual format of /tmp/ultra_map.dat
 */

typedef struct {
    float shm_x, shm_y, shm_z;
    float h, m;
    int type; /* 1=Player, 3=Base, 4=Star, 5=Planet, 6=BH, 10+=Enemies */
    int ship_class;
    int active;
    int health_pct;
    int id;
} SharedObject;

typedef struct {
    float shm_tx, shm_ty, shm_tz;
    int active;
} SharedBeam;

typedef struct {
    float shm_x, shm_y, shm_z;
    int active;
} SharedPoint;

typedef struct {
    float shm_x, shm_y, shm_z;
    int species;
    int active;
} SharedDismantle;

typedef struct {
    pthread_mutex_t mutex;
    
    /* UI Info */
    int shm_energy;
    int shm_shields[6];
    int inventory[7];
    int klingons;
    char quadrant[128];
    int shm_show_axes;
    int shm_show_grid;
    int is_cloaked;

    /* Object List */
    int object_count;
    SharedObject objects[MAX_OBJECTS];

    /* FX */
    int beam_count;
    SharedBeam beams[MAX_BEAMS];
    
    SharedPoint torp;
    SharedPoint boom;
    SharedDismantle dismantle;
    
    /* Synchronization counter */
    long long frame_id;
} GameState;

#endif
