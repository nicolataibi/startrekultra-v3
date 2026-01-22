# Star Trek Ultra: Multi-User Client-Server Edition

## Overview
**Star Trek Ultra: Multi-User** is an evolution of the traditional volumetric tactical simulator into a persistent, networked environment. It allows multiple captains to operate within the same 3D galaxy (10x10x10 quadrants), featuring a centralized authoritative server and specialized clients for command and visualization.

---

## Part I: System Architecture

### 1. The "Subspace" Network Model
The project utilizes a **Specialized Client-Server Architecture** designed for low-latency tactical simulation:
*   **Galaxy Server (`trek_server`)**: The single source of truth. It manages the authoritative state of all 1000 quadrants, NPC AI, combat resolution, and object persistence.
*   **Command Deck (`trek_client`)**: The player's interface. It captures reactive CLI input and communicates with the server via TCP Sockets.
*   **Tactical View (`trek_3dview`)**: A dedicated 3D rendering process launched by the client. It visualizes the local quadrant in real-time using OpenGL.

### 2. IPC & Networking Protocols
*   **Network Layer**: Custom binary protocol over **TCP/IP**.
    *   `PKT_LOGIN`: Handshake for faction and ship class selection.
    *   `PKT_COMMAND`: Transmission of CLI strings to the server.
    *   `PKT_UPDATE`: High-frequency state synchronization (20 FPS) containing the `StarTrekGame` local view.
    *   `PKT_MESSAGE`: Asynchronous subspace radio and system alerts.
*   **Local IPC**: The client and visualizer communicate via **POSIX Shared Memory (`shm_open`, `mmap`)**. This ensures the 3D view renders at 60+ FPS independently of network traffic.
*   **Packet Integrity**: Implementation of `read_all()` logic to handle TCP stream fragmentation, ensuring large packets (like the 3D LRS report) are never truncated.

---

## Part II: Advanced Technical Features

### 1. Reactive Command Interface
To solve the problem of asynchronous network messages (e.g., "Incoming Fire!") interrupting player input, the client implements a **Reactive Terminal System**:
*   **Non-Canonical Raw Mode**: Uses `termios.h` to capture keystrokes individually.
*   **Auto-Repaint Logic**: When a message arrives, the client clears the current line, prints the alert, and instantly restores the user's half-written command.

### 2. Tactical Simulation Engine
*   **6-Axis Directional Shields**: Defense is calculated across **Front, Rear, Top, Bottom, Left, and Right** sectors. Hit detection utilizes 3D vector analysis to determine the primary impact face.
*   **Universal Targeting System (UID)**: Every entity in the galaxy has a unique ID:
    *   `1-32`: Human Players
    *   `100+`: NPC Vessels (Klingon, Romulan, Borg, etc.)
    *   `500+`: Starbases
    *   `1000+`: Planets
    *   `2000+`: Stars
    *   `3000+`: Black Holes
*   **Dynamic LRS (Long Range Sensors)**: A real-time 3D scanning routine that computes Heading (H), Mark (M), and Warp (W) vectors for 26 adjacent quadrants across 3 vertical levels (Decks).

### 3. Rendering Pipeline & Optimizations
*   **OpenGL VBOs**: Vertex Buffer Objects are used for static starfields and the sector grid to minimize CPU-GPU overhead.
*   **Squared Euclidean Distance**: Performance optimization for collision and trail detection, avoiding expensive `sqrt()` calls in the render loop.
*   **Particle Dismantle System**: A procedural effect that shatters ships into hundreds of physics-based particles upon destruction or boarding.

---

## Part III: Gameplay & Commands

### 1. Communications (NEW)
The new **Subspace Radio System** allows for complex diplomatic and tactical coordination:
*   **`rad <message>`**: Broadcasts a message on the **Global Frequency** (visible to all).
*   **`rad @<Faction> <message>`**: Sends an encrypted message to a specific Faction (e.g., `rad @Romulan We propose a treaty`). Supported factions: `Fed`, `Kli`, `Rom`, `Bor`, `Car`.
*   **`rad #<ID> <message>`**: Establishes a **Private Link** to a specific vessel ID (e.g., `rad #2 Cover me!`). Find IDs using the `srs` command.

### 2. Tactical Commands
*   **`nav H M W`**: 3D Warp navigation with vector alignment.
*   **`lock ID`**: Universal target acquisition.
*   **`pha E`**: Phaser fire with damage scaling based on `pow` allocation and distance.
*   **`tor`**: Ballistic torpedo launch (auto-guided if `lock` is active).
*   **`she F R T B L RI`**: Full 6-axis shield energy management.
*   **`bor`**: Transporter-based boarding parties to capture vessels or starbases.
*   **`apr ID DIST`**: Advanced autopilot for intercept or orbital insertion.

### 3. Science & Logistics
*   **`srs`**: Professional tactical analysis of the quadrant, listing coordinates and vectors for all UIDs.
*   **`lrs`**: 3D strategic scan of surrounding space.
*   **`min` / `sco` / `har`**: Resource acquisition from planets, stars (Energy), and black holes (Dilithium).
*   **`con`**: Conversion of raw materials into ship resources (Monotanium, Isolinear Crystals, Gas).
*   **`aux probe`**: Remote sensor deployment.

### 4. Faction System
Captains can choose between **Federation, Klingon, Romulan, and Borg**, each influencing ship models and starting parameters. NPC factions will retaliate if provoked or if players enter their territory.

---

## Part IV: Build & Requirements
*   **Language**: C23 Standard.
*   **Dependencies**: `freeglut3-dev`, `libgl1-mesa-dev`, `librt`, `lpthread`.
*   **Compilation**: Use the provided `Makefile` (`make all`).
*   **Execution**:
    1.  `./trek_server` (Start the galaxy engine).
    2.  `./trek_client` (Connect as a captain).