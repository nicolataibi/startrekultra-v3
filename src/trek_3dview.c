#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "shared_state.h"
#include "network.h"

/* VBO Globals */
GLuint vbo_stars = 0;
GLuint vbo_grid = 0;
int grid_vertex_count = 0;

int shm_fd = -1;
GameState *g_shared_state = NULL;

volatile int g_data_dirty = 0;
volatile int g_is_loading = 0;
float angleY = 0.0f;
float angleX = 20.0f;
float zoom = -14.0f;
float autoRotate = 0.075f;
float pulse = 0.0f;

int g_energy = 0, g_shields = 0, g_klingons = 0;
int g_show_axes = 0;
int g_show_grid = 0;
int g_show_hud = 1; /* Default HUD ON */
char g_quadrant[128] = "Scanning...";
char g_last_quadrant[128] = "";

#define MAX_TRAIL 40
typedef struct {
    float x, y, z;
    float tx, ty, tz; /* Interpolation targets */
    float h, m;
    float th, tm;     /* Target heading and mark */
    int type;
    int ship_class;
    int health_pct;   /* HUD */
    int id;           /* HUD */
    float trail[MAX_TRAIL][3];
    int trail_ptr;
    int trail_count;
    double last_update_time;
} GameObject;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b;
    int active;
} Particle;

typedef struct {
    float x, y, z;
    int species;
    int timer;
    Particle particles[100];
} Dismantle;

GameObject objects[200];
int objectCount = 0;
Dismantle g_dismantle = {-100, -100, -100, 0, 0};

/* Rimosse variabili globali scia singola per scia universale */
typedef struct { float x, y, z, alpha; } PhaserBeam;
PhaserBeam beams[10];
int beamCount = 0;

typedef struct { float x, y, z; int active; int timer; } ViewPoint;
ViewPoint g_torp = {0,0,0,0,0};
ViewPoint g_boom = {0,0,0,0,0};

float enterpriseX = -100, enterpriseY = -100, enterpriseZ = -100;

float stars[1000][3];

/* Prototypes */
void drawStarbase(float x, float y, float z);
void drawPlanet(float x, float y, float z);
void drawGrid();
void drawKlingon(float x, float y, float z);
void drawRomulan(float x, float y, float z);
void drawBorg(float x, float y, float z);
void drawCardassian(float x, float y, float z);
void drawJemHadar(float x, float y, float z);
void drawTholian(float x, float y, float z);
void drawGorn(float x, float y, float z);
void drawFerengi(float x, float y, float z);
void drawSpecies8472(float x, float y, float z);
void drawBreen(float x, float y, float z);
void drawHirogen(float x, float y, float z);
void drawNacelle(float len, float width, float r, float g, float b);
void drawDeflector(float r, float g, float b);
void drawConstitution();
void drawMiranda();
void drawExcelsior();
void drawConstellation();
void drawDefiant();
void drawGalaxy();
void drawSovereign();
void drawIntrepid();
void drawAkira();
void drawNebula();
void drawAmbassador();
void drawOberth();
void drawSteamrunner();
void drawGlow(float radius, float r, float g, float b, float alpha);
void drawHullDetail(void (*drawFunc)(void), float r, float g, float b);
void drawNavLights(float x, float y, float z);
void drawStarfleetSaucer(float sx, float sy, float sz);
void glutSolidSphere_wrapper_saucer();
void glutSolidSphere_wrapper_hull();
void glutSolidSphere_wrapper_defiant();
void glutSolidCube_wrapper();

void handle_signal(int sig) { if (sig == SIGUSR1) g_data_dirty = 1; }

void initStars() {
    for(int i=0; i<1000; i++) {
        float r = 150.0f + (float)(rand()%100);
        float t = (float)(rand()%360) * 3.14/180;
        float p = (float)(rand()%360) * 3.14/180;
        stars[i][0] = r * sin(p) * cos(t);
        stars[i][1] = r * sin(p) * sin(t);
        stars[i][2] = r * cos(p);
    }
}

void initVBOs() {
    glGenBuffers(1, &vbo_stars);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_stars);
    glBufferData(GL_ARRAY_BUFFER, sizeof(stars), stars, GL_STATIC_DRAW);
    int max_verts = 12 * 12 * 3 * 2; 
    float *grid_data = malloc(max_verts * 3 * sizeof(float));
    int idx = 0;
    for(int i=0; i<=11; i++) {
        float p = -5.5f + (float)i;
        for(int j=0; j<=11; j++) {
            float q = -5.5f + (float)j;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = -5.5f;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = 5.5f;
            grid_data[idx++] = p; grid_data[idx++] = -5.5f; grid_data[idx++] = q;
            grid_data[idx++] = p; grid_data[idx++] = 5.5f; grid_data[idx++] = q;
            grid_data[idx++] = -5.5f; grid_data[idx++] = p; grid_data[idx++] = q;
            grid_data[idx++] = 5.5f; grid_data[idx++] = p; grid_data[idx++] = q;
        }
    }
    grid_vertex_count = idx / 3;
    glGenBuffers(1, &vbo_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glBufferData(GL_ARRAY_BUFFER, idx * sizeof(float), grid_data, GL_STATIC_DRAW);
    free(grid_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

long long last_frame_id = -1;

void loadGameState() {
    if (!g_shared_state) return;
    pthread_mutex_lock(&g_shared_state->mutex);
    if (g_shared_state->frame_id == last_frame_id) { pthread_mutex_unlock(&g_shared_state->mutex); return; }
    last_frame_id = g_shared_state->frame_id;
    g_is_loading = 1;
    g_energy = g_shared_state->shm_energy;
    int total_s = 0;
    for(int s=0; s<6; s++) total_s += g_shared_state->shm_shields[s];
    g_shields = total_s / 6;
    g_klingons = g_shared_state->klingons;
    
    int quadrant_changed = 0;
    if (strcmp(g_quadrant, g_shared_state->quadrant) != 0) {
        quadrant_changed = 1;
        strcpy(g_quadrant, g_shared_state->quadrant);
    }
    
    g_show_axes = g_shared_state->shm_show_axes;
    g_show_grid = g_shared_state->shm_show_grid;
    objectCount = g_shared_state->object_count;
    for(int i=0; i<objectCount; i++) {
        float next_x = g_shared_state->objects[i].shm_x - 5.5f;
        float next_y = g_shared_state->objects[i].shm_z - 5.5f;
        float next_z = 5.5f - g_shared_state->objects[i].shm_y;
        
        /* Se è la prima volta che vediamo l'oggetto o se ha fatto un salto enorme (cambio quadrante) */
        float dx = next_x - objects[i].x;
        float dy = next_y - objects[i].y;
        float dz = next_z - objects[i].z;
        
        if (quadrant_changed || objects[i].x < -50.0f || (dx*dx + dy*dy + dz*dz) > 25.0f) {
            objects[i].x = objects[i].tx = next_x;
            objects[i].y = objects[i].ty = next_y;
            objects[i].z = objects[i].tz = next_z;
            /* Reset trails on quadrant jump */
            objects[i].trail_count = 0;
            objects[i].trail_ptr = 0;
        } else {
            objects[i].tx = next_x;
            objects[i].ty = next_y;
            objects[i].tz = next_z;
        }

        objects[i].th = g_shared_state->objects[i].h;
        objects[i].tm = g_shared_state->objects[i].m;
        objects[i].last_update_time = glutGet(GLUT_ELAPSED_TIME);
        objects[i].type = g_shared_state->objects[i].type;
        objects[i].ship_class = g_shared_state->objects[i].ship_class;
        objects[i].health_pct = g_shared_state->objects[i].health_pct;
        objects[i].id = g_shared_state->objects[i].id; 
        if (i == 0) { enterpriseX = objects[i].x; enterpriseY = objects[i].y; enterpriseZ = objects[i].z; }
    }
    if (g_shared_state->beam_count > 0) {
        for(int i=0; i<g_shared_state->beam_count; i++) {
            int slot = -1;
            for(int j=0; j<10; j++) if(beams[j].alpha <= 0) { slot = j; break; }
            if (slot == -1) slot = rand()%10;
            beams[slot].x = g_shared_state->beams[i].shm_tx - 5.5f;
            beams[slot].y = g_shared_state->beams[i].shm_tz - 5.5f;
            beams[slot].z = 5.5f - g_shared_state->beams[i].shm_ty;
            beams[slot].alpha = 1.5f;
        }
        /* Consume events */
        g_shared_state->beam_count = 0;
    }
    if (g_shared_state->torp.active) {
        g_torp.x = g_shared_state->torp.shm_x - 5.5f;
        g_torp.y = g_shared_state->torp.shm_z - 5.5f;
        g_torp.z = 5.5f - g_shared_state->torp.shm_y;
        g_torp.active = 1;
    } else g_torp.active = 0;
    if (g_shared_state->boom.active) {
        g_boom.x = g_shared_state->boom.shm_x - 5.5f;
        g_boom.y = g_shared_state->boom.shm_z - 5.5f;
        g_boom.z = 5.5f - g_shared_state->boom.shm_y;
        g_boom.active = 1;
        g_boom.timer = 40; /* 1.3 seconds approx */
        /* Consume event */
        g_shared_state->boom.active = 0;
    }

    /* Dismantle */
    if (g_shared_state->dismantle.active) {
        g_dismantle.x = g_shared_state->dismantle.shm_x - 5.5f;
        g_dismantle.y = g_shared_state->dismantle.shm_z - 5.5f;
        g_dismantle.z = 5.5f - g_shared_state->dismantle.shm_y;
        g_dismantle.species = g_shared_state->dismantle.species;
        g_dismantle.timer = 60;
        for(int i=0; i<100; i++) {
            g_dismantle.particles[i].x = g_dismantle.x;
            g_dismantle.particles[i].y = g_dismantle.y;
            g_dismantle.particles[i].z = g_dismantle.z;
            g_dismantle.particles[i].vx = ((rand()%100)-50)/500.0f;
            g_dismantle.particles[i].vy = ((rand()%100)-50)/500.0f;
            g_dismantle.particles[i].vz = ((rand()%100)-50)/500.0f;
            g_dismantle.particles[i].r = (rand()%100)/100.0f;
            g_dismantle.particles[i].g = (rand()%100)/100.0f;
            g_dismantle.particles[i].b = (rand()%100)/100.0f;
            g_dismantle.particles[i].active = 1;
        }
        /* Reset event to prevent re-triggering */
        g_shared_state->dismantle.active = 0;
    }
    pthread_mutex_unlock(&g_shared_state->mutex);
    kill(getppid(), SIGUSR2);
    g_is_loading = 0;
}

void drawText3D(float x, float y, float z, const char* text) {
    glRasterPos3f(x, y, z);
    for(int i=0; i<strlen(text); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, text[i]);
}

const char* getSpeciesName(int s) {
    switch(s) {
        case 1: return "Player"; case 3: return "Starbase"; case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 10: return "Klingon"; case 11: return "Romulan"; case 12: return "Borg";
        case 13: return "Cardassian"; case 14: return "Jem'Hadar"; case 15: return "Tholian";
        case 16: return "Gorn"; case 17: return "Ferengi"; case 18: return "Species 8472";
        case 19: return "Breen"; case 20: return "Hirogen";
        default: return "Unknown";
    }
}

void drawHUD(float x, float y, float z, int type, int id, int hp) {
    GLdouble model[16], proj[16];
    GLint view[4];
    GLdouble winX, winY, winZ;

    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    if (gluProject(x, y, z + 0.8f, model, proj, view, &winX, &winY, &winZ) == GL_TRUE) {
        /* Check if behind camera */
        if (winZ > 1.0) return;

        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        gluOrtho2D(0, view[2], 0, view[3]);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);

        /* Draw Name/ID */
        char buf[64];
        if (type == 1) sprintf(buf, "Vessel %d (Player)", id);
        else sprintf(buf, "%s [%d]", getSpeciesName(type), id);
        
        glColor3f(0.0f, 1.0f, 1.0f);
        glRasterPos2f(winX - (strlen(buf)*4), winY + 15);
        for(int i=0; i<strlen(buf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, buf[i]);

        /* Draw Health Bar (Only for ships/bases) */
        if (type == 1 || type == 3 || type >= 10) {
            float w = 40.0f;
            float h = 4.0f;
            float bar = (hp / 100.0f) * w;
            if (bar < 0) bar = 0; if (bar > w) bar = w;

            /* Border */
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX + w/2, winY);
            glVertex2f(winX + w/2, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Fill */
            if (hp > 50) glColor3f(0.0f, 1.0f, 0.0f);
            else if (hp > 25) glColor3f(1.0f, 1.0f, 0.0f);
            else glColor3f(1.0f, 0.0f, 0.0f);

            glBegin(GL_QUADS);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX - w/2 + bar, winY);
            glVertex2f(winX - w/2 + bar, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();
        }

        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
}

void drawCompass() {
    glDisable(GL_LIGHTING);
    glColor3f(0, 1, 0); glBegin(GL_LINES); glVertex3f(-5.5,0,0); glVertex3f(5.5,0,0); glEnd();
    glColor3f(0, 0, 1); glBegin(GL_LINES); glVertex3f(0,0,-5.5); glVertex3f(0,0,5.5); glEnd();
    glColor3f(1, 0, 0); glBegin(GL_LINES); glVertex3f(0,-5.5,0); glVertex3f(0,5.5,0); glEnd();
    glEnable(GL_LIGHTING);
}

void drawGlow(float radius, float r, float g, float b, float alpha) {
    glDisable(GL_LIGHTING);
    for (int i = 1; i <= 5; i++) {
        float s = radius * (1.0f + i * 0.2f);
        glColor4f(r, g, b, alpha / (i * 1.5f));
        glutSolidSphere(s, 16, 16);
    }
    glEnable(GL_LIGHTING);
}

void glutSolidSphere_wrapper_saucer() { glutSolidSphere(0.5, 40, 40); }
void glutSolidSphere_wrapper_hull() { glutSolidSphere(0.15, 20, 20); }
void glutSolidSphere_wrapper_defiant() { glutSolidSphere(0.35, 30, 30); }
void glutSolidCube_wrapper() { glutSolidCube(0.5); }

void drawHullDetail(void (*drawFunc)(void), float r, float g, float b) {
    glColor3f(r, g, b); drawFunc();
    glDisable(GL_LIGHTING); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor4f(r*1.2f, g*1.2f, b*1.2f, 0.15f); drawFunc();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glEnable(GL_LIGHTING);
}

void drawNavLights(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glColor3f(1, 0, 0); glPushMatrix(); glTranslatef(x, y, z); glutSolidSphere(0.03, 8, 8); glPopMatrix();
    glColor3f(0, 1, 0); glPushMatrix(); glTranslatef(x, y, -z); glutSolidSphere(0.03, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawNacelle(float len, float width, float r, float g, float b) {
    /* Corpo della gondola (Scalato) */
    glPushMatrix(); 
    glScalef(len, width, width); 
    glColor3f(0.4f, 0.4f, 0.45f); 
    glutSolidSphere(0.1, 16, 16); 
    glPopMatrix();

    /* Glow posteriore (Non deformato, posizionato alla fine) */
    glPushMatrix(); 
    glTranslatef(-0.1f * len, 0, 0); 
    drawGlow(0.07f, r, g, b, 0.3f); 
    glPopMatrix();

    /* Collettore di Bussard anteriore (Non deformato, sferico, posizionato in punta) */
    glPushMatrix(); 
    glTranslatef(0.1f * len, 0, 0); 
    glColor3f(0.8f, 0.0f, 0.0f); 
    glutSolidSphere(0.04, 12, 12); 
    drawGlow(0.05f, 1.0f, 0.2f, 0.0f, 0.3f); 
    glPopMatrix();
}

void drawDeflector(float r, float g, float b) {
    glDisable(GL_LIGHTING); glColor3f(r, g, b); glutSolidSphere(0.12, 16, 16); glEnable(GL_LIGHTING);
}

void drawStarfleetSaucer(float sx, float sy, float sz) {
    glPushMatrix(); glScalef(sx, sy, sz); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.88f, 0.88f, 0.92f); glPopMatrix();
    
    /* Luci di posizione superiori */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.0f, 0.0f); /* Rosso (Port) */
    glPushMatrix(); glTranslatef(0, 0.12f, 0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glColor3f(0.0f, 1.0f, 0.0f); /* Verde (Starboard) */
    glPushMatrix(); glTranslatef(0, 0.12f, -0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawConstitution() {
    drawStarfleetSaucer(1.0f, 0.15f, 1.0f);
    glDisable(GL_LIGHTING); glColor3f(0.0f, 0.5f, 1.0f); glPushMatrix(); glTranslatef(0, 0.1f, 0); glutSolidSphere(0.08, 12, 12); drawGlow(0.06, 0, 0.5, 1, 0.4); glPopMatrix(); glEnable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(-0.2f, -0.1f, 0); glScalef(0.4f, 0.3f, 0.1f); glColor3f(0.8f, 0.8f, 0.85f); glutSolidCube(0.5); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.45f, -0.25f, 0); glScalef(1.8f, 0.8f, 0.8f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.15f, -0.25f, 0); drawDeflector(1.0f, 0.4f, 0.0f); drawGlow(0.1f, 1.0f, 0.3f, 0.0f, 0.5f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.5f, -0.1f, side * 0.15f); glRotatef(side*30, 1, 0, 0); glScalef(0.1f, 0.4f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.6f, 0.15f, side * 0.38f); drawNacelle(4.8, 0.28, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawMiranda() { 
    drawStarfleetSaucer(1.2f, 0.18f, 1.1f);
    glPushMatrix(); glTranslatef(-0.25f, 0.2f, 0); glScalef(0.3f, 0.5f, 0.9f); glColor3f(0.75f, 0.75f, 0.8f); glutSolidCube(0.5); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.25f, 0.1f, side * 0.3f); glScalef(0.1f, 0.4f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35f, -0.15f, side * 0.45f); drawNacelle(3.8, 0.4, 0.3, 0.4, 0.8); glPopMatrix();
    }
}

void drawExcelsior() { 
    drawStarfleetSaucer(1.4f, 0.12f, 1.3f);
    glPushMatrix(); glTranslatef(-0.35f, -0.15f, 0); glScalef(0.7f, 0.2f, 0.1f); glutSolidCube(0.5); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.7f, -0.3f, 0); glScalef(2.8f, 0.7f, 0.7f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, -0.3f, 0); drawDeflector(0.0f, 0.6f, 1.0f); drawGlow(0.1f, 0.2f, 0.7f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.8f, -0.15f, side * 0.35f); drawNacelle(5.5, 0.25, 0.2, 0.6, 1.0); glPopMatrix();
    }
}

void drawConstellation() {
    glPushMatrix(); glScalef(1.2f, 0.4f, 0.9f); drawHullDetail(glutSolidCube_wrapper, 0.75f, 0.75f, 0.8f); glPopMatrix();
    for(int updown=-1; updown<=1; updown+=2) {
        for(int side=-1; side<=1; side+=2) {
            glPushMatrix(); glTranslatef(-0.2f, updown * 0.25f, side * 0.35f); drawNacelle(3.5, 0.3, 0.5, 0.5, 0.6); glPopMatrix();
        }
    }
}

void drawDefiant() {
    glPushMatrix(); glTranslatef(0.3f, 0, 0); glScalef(0.5f, 0.4f, 0.4f); drawDeflector(1.0f, 0.3f, 0.0f); drawGlow(0.15f, 1.0f, 0.2f, 0.0f, 0.4f); glPopMatrix();
    glPushMatrix(); glScalef(1.5f, 0.5f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_defiant, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.1f, -0.05f, side * 0.4f); drawNacelle(2.5, 0.6, 0.2, 0.3, 0.7); glPopMatrix();
    }
}

void drawGalaxy() {
    drawStarfleetSaucer(1.6f, 0.15f, 2.4f);
    glPushMatrix(); glTranslatef(-0.4f, -0.15f, 0); glColor3f(0.85f, 0.85f, 0.9f); glScalef(0.6f, 0.5f, 0.4f); glutSolidCube(0.5); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.7f, -0.3f, 0); glScalef(2.0f, 1.0f, 1.0f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.9f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.4f, -0.35f, 0); glScalef(1.0f, 0.7f, 1.5f); drawDeflector(0.9f, 0.6f, 0.1f); drawGlow(0.12f, 0.8f, 0.5f, 0.0f, 0.5f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.8f, -0.05f, side * 0.65f); drawNacelle(4.0, 0.4, 0.4, 0.6, 1.0); glPopMatrix();
    }
}

void drawSovereign() {
    drawStarfleetSaucer(2.2f, 0.12f, 1.3f);
    glPushMatrix(); glTranslatef(-0.7f, -0.15f, 0); glScalef(2.5f, 0.5f, 0.6f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.9f, 0.9f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, -0.15f, 0); drawDeflector(0.0f, 0.4f, 0.8f); drawGlow(0.1f, 0.2f, 0.6f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-1.0f, 0.05f, side * 0.45f); drawNacelle(6.0, 0.2, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawIntrepid() {
    drawStarfleetSaucer(2.0f, 0.15f, 1.0f);
    glPushMatrix(); glTranslatef(-0.6f, -0.15f, 0); glScalef(1.8f, 0.4f, 0.5f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.25f, -0.15f, 0); drawDeflector(0.0f, 0.5f, 0.9f); drawGlow(0.1f, 0.3f, 0.7f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.8f, 0.1f, side * 0.4f); glRotatef(side*25, 1, 0, 0);
        drawNacelle(3.5, 0.25, 0.3, 0.6, 1.0); glPopMatrix();
    }
}

void drawAkira() {
    glColor3f(0.6f, 0.6f, 0.7f); glPushMatrix(); glScalef(1.4f, 0.2f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.6f, 0.6f, 0.7f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.6f, -0.2f, side * 0.7f); drawNacelle(4.5, 0.4, 0.4, 0.4, 0.8); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.2f, -0.1f, side * 0.5f); glRotatef(side*30, 1,0,0); glScalef(0.2f, 0.6f, 0.2f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawNebula() {
    drawGalaxy();
    glPushMatrix(); glTranslatef(-0.6f, 0.4f, 0); glColor3f(0.7f, 0.7f, 0.75f);
    glPushMatrix(); glScalef(0.8f, 0.15f, 0.8f); glutSolidSphere(0.5, 24, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.2f, 0); glScalef(0.1f, 0.4f, 0.4f); glutSolidCube(1.0); glPopMatrix();
    glPopMatrix();
}

void drawAmbassador() {
    drawStarfleetSaucer(1.4f, 0.2f, 1.4f);
    glPushMatrix(); glTranslatef(-0.45f, -0.25f, 0); glScalef(1.6f, 0.8f, 0.8f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.15f, -0.25f, 0); drawDeflector(0.0f, 0.3f, 0.7f); drawGlow(0.1f, 0.2f, 0.5f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, 0.05f, side * 0.45f); drawNacelle(4.5, 0.35, 0.3, 0.4, 0.9); glPopMatrix();
    }
}

void drawOberth() {
    glColor3f(0.9f, 0.9f, 0.9f); glPushMatrix(); glScalef(1.1f, 0.15f, 0.9f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.9f, 0.9f, 0.9f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.5f, 0); glScalef(1.2f, 0.3f, 0.6f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(0, -0.25f, side * 0.35f); glScalef(0.15f, 0.6f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35f, -0.25f, side * 0.4f); drawNacelle(2.5, 0.2, 0.4, 0.4, 0.7); glPopMatrix();
    }
}

void drawSteamrunner() {
    glColor3f(0.6f, 0.6f, 0.65f); glPushMatrix(); glScalef(1.6f, 0.25f, 1.5f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, -0.05f, side * 0.55f); drawNacelle(3.5, 0.3, 0.2, 0.3, 0.6); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.8f, 0, 0); glScalef(0.1f, 0.1f, 1.0f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawFederationShip(int class, float h, float m) {
    glRotatef(h - 90.0f, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    switch(class) {
        case SHIP_CLASS_CONSTITUTION: drawConstitution(); break;
        case SHIP_CLASS_MIRANDA:      drawMiranda(); break;
        case SHIP_CLASS_EXCELSIOR:    drawExcelsior(); break;
        case SHIP_CLASS_CONSTELLATION: drawConstellation(); break;
        case SHIP_CLASS_DEFIANT:      drawDefiant(); break;
        case SHIP_CLASS_GALAXY:       drawGalaxy(); break;
        case SHIP_CLASS_SOVEREIGN:    drawSovereign(); break;
        case SHIP_CLASS_INTREPID:     drawIntrepid(); break;
        case SHIP_CLASS_AKIRA:        drawAkira(); break;
        case SHIP_CLASS_NEBULA:       drawNebula(); break;
        case SHIP_CLASS_AMBASSADOR:   drawAmbassador(); break;
        case SHIP_CLASS_OBERTH:       drawOberth(); break;
        case SHIP_CLASS_STEAMRUNNER:  drawSteamrunner(); break;
        default:                      drawConstitution(); break;
    }
}

void drawKlingon(float x, float y, float z) {
    glPushMatrix(); glColor3f(0.6f, 0.1f, 0.0f); glScalef(1.0f, 0.3f, 1.5f); glutSolidSphere(0.3, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0); glScalef(2.0f, 0.2f, 0.2f); glutSolidSphere(0.15, 8, 8); glPopMatrix();
    glColor3f(0.8f, 0.0f, 0.0f); glPushMatrix(); glTranslatef(0.7f, 0, 0); glScalef(1.0f, 0.5f, 1.2f); glutSolidSphere(0.15, 12, 12); glPopMatrix();
}

void drawRomulan(float x, float y, float z) {
    glColor3f(0.0f, 0.5f, 0.0f); glPushMatrix(); glScalef(1.5f, 0.2f, 1.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.25f, 0); glScalef(1.5f, 0.2f, 0.8f); glutSolidSphere(0.35, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0.1f, 0); glScalef(1.0f, 0.5f, 0.2f); glutSolidCube(0.3); glPopMatrix();
    glColor3f(0.0f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.7f, 0.1f, 0); glutSolidCone(0.1, 0.3, 8, 8); glPopMatrix();
}

void drawBorg(float x, float y, float z) {
    glRotatef(pulse*5, 1, 1, 1); glColor3f(0.15f, 0.15f, 0.15f); glutWireCube(0.85);
    glColor3f(0.05f, 0.05f, 0.05f); glutSolidCube(0.75); glDisable(GL_LIGHTING);
    float p = (sin(pulse)+1.0f)*0.5f; glColor4f(0.0f, 0.8f * p, 0.0f, 0.6f); glutWireCube(0.8);
    for(int i=0; i<6; i++) {
        glPushMatrix(); if(i==0) glTranslatef(0.38,0,0); else if(i==1) glTranslatef(-0.38,0,0); else if(i==2) glTranslatef(0,0.38,0); else if(i==3) glTranslatef(0,-0.38,0); else if(i==4) glTranslatef(0,0,0.38); else if(i==5) glTranslatef(0,0,-0.38);
        glColor3f(0, 1, 0); glutSolidSphere(0.04, 8, 8); drawGlow(0.03, 0, 1, 0, 0.4); glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

void drawCardassian(float x, float y, float z) {
    glColor3f(0.6f, 0.5f, 0.3f); glPushMatrix(); glScalef(2.0f, 0.2f, 1.2f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glColor3f(0.8f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.5f, 0, 0); glScalef(1.0f, 0.4f, 0.4f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
}

void drawJemHadar(float x, float y, float z) {
    glColor3f(0.4f, 0.4f, 0.6f); glPushMatrix(); glScalef(1.2f, 0.5f, 1.0f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, -0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
}

void drawTholian(float x, float y, float z) {
    glRotatef(pulse*15, 0, 1, 0); glColor4f(1.0f, 0.5f, 0.0f, 0.6f); glDisable(GL_LIGHTING); glutWireOctahedron();
    glColor4f(1.0f, 0.2f, 0.0f, 0.4f); glutSolidOctahedron(); glEnable(GL_LIGHTING);
}

void drawGorn(float x, float y, float z) {
    glColor3f(0.3f, 0.4f, 0.1f); glPushMatrix(); glScalef(1.5f, 0.6f, 0.6f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, 0, 0); glScalef(0.5f, 1.2f, 1.5f); glutSolidCube(0.3); glPopMatrix();
}

void drawFerengi(float x, float y, float z) {
    glColor3f(0.7f, 0.3f, 0.1f); glPushMatrix(); glScalef(1.0f, 0.2f, 2.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.3f, 0, 0); glScalef(1.2f, 0.4f, 0.6f); glutSolidSphere(0.3, 12, 12); glPopMatrix();
}

void drawSpecies8472(float x, float y, float z) {
    glRotatef(pulse*10, 1, 0, 1); glColor3f(0.8f, 0.8f, 0.2f); for(int i=0; i<3; i++) { glPushMatrix(); glRotatef(i*120, 0, 1, 0); glTranslatef(0.3f, 0, 0); glScalef(2.0f, 0.3f, 0.3f); glutSolidSphere(0.15, 12, 12); glPopMatrix(); }
    glutSolidSphere(0.2, 12, 12);
}

void drawBreen(float x, float y, float z) {
    glColor3f(0.4f, 0.5f, 0.4f); glPushMatrix(); glScalef(1.8f, 0.2f, 0.8f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(0.2f, 0.1f, 0.2f); glScalef(0.5f, 0.5f, 1.2f); glutSolidSphere(0.2, 8, 8); glPopMatrix();
}

void drawHirogen(float x, float y, float z) {
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(2.5f, 0.15f, 0.4f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.4f, 0, 0); glScalef(0.5f, 0.8f, 1.5f); glutSolidCube(0.2); glPopMatrix();
}

void drawStarbase(float x, float y, float z) {
    glRotatef(pulse*10, 0, 1, 0); glColor3f(0.9f, 0.9f, 0.1f); glutWireSphere(0.4, 12, 12);
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(1.5f, 0.1f, 1.5f); glutSolidCube(0.6); glPopMatrix();
}

void drawStar(float x, float y, float z) {
    glPushMatrix();
    /* Core (Bright White-Yellow) */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.8f); 
    glutSolidSphere(0.2, 16, 16);
    
    /* Corona / Halo (Pulsing Yellow-Orange) */
    float p = 1.0f + sin(pulse * 3.0f) * 0.1f; /* Pulsazione */
    
    /* Inner Corona */
    glColor4f(1.0f, 0.8f, 0.0f, 0.4f);
    glutSolidSphere(0.35 * p, 24, 24);
    
    /* Outer Corona (Fading) */
    glColor4f(1.0f, 0.6f, 0.0f, 0.2f);
    glutSolidSphere(0.6 * p, 24, 24);

    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawPlanet(float x, float y, float z) {
    glRotatef(pulse*5, 0, 1, 0); glColor3f(0.2f, 0.6f, 0.3f); glutSolidSphere(0.6, 24, 24);
    glDisable(GL_LIGHTING); glColor4f(0.4f, 0.8f, 1.0f, 0.3f); glutSolidSphere(0.65, 24, 24); glEnable(GL_LIGHTING);
}

void drawBlackHole(float x, float y, float z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    
    /* Event Horizon - Solid Black */
    glDisable(GL_LIGHTING);
    glColor3f(0.0f, 0.0f, 0.0f);
    glutSolidSphere(0.4, 32, 32);
    
    /* Accretion Disk - Rotating Rings */
    glRotatef(pulse*20, 1, 1, 0);
    for(int i=0; i<5; i++) {
        float r = 0.5f + i*0.15f + sin(pulse*2)*0.05f;
        glColor4f(0.6f - i*0.1f, 0.0f, 0.8f, 0.5f - i*0.1f);
        glutWireTorus(0.02, r, 10, 40);
    }
    
    /* Gravitational Lensing / Glow */
    glColor4f(0.3f, 0.0f, 0.5f, 0.2f);
    glutSolidSphere(1.2, 20, 20);
    
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawGrid() {
    glDisable(GL_LIGHTING); glColor4f(0.5f, 0.5f, 0.5f, 0.2f);
    if (vbo_grid != 0) { glEnableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER, vbo_grid); glVertexPointer(3, GL_FLOAT, 0, 0); glDrawArrays(GL_LINES, 0, grid_vertex_count); glBindBuffer(GL_ARRAY_BUFFER, 0); glDisableClientState(GL_VERTEX_ARRAY); }
    glEnable(GL_LIGHTING);
}

void drawShipTrail(int obj_idx) {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    
    float r=0.4, g=0.7, b=1.0; /* Default Starfleet Blue */
    if (objects[obj_idx].type == 10) { r=1.0; g=0.1; b=0.0; } /* Klingon Red */
    if (objects[obj_idx].type == 11) { r=0.0; g=1.0; b=0.2; } /* Romulan Green */
    if (objects[obj_idx].type == 12) { r=0.0; g=0.8; b=0.8; } /* Borg Cyan */

    for (int i = 0; i < objects[obj_idx].trail_count; i++) {
        int idx = (objects[obj_idx].trail_ptr - 1 - i + MAX_TRAIL) % MAX_TRAIL;
        float alpha = (1.0f - (float)i / objects[obj_idx].trail_count) * 0.5f;
        glColor4f(r, g, b, alpha);
        glVertex3f(objects[obj_idx].trail[idx][0], objects[obj_idx].trail[idx][1], objects[obj_idx].trail[idx][2]);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawPhaserBeams() {
    glDisable(GL_LIGHTING);
    for (int i = 0; i < 10; i++) {
        if (beams[i].alpha > 0) {
            glLineWidth(4.0f); glColor4f(1.0f, 0.8f, 0.0f, (beams[i].alpha > 1.0f) ? 1.0f : beams[i].alpha);
            glBegin(GL_LINES); glVertex3f(enterpriseX, enterpriseY, enterpriseZ); glVertex3f(beams[i].x, beams[i].y, beams[i].z); glEnd();
        }
    }
    glEnable(GL_LIGHTING);
}

void drawExplosion() {
    if (g_boom.timer <= 0) return;
    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_boom.x, g_boom.y, g_boom.z);
    /* Pulsing expansion */
    float s = 0.5f + (40 - g_boom.timer) * 0.05f;
    float alpha = g_boom.timer / 40.0f;
    glColor4f(1.0f, 0.5f, 0.0f, alpha); 
    glutSolidSphere(s, 16, 16);
    glPopMatrix(); glEnable(GL_LIGHTING);
}

void drawTorpedo() {
    if (!g_torp.active) return;
    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_torp.x, g_torp.y, g_torp.z);
    
    /* Core */
    glColor3f(1.0f, 0.8f, 0.6f); 
    glutSolidSphere(0.08, 8, 8);
    
    /* Glow */
    drawGlow(0.15f, 1.0f, 0.2f, 0.0f, 0.6f);
    
    glPopMatrix(); glEnable(GL_LIGHTING);
}

void drawDismantle() {
    if (g_dismantle.timer <= 0) return;
    glDisable(GL_LIGHTING);
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for(int i=0; i<100; i++) {
        if (g_dismantle.particles[i].active) {
            glColor4f(g_dismantle.particles[i].r, g_dismantle.particles[i].g, g_dismantle.particles[i].b, g_dismantle.timer/60.0f);
            glVertex3f(g_dismantle.particles[i].x, g_dismantle.particles[i].y, g_dismantle.particles[i].z);
        }
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void display() {
    if (g_data_dirty) { loadGameState(); g_data_dirty = 0; }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glLoadIdentity(); glTranslatef(0, 0, zoom); glRotatef(angleX, 1, 0, 0); glRotatef(angleY, 0, 1, 0);
    glDisable(GL_LIGHTING);
    if (vbo_stars != 0) { glColor3f(0.7,0.7,0.7); glEnableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER, vbo_stars); glVertexPointer(3, GL_FLOAT, 0, 0); glDrawArrays(GL_POINTS, 0, 1000); glBindBuffer(GL_ARRAY_BUFFER, 0); glDisableClientState(GL_VERTEX_ARRAY); }
    glColor3f(0.2, 0.2, 0.5); glutWireCube(11.0);
    if (g_show_axes) drawCompass();
    if (g_show_grid) drawGrid();
    
    drawPhaserBeams();
    drawExplosion();
    drawTorpedo();
    drawDismantle();

    /* Render Trails */
    for(int k=0; k<objectCount; k++) {
        if (objects[k].type == 1 || objects[k].type >= 10) drawShipTrail(k);
    }

    glEnable(GL_LIGHTING);
    for(int i=0; i<objectCount; i++) {
        glPushMatrix(); glTranslatef(objects[i].x, objects[i].y, objects[i].z);
        if (objects[i].type == 1) { 
            if (i == 0 && g_shared_state->is_cloaked) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(0.5f, 0.8f, 1.0f, 0.3f); }
            drawFederationShip(objects[i].ship_class, objects[i].h, objects[i].m);
            if (i == 0 && g_shared_state->is_cloaked) glDisable(GL_BLEND);
        } else {
            glRotatef(objects[i].h - 90.0f, 0, 1, 0); glRotatef(objects[i].m, 0, 0, 1);
            switch(objects[i].type) {
                case 3: drawStarbase(0,0,0); break;
                case 4: drawStar(0,0,0); break;
                case 5: drawPlanet(0,0,0); break;
                case 6: drawBlackHole(0,0,0); break;
                case 10: drawKlingon(0,0,0); break;
                case 11: drawRomulan(0,0,0); break;
                case 12: drawBorg(0,0,0); break;
                case 13: drawCardassian(0,0,0); break;
                case 14: drawJemHadar(0,0,0); break;
                case 15: drawTholian(0,0,0); break;
                case 16: drawGorn(0,0,0); break;
                case 17: drawFerengi(0,0,0); break;
                case 18: drawSpecies8472(0,0,0); break;
                case 19: drawBreen(0,0,0); break;
                case 20: drawHirogen(0,0,0); break;
            }
        }
        glPopMatrix();
    }
    
    /* Draw HUD Overlay */
    if (g_show_hud) {
        for(int i=0; i<objectCount; i++) {
            if (objects[i].type != 0 && !g_is_loading) {
                drawHUD(objects[i].x, objects[i].y, objects[i].z, objects[i].type, objects[i].id, objects[i].health_pct);
            }
        }
    }

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING); glColor3f(0, 1, 1); drawText3D(20, 960, 0, "STAR TREK ULTRA: MULTI-USER TACTICAL");
    char buf[256]; sprintf(buf, "QUADRANT: %s", g_quadrant); drawText3D(20, 930, 0, buf);
    sprintf(buf, "ENERGY: %d  SHIELDS: %d", g_energy, g_shields); drawText3D(20, 905, 0, buf);
    sprintf(buf, "ENEMIES REMAINING: %d", g_klingons); drawText3D(20, 880, 0, buf);
    
    glColor3f(1, 1, 1);
    drawText3D(20, 50, 0, "Arrows: Rotate | W/S: Zoom | SPACE: Pause | H: Toggle HUD | ESC: Exit");

    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_LIGHTING); glutSwapBuffers();
}

void timer(int v) { 
    angleY += autoRotate; pulse += 0.05; 
    
    /* Fade out beams */
    for (int i = 0; i < 10; i++) if (beams[i].alpha > 0) beams[i].alpha -= 0.05f;

    /* Update Boom Timer */
    if (g_boom.timer > 0) g_boom.timer--;

    /* Update Dismantle */
    if (g_dismantle.timer > 0) {
        g_dismantle.timer--;
        for(int i=0; i<100; i++) {
            if (g_dismantle.particles[i].active) {
                g_dismantle.particles[i].x += g_dismantle.particles[i].vx;
                g_dismantle.particles[i].y += g_dismantle.particles[i].vy;
                g_dismantle.particles[i].z += g_dismantle.particles[i].vz;
            }
        }
    }

    /* Update Objects with Interpolation */
    for (int i = 0; i < objectCount; i++) {
        /* Interpolazione fluida per la posizione (LERP) */
        float interp_speed = 0.12f;
        objects[i].x += (objects[i].tx - objects[i].x) * interp_speed;
        objects[i].y += (objects[i].ty - objects[i].y) * interp_speed;
        objects[i].z += (objects[i].tz - objects[i].z) * interp_speed;
        
        /* Interpolazione fluida per l'orientamento (Heading/Mark) */
        float dh = objects[i].th - objects[i].h;
        if (dh > 180.0f) dh -= 360.0f;
        if (dh < -180.0f) dh += 360.0f;
        objects[i].h += dh * 0.08f;
        if (objects[i].h >= 360.0f) objects[i].h -= 360.0f;
        if (objects[i].h < 0.0f) objects[i].h += 360.0f;

        objects[i].m += (objects[i].tm - objects[i].m) * 0.08f;
        
        if (i == 0) { enterpriseX = objects[i].x; enterpriseY = objects[i].y; enterpriseZ = objects[i].z; }

        if (objects[i].type == 1 || objects[i].type >= 10) {
            /* Check for jump only if we have a history */
            if (objects[i].trail_count > 0) {
                float lastX = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][0];
                float lastY = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][1];
                float lastZ = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][2];

                /* Rilevamento salto (cambio quadrante o teletrasporto) */
                float dx = objects[i].x - lastX;
                float dy = objects[i].y - lastY;
                float dz = objects[i].z - lastZ;
                float dist_sq = dx*dx + dy*dy + dz*dz;
                if (dist_sq > 25.0f) {
                    objects[i].trail_count = 0;
                    objects[i].trail_ptr = 0;
                }
            }

            static int trail_tick = 0;
            if (trail_tick % 2 == 0) { /* Riduciamo la densità della scia per fluidità visiva */
                objects[i].trail[objects[i].trail_ptr][0] = objects[i].x;
                objects[i].trail[objects[i].trail_ptr][1] = objects[i].y;
                objects[i].trail[objects[i].trail_ptr][2] = objects[i].z;
                objects[i].trail_ptr = (objects[i].trail_ptr + 1) % MAX_TRAIL;
                if (objects[i].trail_count < MAX_TRAIL) objects[i].trail_count++;
            }
        }
    }
    static int global_trail_tick = 0;
    global_trail_tick++;

    glutPostRedisplay(); glutTimerFunc(16, timer, 0); 
}
void keyboard(unsigned char k, int x, int y) { 
    if(k==27) exit(0); 
    if(k==' ') autoRotate=(autoRotate==0)?0.15:0; 
    if(k=='w' || k=='W') zoom += 0.5f;
    if(k=='s' || k=='S') zoom -= 0.5f;
    if(k=='h' || k=='H') g_show_hud = !g_show_hud;
}
void special(int k, int x, int y) { if(k==GLUT_KEY_UP) angleX-=5; if(k==GLUT_KEY_DOWN) angleX+=5; if(k==GLUT_KEY_LEFT) angleY-=5; if(k==GLUT_KEY_RIGHT) angleY+=5; }

int main(int argc, char** argv) {
    setlocale(LC_ALL, "C"); signal(SIGUSR1, handle_signal);
    char *shm_name = SHM_NAME; if (argc > 1) shm_name = argv[1];
    int retries = 0; while(shm_fd == -1 && retries < 10) { shm_fd = shm_open(shm_name, O_RDWR, 0666); if (shm_fd == -1) { usleep(100000); retries++; } }
    if (shm_fd == -1) exit(1);
    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    glutInit(&argc, argv); glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH); glutInitWindowSize(1024, 768); glutCreateWindow("Trek 3DView - Multiuser");
    glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLfloat lp[] = {10, 10, 10, 1}; glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glLightfv(GL_LIGHT0, GL_POSITION, lp);
    initStars(); initVBOs(); glMatrixMode(GL_PROJECTION); gluPerspective(45, 1.33, 1, 500); glMatrixMode(GL_MODELVIEW);
    glutDisplayFunc(display); glutKeyboardFunc(keyboard); glutSpecialFunc(special); glutTimerFunc(16, timer, 0);
    kill(getppid(), SIGUSR2); glutMainLoop(); return 0;
}