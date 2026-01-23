#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "network.h"
#include "shared_state.h"
#include "ui.h"

/* Colori per l'interfaccia CLI */
#define RESET   "\033[0m"
#define B_RED     "\033[1;31m"
#define B_GREEN   "\033[1;32m"
#define B_YELLOW  "\033[1;33m"
#define B_BLUE    "\033[1;34m"
#define B_MAGENTA "\033[1;35m"
#define B_CYAN    "\033[1;36m"
#define B_WHITE   "\033[1;37m"

#include <termios.h>

int sock = 0;
char captain_name[64];
int my_faction = 0;
pid_t visualizer_pid = 0;
GameState *g_shared_state = NULL;
int shm_fd = -1;
char shm_path[64];
volatile sig_atomic_t g_visualizer_ready = 0;

/* Gestione Input Reattivo */
char g_input_buf[256] = {0};
int g_input_ptr = 0;
struct termios orig_termios;

volatile sig_atomic_t g_running = 1;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /* Lasciamo ISIG attivo per permettere Ctrl+C */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void reprint_prompt() {
    printf("\r\033[KCommand? %s", g_input_buf);
    fflush(stdout);
}

void handle_ack(int sig) {
    g_visualizer_ready = 1;
}

void handle_sigchld(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

void init_shm() {


    sprintf(shm_path, "/st_shm_%d", getpid());
    shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(GameState));
    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shared_state->mutex, &attr);
}

void cleanup() {
    if (visualizer_pid > 0) kill(visualizer_pid, SIGTERM);
    if (g_shared_state) munmap(g_shared_state, sizeof(GameState));
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(shm_path);
    }
}

/* Funzione di utilità per leggere esattamente N byte dal socket */
int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void *network_listener(void *arg) {
    while (g_running) {
        int type;
        if (read_all(sock, &type, sizeof(int)) <= 0) {
            g_running = 0;
            disable_raw_mode();
            printf("\nConnection lost to server.\n");
            exit(0);
        }
        
        if (type == PKT_MESSAGE) {
            PacketMessage msg;
            msg.type = type;
            if (read_all(sock, ((char*)&msg) + sizeof(int), sizeof(PacketMessage) - sizeof(int)) <= 0) {
                g_running = 0;
                break;
            }
            
            printf("\r\033[K"); /* Pulisce la riga di input attuale */
            if (strcmp(msg.from, "SERVER") == 0 || strcmp(msg.from, "COMPUTER") == 0 || 
                strcmp(msg.from, "SCIENCE") == 0 || strcmp(msg.from, "TACTICAL") == 0 ||
                strcmp(msg.from, "ENGINEERING") == 0 || strcmp(msg.from, "HELMSMAN") == 0 ||
                strcmp(msg.from, "WARNING") == 0 || strcmp(msg.from, "DAMAGE CONTROL") == 0) {
                printf("%s\n", msg.text);
            } else {
                printf(B_CYAN "[RADIO] %s (%s): %s\n" RESET, msg.from, 
                       (msg.faction == FACTION_FEDERATION) ? "Starfleet" : "Alien", msg.text);
            }
            reprint_prompt();
        } else if (type == PKT_UPDATE) {
            PacketUpdate upd;
            upd.type = type;
            if (read_all(sock, ((char*)&upd) + sizeof(int), sizeof(PacketUpdate) - sizeof(int)) <= 0) break;
            
            if (g_shared_state) {
                pthread_mutex_lock(&g_shared_state->mutex);
                /* Sincronizziamo lo stato locale con i dati ottimizzati dal server */
                g_shared_state->shm_energy = upd.energy;
                int total_s = 0;
                for(int s=0; s<6; s++) {
                    g_shared_state->shm_shields[s] = upd.shields[s];
                    total_s += upd.shields[s];
                }
                g_shared_state->is_cloaked = upd.is_cloaked;
                sprintf(g_shared_state->quadrant, "Q-%d-%d-%d", upd.q1, upd.q2, upd.q3);

                g_shared_state->object_count = upd.object_count;
                for (int o=0; o < upd.object_count; o++) {
                    g_shared_state->objects[o].shm_x = upd.objects[o].net_x;
                    g_shared_state->objects[o].shm_y = upd.objects[o].net_y;
                    g_shared_state->objects[o].shm_z = upd.objects[o].net_z;
                    g_shared_state->objects[o].h = upd.objects[o].h;
                    g_shared_state->objects[o].m = upd.objects[o].m;
                    g_shared_state->objects[o].type = upd.objects[o].type;
                    g_shared_state->objects[o].ship_class = upd.objects[o].ship_class;
                    g_shared_state->objects[o].health_pct = upd.objects[o].health_pct;
                    g_shared_state->objects[o].id = upd.objects[o].id;
                    g_shared_state->objects[o].active = 1;
                }
                
                /* Append beams to shared state (Queue logic) */
                if (upd.beam_count > 0) {
                    for (int b=0; b < upd.beam_count; b++) {
                        if (g_shared_state->beam_count < MAX_BEAMS) {
                            int idx = g_shared_state->beam_count;
                            g_shared_state->beams[idx].shm_tx = upd.beams[b].net_tx;
                            g_shared_state->beams[idx].shm_ty = upd.beams[b].net_ty;
                            g_shared_state->beams[idx].shm_tz = upd.beams[b].net_tz;
                            g_shared_state->beams[idx].active = upd.beams[b].active;
                            g_shared_state->beam_count++;
                        }
                    }
                }
                
                /* Projectile position */
                g_shared_state->torp.shm_x = upd.torp.net_x;
                g_shared_state->torp.shm_y = upd.torp.net_y;
                g_shared_state->torp.shm_z = upd.torp.net_z;
                g_shared_state->torp.active = upd.torp.active;
                
                /* Event Latching (Visualizer will clear these) */
                if (upd.boom.active) {
                    g_shared_state->boom.shm_x = upd.boom.net_x;
                    g_shared_state->boom.shm_y = upd.boom.net_y;
                    g_shared_state->boom.shm_z = upd.boom.net_z;
                    g_shared_state->boom.active = 1;
                }
                
                if (upd.dismantle.active) {
                    g_shared_state->dismantle.shm_x = upd.dismantle.net_x;
                    g_shared_state->dismantle.shm_y = upd.dismantle.net_y;
                    g_shared_state->dismantle.shm_z = upd.dismantle.net_z;
                    g_shared_state->dismantle.species = upd.dismantle.species;
                    g_shared_state->dismantle.active = 1;
                }
                
                g_shared_state->frame_id++; 
                pthread_mutex_unlock(&g_shared_state->mutex);
                kill(visualizer_pid, SIGUSR1); 
            }
        }
    }
    return NULL;
}

void handle_sigint(int sig) {
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char server_ip[64];

    struct sigaction sa;
    sa.sa_handler = handle_ack;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);

    struct sigaction sa_exit;
    sa_exit.sa_handler = handle_sigint;
    sigemptyset(&sa_exit.sa_mask);
    sa_exit.sa_flags = 0;
    sigaction(SIGINT, &sa_exit, NULL);
    sigaction(SIGTERM, &sa_exit, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    atexit(cleanup);

    printf(B_YELLOW "--- TREK ULTRA CLIENT ---\n" RESET);
    printf("Server IP: "); scanf("%s", server_ip);
    printf("Commander Name: "); scanf("%s", captain_name);
    printf("Faction (0:Fed, 1:Kli, 2:Rom, 3:Bor): "); scanf("%d", &my_faction);
    
    int my_ship_class = SHIP_CLASS_GENERIC_ALIEN;
    if (my_faction == FACTION_FEDERATION) {
        printf("\n" B_WHITE "--- SELECT YOUR CLASS ---" RESET "\n");
        printf(" 0: " B_CYAN "Constitution (Enterprise)" RESET "  5: Galaxy (Huge/Powerful)\n");
        printf(" 1: Miranda (Maneuverable)    6: Sovereign (Tactical/Fast)\n");
        printf(" 2: Excelsior (Heavy/Strong)  7: Intrepid (Scientific/Fast)\n");
        printf(" 3: Constellation (4-Nacelles) 8: Akira (Combat Heavy)\n");
        printf(" 4: Defiant (Escort/Stealth)  9: Nebula (Versatile)\n");
        printf("10: Ambassador (Heavy)       11: Oberth (Small Science)\n");
        printf("12: Steamrunner (Tactical)\n");
        printf("Selection: ");
        scanf("%d", &my_ship_class);
    }
    getchar(); /* pulisci buffer */

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DEFAULT_PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    /* Login */
    PacketLogin lpkt = {PKT_LOGIN, "", my_faction, my_ship_class};
    strcpy(lpkt.name, captain_name);
    send(sock, &lpkt, sizeof(lpkt), 0);

    /* Ricezione Galassia Master (Sincronizzazione iniziale) */
    StarTrekGame master_sync;
    if (read(sock, &master_sync, sizeof(StarTrekGame)) > 0) {
        printf(B_GREEN "Galaxy Map synchronized.\n" RESET);
    }

    init_shm();
    visualizer_pid = fork();
    if (visualizer_pid == 0) {
        execl("./trek_3dview", "trek_3dview", shm_path, NULL);
        exit(0);
    }

    /* Wait for visualizer handshake */
    while(!g_visualizer_ready) usleep(10000);
    printf(B_GREEN "Tactical View (3D) initialized.\n" RESET);

    /* Thread per ascoltare il server */
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, network_listener, NULL);

    printf(B_GREEN "Connected to Galaxy Server. Command Deck ready.\n" RESET);
    enable_raw_mode();
    reprint_prompt();

    while (g_running) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') {
                if (g_input_ptr > 0) {
                    printf("\n");
                    g_input_buf[g_input_ptr] = 0;
                    
                    if (strcmp(g_input_buf, "xxx") == 0) {
                        PacketCommand cpkt = {PKT_COMMAND, "xxx"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                        g_running = 0;
                        disable_raw_mode();
                        exit(0);
                    }
                    if (strcmp(g_input_buf, "help") == 0) {
                        printf(B_WHITE "\n--- STAR TREK ULTRA: MULTIPLAYER COMMANDS ---" RESET "\n");
                        printf("nav H M W   : Warp Navigation (Heading 0-359, Mark -90/90, Warp 0-8)\n");
                        printf("imp H M S   : Impulse Drive (H, M, Speed 0.0-1.0). imp 0 0 0 to stop.\n");
                        printf("srs         : Short Range Sensors (Current Quadrant View)\n");
                        printf("lrs         : Long Range Sensors (3x3x3 Neighborhood Scan)\n");
                        printf("pha E       : Fire Phasers (Distance-based damage, uses Energy)\n");
                        printf("tor H M     : Launch Photon Torpedo (Ballistic projectile)\n");
                        printf("she F R T B L RI : Configure 6 Shield Quadrants\n");
                        printf("lock ID     : Lock-on Target (0:Self, 1+:Nearby vessels)\n");
                        printf("pow E S W   : Power Distribution (Engines, Shields, Weapons %%)\n");
                        printf("psy         : Psychological Warfare (Corbomite Bluff)\n");
                        printf("aux probe QX QY QZ: Launch long-range probe\n");
                        printf("aux jettison: Eject Warp Core (Suicide maneuver)\n");
                        printf("bor         : Boarding party operation (Dist < 1.0)\n");
                        printf("min         : Planetary Mining (Must be in orbit dist < 2.0)\n");
                        printf("doc         : Dock with Starbase (Replenish/Repair, same faction)\n");
                        printf("con T A     : Convert Resources (1:Dilithium->E, 3:Verterium->Torps)\n");
                        printf("rep ID      : Repair System (Uses Tritanium or Isolinear Crystals)\n");
                        printf("inv         : Cargo Inventory Report\n");
                        printf("who         : List active captains in galaxy\n");
                        printf("cal QX QY QZ: Navigation Calculator\n");
                        printf("apr ID DIST : Approach target autopilot\n");
                        printf("sco         : Solar scooping for energy\n");
                        printf("har         : Antimatter harvest from Black Hole\n");
                        printf("sta         : Mission Status Report\n");
                        printf("dam         : Detailed Damage Report\n");
                        printf("rad MSG     : Send Global Radio Message\n");
                        printf("rad @Fac MSG: Send to Faction (e.g. @Romulan ...)\n");
                        printf("rad #ID MSG : Send Private Message to Player ID\n");
                        printf("clo         : Toggle Cloaking Device (Consumes constant Energy)\n");
                        printf("axs / grd   : Toggle 3D Visual Guides\n");
                        printf("xxx         : Self-Destruct\n");
                    } else if (strcmp(g_input_buf, "axs") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_axes = !g_shared_state->shm_show_axes;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Axes toggled.\n");
                        }
                    } else if (strcmp(g_input_buf, "grd") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_grid = !g_shared_state->shm_show_grid;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Grid toggled.\n");
                        }
                    } else if (strncmp(g_input_buf, "rad ", 4) == 0) {
                        PacketMessage mpkt = {PKT_MESSAGE, "", my_faction, SCOPE_GLOBAL, 0, ""};
                        strcpy(mpkt.from, captain_name);
                        
                        char *msg_start = g_input_buf + 4;
                        if (msg_start[0] == '@') {
                            char target_name[64];
                            int offset = 0;
                            sscanf(msg_start + 1, "%s%n", target_name, &offset);
                            if (offset > 0) {
                                mpkt.scope = SCOPE_FACTION;
                                if (strcasecmp(target_name, "Federation")==0 || strcasecmp(target_name, "Fed")==0) mpkt.faction = FACTION_FEDERATION;
                                else if (strcasecmp(target_name, "Klingon")==0 || strcasecmp(target_name, "Kli")==0) mpkt.faction = FACTION_KLINGON;
                                else if (strcasecmp(target_name, "Romulan")==0 || strcasecmp(target_name, "Rom")==0) mpkt.faction = FACTION_ROMULAN;
                                else if (strcasecmp(target_name, "Borg")==0 || strcasecmp(target_name, "Bor")==0) mpkt.faction = FACTION_BORG;
                                else if (strcasecmp(target_name, "Cardassian")==0 || strcasecmp(target_name, "Car")==0) mpkt.faction = FACTION_CARDASSIAN;
                                else {
                                    mpkt.scope = SCOPE_GLOBAL; /* Fallback */
                                }
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 2047);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 2047);
                        } else if (msg_start[0] == '#') {
                            int tid;
                            int offset = 0;
                            if (sscanf(msg_start + 1, "%d%n", &tid, &offset) == 1) {
                                mpkt.scope = SCOPE_PRIVATE;
                                mpkt.target_id = tid;
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 2047);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 2047);
                        } else {
                            strncpy(mpkt.text, msg_start, 2047);
                        }
                        
                        send(sock, &mpkt, sizeof(mpkt), 0);
                    } else {
                        PacketCommand cpkt = {PKT_COMMAND, ""};
                        strncpy(cpkt.cmd, g_input_buf, 255);
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    }
                    
                    g_input_ptr = 0;
                    g_input_buf[0] = 0;
                } else {
                    printf("\n");
                }
                reprint_prompt();
            } else if (c == 127 || c == 8) { /* Backspace */
                if (g_input_ptr > 0) {
                    g_input_ptr--;
                    g_input_buf[g_input_ptr] = 0;
                    reprint_prompt();
                }
            } else if (c >= 32 && c <= 126 && g_input_ptr < 255) {
                g_input_buf[g_input_ptr++] = c;
                g_input_buf[g_input_ptr] = 0;
                reprint_prompt();
            } else if (c == 27) { /* ESC o sequenze speciali */
                /* Potremmo gestire le frecce qui, ma per ora lo ignoriamo */
            }
        }
    }

    close(sock);
    return 0;
}
