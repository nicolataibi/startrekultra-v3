# Star Trek Ultra: Multi-User Client-Server Edition
**Persistent Galaxy Tactical Navigation & Combat Simulator**

Star Trek Ultra is a complex space simulation that merges the strategic depth of classic text-based "Trek" games with a modern Client-Server architecture and hardware-accelerated 3D real-time visualization.

---

## 🧭 System Overview
Unlike most space simulators, Star Trek Ultra strictly separates the **Galactic Logic** (managed by the Server) from the **Command Deck** (CLI Client) and the **Tactical View** (3D Client). This allows for a seamless multiplayer experience where dozens of captains can interact within the same persistent universe of 1000 quadrants.

---

## 🚀 Quick Start & Installation

### System Requirements
*   **OS**: Linux (Ubuntu, Debian, Fedora, etc.)
*   **Graphics Dependencies**: `freeglut3-dev`, `libgl1-mesa-dev`, `libglu1-mesa-dev`.
*   **Compiler**: GCC with C23 support.

### Installation
1.  Clone the repository to your development folder.
2.  Compile the entire ecosystem:
    ```bash
    make
    ```
    *This will generate three executables: `trek_server`, `trek_client`, and `trek_3dview`.*

### Starting a Session
1.  **Start the Server**:
    ```bash
    ./trek_server
    ```
    *The server will load `galaxy.dat` if present; otherwise, it will generate a new galaxy.*
2.  **Start the Command Deck**:
    ```bash
    ./trek_client
    ```
    *Follow the on-screen instructions to enter the server IP (127.0.0.1 for local), your name, and choose your faction.*

---

## 🕹️ Operational Command Manual

### 1. Navigation and Movement
The system uses a 3D coordinate model based on **Quadrants** (macro-space) and **Sectors** (micro-space).

*   `nav <H> <M> <W>`: Engages the **Warp Drive** for travel between quadrants.
    *   `H` (Heading): Compass direction (0-359°).
    *   `M` (Mark): Vertical inclination (-90° to +90°).
    *   `W` (Warp): Speed from 1 to 8. Determines the distance traveled.
*   `imp <H> <M> <S>`: Activates **Impulse Engines** for manual sub-light maneuvering.
    *   `S` (Speed): From 0.0 (Stop) to 1.0 (Full Impulse). Ideal for docking and close-quarters combat.
*   `apr <ID> <DIST>`: Autopilot approach. The ship calculates the optimal path to reach distance `DIST` from target `ID`.
*   `cal <QX> <QY> <QZ>`: Nav-Computer. Calculates the H, M, and W parameters needed to reach a specific quadrant.

### 2. Tactics and Weapon Systems
*   `srs`: **Short Range Sensors**. Provides a tactical text dump of all objects in the current quadrant, including their unique IDs (required for most commands).
*   `lrs`: **Long Range Sensors**. Displays a 3x3x3 strategic map of the surrounding quadrants.
*   `lock <ID>`: Locks targeting systems onto object `ID`. Use `lock 0` to release.
*   `pha <Energy>`: Fires Phasers. If a target is `locked`, all energy is concentrated on it. Without a lock, fire is directed straight ahead.
*   `tor [H] [M]`: Launches a Photon Torpedo. If a target is `locked`, the torpedo is auto-guided. Manual fire requires H and M parameters.
*   `she <F> <R> <T> <B> <L> <RI>`: Configures the 6 shield quadrants (Front, Rear, Top, Bottom, Left, Right).
*   `clo`: Toggles the **Cloaking Device**. Consumes constant energy but renders you invisible to AI and other players' HUDs.

### 3. Logic and Resources
*   `min`: Mines minerals from a planet if within distance < 2.0.
*   `sco`: **Solar Scooping**. Recharges ship energy by flying near a star (watch out for heat damage!).
*   `har`: **Antimatter Harvest**. Collects pure energy from Black Holes (extremely high structural risk).
*   `con <Type> <Qty>`: Converts raw materials into usable resources.
    *   Type 1: Dilithium -> Energy.
    *   Type 3: Verterium -> Torpedoes.
*   `bor`: **Boarding Party**. Attempts to board and capture the `locked` ship (Distance < 1.0).

---

## 📊 HUD and Graphical Interface
The 3D window (`trek_3dview`) provides a dynamic tactical overlay:
*   **Vessel Labels**: Display Species and unique ID (e.g., `Klingon [102]`).
*   **Health Bars**: Color-coded indicators of target status (Green/Yellow/Red).
*   **Keyboard Shortcuts**:
    *   `H`: Toggle HUD labels.
    *   `Arrow Keys`: Rotate view around your ship.
    *   `W / S`: Zoom In / Out.
    *   `Spacebar`: Toggle cinematic auto-rotation.
    *   `ESC`: Close the client.

---

## ⚔️ Gameplay Scenarios

### Scenario A: Combat Against an NPC Hunter
1.  Run `srs`. You spot a `Klingon [105]` at distance 6.0.
2.  The NPC detects you and enters `CHASE` state, veering toward your position.
3.  Execute `lock 105`.
4.  Execute `pha 500`. You hit the enemy shields.
5.  The NPC returns fire. You see `WARNING: Incoming phaser fire!`.
6.  Execute `tor`. You launch a guided torpedo that destroys the NPC.
7.  Watch the `Dismantle` effect (the enemy ship explodes into colored debris).

### Scenario B: Emergency Escape from a Black Hole
1.  You are too close to a Black Hole. Warp is blocked: `EMERGENCY: Gravitational shear detected`.
2.  Execute `imp 180 0 1.0` (Set a course directly away from danger at full power).
3.  Once clear of the range (Dist > 2.25), the computer unlocks the engines.
4.  Execute `nav <parameters>` to jump to another quadrant.

### Scenario C: Diplomacy and Radio
1.  Run `who` to see who is online. You find `Captain Kirk [ID: 1]`.
2.  Send a private message: `rad #1 Hello Captain, shall we proceed together to Q[5,5,5]?`.
3.  Kirk replies, and you see: `[RADIO] Kirk (Starfleet): Copy that, setting course.`.

---

## 🛠️ Technical Architecture for Developers

### The "Subspace" Network System
The server and client communicate via a binary protocol over TCP. Every tick (30ms), the server sends a `PacketUpdate` containing the complete state of the player's local quadrant. This packet is optimized to minimize bandwidth usage.

### Shared Memory (IPC)
The client (`trek_client`) and the visualizer (`trek_3dview`) communicate via **POSIX Shared Memory** (`/st_shm_PID`). 
*   The client writes data received from the server into memory.
*   The visualizer reads the memory at 60 FPS for fluid rendering.
*   This decouples network logic (slow/variable) from rendering logic (fast).

### Hunter AI (State Machine)
Every NPC ship runs internal logic:
*   **PATROL**: Slow, erratic movement.
*   **CHASE**: Vector-based pursuit toward the nearest non-cloaked player.
*   **FLEE**: Directional retreat when energy falls below 20%.

---

## 💾 Data Persistence
The `galaxy.dat` file is a binary dump of the `StarTrekGame`, `NPCShip`, and `NPCPlanet` structures. It is updated every 60 seconds and loaded upon server startup. This ensures that every action (ship destruction, planet depletion) has permanent consequences over time.

---
**Note**: Star Trek Ultra is under continuous development. Please check `multiutenza.txt` and `suggerimenti.txt` for future roadmaps.
