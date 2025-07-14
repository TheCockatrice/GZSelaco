# Selaco Archipelago Integration

## Overview
This document outlines the implementation plan for integrating Archipelago multiworld randomizer support into Selaco, a GZDoom-based FPS game.

## Architecture Overview

### Core Components

1. **C++ Archipelago Client** (`src/archipelago/`)
   - WebSocket client for Archipelago server communication
   - JSON message handling using existing RapidJSON
   - Item/location management
   - Connection management

2. **ZScript API** (`wadsrc/static/zscript/archipelago/`)
   - Game-facing API for location checks and item handling
   - Event handlers for game progression
   - UI components for connection status

3. **Network Integration**
   - Leverage existing `NetworkCommand`/`NetworkBuffer` system
   - Event-driven architecture using `EventManager`

## Implementation Details

### 1. Archipelago Protocol Implementation

**File Structure:**
```
src/archipelago/
├── archipelago_client.h
├── archipelago_client.cpp
├── ap_definitions.h
├── websocket_client.h
├── websocket_client.cpp
└── ap_protocol.h
```

**Key Features:**
- WebSocket connection to Archipelago server
- JSON packet parsing/generation
- Item/location ID mapping
- Connection state management
- Automatic reconnection handling

### 2. Game Integration Points

**Location Checks:**
- Enemy defeats (bosses, specific enemies)
- Item pickups (weapons, upgrades, keys)
- Level completion milestones
- Secret area discoveries
- Objective completions

**Item Types:**
- Weapons and weapon upgrades
- Health/armor upgrades
- Key items and access cards
- Equipment modifications
- Cosmetic items

### 3. ZScript API Design

```zscript
class ArchipelagoManager : EventHandler
{
    // Connection management
    static bool ConnectToServer(String address, int port, String slot_name, String password);
    static void Disconnect();
    static bool IsConnected();
    
    // Location checking
    static void CheckLocation(int location_id);
    static bool IsLocationChecked(int location_id);
    
    // Item handling  
    static void HandleReceivedItem(int item_id, int player_id);
    static Array<int> GetPendingItems();
    
    // Status and info
    static String GetConnectionStatus();
    static int GetItemCount();
    static int GetCheckedLocationCount();
}
```

### 4. UI Components

**Connection Menu:**
- Server address/port input
- Slot name and password fields
- Connection status display
- Auto-connect option

**In-game HUD:**
- Connection status indicator
- Item queue display
- Location check notifications
- Chat message display

### 5. Configuration System

**archipelago.cfg:**
```
# Archipelago Configuration
ap_server_address = "archipelago.gg"
ap_server_port = 38281
ap_slot_name = ""
ap_password = ""
ap_auto_connect = false
ap_show_hud = true
ap_location_notifications = true
```

## Technical Implementation

### WebSocket Client
Using existing platform abstractions, implement a WebSocket client that can:
- Establish secure connections (WSS support)
- Handle JSON message serialization/deserialization  
- Manage connection lifecycle
- Queue messages during disconnection

### Event Integration
Leverage Selaco's existing event system:
```cpp
// Register for location checks
EventManager::RegisterHandler("LocationCheck", ArchipelagoHandler);

// Send network events for items
SendNetworkEvent("ArchipelagoItem", item_id, player_id, flags);
```

### Data Persistence
Store Archipelago state in save games:
- Connection parameters
- Checked locations
- Received items
- Player progress

## Development Phases

### Phase 1: Core Infrastructure
- [ ] Basic WebSocket client implementation
- [ ] Archipelago protocol packet handling
- [ ] JSON message parsing/generation
- [ ] Basic connection management

### Phase 2: Game Integration
- [ ] ZScript API implementation
- [ ] Event system integration
- [ ] Location definition system
- [ ] Item handling framework

### Phase 3: UI and UX
- [ ] Connection menu implementation
- [ ] In-game HUD elements
- [ ] Configuration system
- [ ] Status notifications

### Phase 4: Game-Specific Features
- [ ] Selaco location mapping
- [ ] Selaco item definitions
- [ ] Game progression hooks
- [ ] Balance and testing

### Phase 5: Polish and Documentation
- [ ] Error handling and recovery
- [ ] Performance optimization
- [ ] User documentation
- [ ] Developer documentation

## Testing Strategy

1. **Unit Tests:** Core protocol handling and data structures
2. **Integration Tests:** ZScript API and event system integration  
3. **Network Tests:** Connection handling and message flow
4. **Game Tests:** In-game functionality and user experience
5. **Multiworld Tests:** Cross-game compatibility testing

## Compatibility

- **Archipelago Version:** Target latest stable (0.5.x)
- **Protocol Support:** Full network protocol compliance
- **Platform Support:** Windows, Linux (matching Selaco platforms)
- **Save Compatibility:** Maintain compatibility with vanilla saves

## Dependencies

- **RapidJSON:** Already present in codebase
- **WebSocket Library:** Platform-appropriate implementation
- **Threading:** For non-blocking network operations
- **Crypto:** For secure connections (if needed)

## Deployment

- Distributed as part of Selaco builds
- Optional feature that can be disabled
- Configuration through in-game menus
- Documentation for setup and usage