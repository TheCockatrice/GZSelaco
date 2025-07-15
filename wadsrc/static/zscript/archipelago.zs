// Archipelago ZScript API for Selaco
// Provides game-facing interface for multiworld randomizer functionality

// Connection status constants
enum EArchipelagoConnectionState
{
    ACS_Disconnected = 0,
    ACS_Connecting = 1,
    ACS_Connected = 2,
    ACS_Authenticated = 3,
    ACS_Failed = 4
}

// Client status constants  
enum EArchipelagoClientStatus
{
    ACLS_Unknown = 0,
    ACLS_Connected = 5,
    ACLS_Ready = 10,
    ACLS_Playing = 20,
    ACLS_Goal = 30
}

// Item structure for received items
struct ArchipelagoItem
{
    int itemId;
    int locationId;
    int playerId;
    int flags;
    String itemName;    // Resolved name if available
    String playerName;  // Resolved name if available
}

// Player information
struct ArchipelagoPlayer
{
    int team;
    int slot;
    String alias;
    String name;
}

// Main Archipelago manager class
class ArchipelagoManager : EventHandler
{
    // Connection management
    static play bool ConnectToServer(String address, int port, String slotName, String password = "")
    {
        Console.Printf("Connecting to Archipelago server: %s:%d as %s", address, port, slotName);
        return CallArchipelagoFunction("Connect", String.Format("%s,%d,%s,%s", address, port, slotName, password));
    }
    
    static play void Disconnect()
    {
        CallArchipelagoFunction("Disconnect");
    }
    
    static play bool IsConnected()
    {
        return GetArchipelagoInt("IsConnected") != 0;
    }
    
    static play int GetConnectionState()
    {
        return GetArchipelagoInt("GetConnectionState");
    }
    
    static play String GetConnectionStatusString()
    {
        int state = GetConnectionState();
        switch (state)
        {
            case ACS_Disconnected: return "Disconnected";
            case ACS_Connecting: return "Connecting";
            case ACS_Connected: return "Connected";
            case ACS_Authenticated: return "Authenticated";
            case ACS_Failed: return "Failed";
            default: return "Unknown";
        }
    }
    
    // Location checking
    static play void CheckLocation(int locationId)
    {
        Console.Printf("Checking Archipelago location: %d", locationId);
        CallArchipelagoFunction("CheckLocation", String.Format("%d", locationId));
    }
    
    static play void CheckSelacoDLocation(int selacoLocationId)
    {
        // Convert Selaco location ID to Archipelago ID
        int apLocationId = 200000 + selacoLocationId; // Using LOCATION_BASE from definitions
        CheckLocation(apLocationId);
    }
    
    static play bool IsLocationChecked(int locationId)
    {
        return GetArchipelagoInt("IsLocationChecked", String.Format("%d", locationId)) != 0;
    }
    
    static play bool IsSelacoDLocationChecked(int selacoLocationId)
    {
        int apLocationId = 200000 + selacoLocationId;
        return IsLocationChecked(apLocationId);
    }
    
    // Item management (simplified - ZScript has limitations with struct arrays)
    static play int GetPendingItemCount()
    {
        return GetArchipelagoInt("GetPendingItemCount");
    }
    
    static play String GetPendingItemInfo(int index)
    {
        return GetArchipelagoString("GetPendingItemInfo", String.Format("%d", index));
    }
    
    static play void ClearPendingItems()
    {
        CallArchipelagoFunction("ClearPendingItems");
    }
    
    static int GetReceivedItemCount()
    {
        return GetArchipelagoInt("GetReceivedItemCount");
    }
    
    // Status updates
    static void UpdateStatus(EArchipelagoClientStatus status)
    {
        CallArchipelagoFunction("UpdateStatus", String.Format("%d", int(status)));
    }
    
    // Chat
    static play void SendChatMessage(String message)
    {
        CallArchipelagoFunction("SendChat", message);
    }
    
    // Game information (simplified - ZScript has limitations with struct arrays)
    static play int GetPlayerCount()
    {
        return GetArchipelagoInt("GetPlayerCount");
    }
    
    static play String GetPlayerInfo(int index)
    {
        return GetArchipelagoString("GetPlayerInfo", String.Format("%d", index));
    }
    
    static play int GetTeam()
    {
        return GetArchipelagoInt("GetTeam");
    }
    
    static play int GetSlot()
    {
        return GetArchipelagoInt("GetSlot");
    }
    
    static play int GetHintPoints()
    {
        return GetArchipelagoInt("GetHintPoints");
    }
    
    static play int GetReceivedItemCount()
    {
        return GetArchipelagoInt("GetReceivedItemCount");
    }
    
    static play int GetMessagesSent()
    {
        return GetArchipelagoInt("GetMessagesSent");
    }
    
    static play int GetMessagesReceived()
    {
        return GetArchipelagoInt("GetMessagesReceived");
    }
    
    // Stub implementations for native functions
    static play bool CallArchipelagoFunction(String func, String args = "")
    {
        Console.Printf("Archipelago function call: %s(%s)", func, args);
        return true;
    }
    
    static play int GetArchipelagoInt(String func, String args = "")
    {
        Console.Printf("Archipelago int call: %s(%s)", func, args);
        return 0;
    }
    
    static play String GetArchipelagoString(String func, String args = "")
    {
        Console.Printf("Archipelago string call: %s(%s)", func, args);
        return "placeholder";
    }
    
    static int GetHintPoints()
    {
        return GetArchipelagoInt("GetHintPoints");
    }
    
    // Statistics
    static int GetMessagesSent()
    {
        return GetArchipelagoInt("GetMessagesSent");
    }
    
    static int GetMessagesReceived()
    {
        return GetArchipelagoInt("GetMessagesReceived");
    }
    
    // Configuration
    static void SetAutoReconnect(bool enabled)
    {
        CallArchipelagoFunction("SetAutoReconnect", enabled ? "1" : "0");
    }
    
    static void LoadConfig()
    {
        CallArchipelagoFunction("LoadConfig");
    }
    
    static void SaveConfig()
    {
        CallArchipelagoFunction("SaveConfig");
    }
    
    // Event handling
    override void WorldLoaded(WorldEvent e)
    {
        // Initialize Archipelago when world loads
        Console.Printf("Initializing Archipelago integration...");
        CallArchipelagoFunction("Initialize");
        LoadConfig();
    }
    
    override void WorldUnloaded(WorldEvent e)
    {
        // Clean up when world unloads
        SaveConfig();
        CallArchipelagoFunction("Shutdown");
    }
    
    override void NetworkProcess(ConsoleEvent e)
    {
        // Handle network commands for Archipelago
        Array<String> args;
        String cmd = e.Name;
        cmd.Split(args, ",");
        
        if (args.Size() > 0)
        {
            String command = args[0];
            
            if (command ~== "ap_connect" && args.Size() >= 4)
            {
                ConnectToServer(args[1], args[2].ToInt(), args[3], 
                              args.Size() > 4 ? args[4] : "");
            }
            else if (command ~== "ap_disconnect")
            {
                Disconnect();
            }
            else if (command ~== "ap_status")
            {
                Console.Printf("Archipelago Status: %s", GetConnectionStatusString());
                Console.Printf("Team: %d, Slot: %d", GetTeam(), GetSlot());
                Console.Printf("Messages Sent: %d, Received: %d", 
                             GetMessagesSent(), GetMessagesReceived());
            }
            else if (command ~== "ap_check" && args.Size() >= 2)
            {
                CheckSelacoDLocation(args[1].ToInt());
            }
            else if (command ~== "ap_chat" && args.Size() >= 2)
            {
                SendChatMessage(args[1]);
            }
        }
    }
    
    // Helper functions to interface with C++ code
    private static bool CallArchipelagoFunction(String functionName, String arg1 = "", String arg2 = "", String arg3 = "", String arg4 = "")
    {
        // This would need to be implemented as a native function call
        // For now, return a placeholder
        Console.Printf("Called Archipelago function: %s", functionName);
        return true;
    }
    
    private static int GetArchipelagoInt(String functionName, String arg = "")
    {
        // This would need to be implemented as a native function call
        // For now, return a placeholder
        return 0;
    }
    
    private static String GetArchipelagoString(String functionName, String arg = "")
    {
        // This would need to be implemented as a native function call
        // For now, return empty string
        return "";
    }
}

// Helper functions for easier access
class ArchipelagoHelpers
{
    // Common location types for Selaco
    static void CheckBossDefeated(String bossName)
    {
        Console.Printf("Boss defeated: %s", bossName);
        // Map boss names to location IDs
        int locationId = GetBossLocationId(bossName);
        if (locationId > 0)
        {
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
    }
    
    static void CheckItemPickup(String itemName)
    {
        Console.Printf("Item picked up: %s", itemName);
        int locationId = GetItemLocationId(itemName);
        if (locationId > 0)
        {
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
    }
    
    static void CheckLevelComplete(String levelName)
    {
        Console.Printf("Level completed: %s", levelName);
        int locationId = GetLevelLocationId(levelName);
        if (locationId > 0)
        {
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
    }
    
    static void CheckSecretFound(String secretName)
    {
        Console.Printf("Secret found: %s", secretName);
        int locationId = GetSecretLocationId(secretName);
        if (locationId > 0)
        {
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
    }
    
    static void CheckObjectiveComplete(String objectiveName)
    {
        Console.Printf("Objective completed: %s", objectiveName);
        int locationId = GetObjectiveLocationId(objectiveName);
        if (locationId > 0)
        {
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
    }
    
    // Process received Archipelago items
    static void ProcessReceivedItems()
    {
        int itemCount = ArchipelagoManager.GetPendingItemCount();
        
        for (int i = 0; i < itemCount; i++)
        {
            String itemInfo = ArchipelagoManager.GetPendingItemInfo(i);
            Console.Printf("Processing received item: %s", itemInfo);
            
            // For now, just log the item processing
            // In a full implementation, you'd parse itemInfo and give the actual item
            Console.Printf("Item processed (placeholder implementation)");
        }
        
        if (itemCount > 0)
        {
            ArchipelagoManager.ClearPendingItems();
        }
    }
    
    // Map boss names to location IDs (these would need to be defined)
    private static int GetBossLocationId(String bossName)
    {
        // Return base boss location + specific boss ID
        return 1000; // Placeholder
    }
    
    private static int GetItemLocationId(String itemName)
    {
        return 2000; // Placeholder
    }
    
    private static int GetLevelLocationId(String levelName)
    {
        return 3000; // Placeholder
    }
    
    private static int GetSecretLocationId(String secretName)
    {
        return 4000; // Placeholder
    }
    
    private static int GetObjectiveLocationId(String objectiveName)
    {
        return 5000; // Placeholder
    }
    
    // Give received item to player (simplified version)
    private static void GiveSelacoDItem(String itemInfo)
    {
        // Convert AP item info to Selaco item and give to player
        // This would integrate with Selaco's inventory system
        Console.Printf("Giving Selaco item for AP item: %s", itemInfo);
    }
}

// HUD element to display Archipelago status (simplified)
class ArchipelagoStatusHUD : EventHandler
{
    private bool showStatus;
    private String statusText;
    private double lastUpdate;
    
    override void RenderOverlay(RenderEvent e)
    {
        // Simple HUD update - just show basic status
        showStatus = true; // Simplified for now
        
        if (showStatus && (level.totaltime - lastUpdate) > 35) // Update every second
        {
            statusText = "Archipelago: Ready";
            lastUpdate = level.totaltime;
        }
        
        if (showStatus && statusText.Length() > 0)
        {
            // Simple text display - can be enhanced later
            Console.Printf("Archipelago Status: %s", statusText);
        }
    }
}

// Console commands for Archipelago
class ArchipelagoConsoleCommands : EventHandler
{
    override void ConsoleProcess(ConsoleEvent e)
    {
        if (e.Args.Size() == 0) return;
        
        String cmd = e.Args[0];
        cmd.MakeLower();
        
        if (cmd == "ap_connect")
        {
            if (e.Args.Size() < 4)
            {
                Console.Printf("Usage: ap_connect <server> <port> <slot_name> [password]");
                return;
            }
            
            String server = e.Args[1];
            int port = e.Args[2].ToInt();
            String slotName = e.Args[3];
            String password = e.Args.Size() > 4 ? e.Args[4] : "";
            
            // Note: ConnectToServer needs to be called from play context
            Console.Printf("Archipelago connect requested to %s:%d as %s", server, port, slotName);
        }
        else if (cmd == "ap_disconnect")
        {
            Console.Printf("Archipelago disconnect requested");
        }
        else if (cmd == "ap_status")
        {
            Console.Printf("=== Archipelago Status ===");
            Console.Printf("Archipelago integration loaded");
            Console.Printf("Use ap_connect to connect to server");
        }
        else if (cmd == "ap_check")
        {
            if (e.Args.Size() < 2)
            {
                Console.Printf("Usage: ap_check <location_id>");
                return;
            }
            
            int locationId = e.Args[1].ToInt();
            Console.Printf("Archipelago location check requested: %d", locationId);
        }
        else if (cmd == "ap_items")
        {
            Console.Printf("Archipelago items list requested");
        }
        else if (cmd == "ap_process_items")
        {
            Console.Printf("Archipelago process items requested");
        }
        else if (cmd == "ap_chat")
        {
            if (e.Args.Size() < 2)
            {
                Console.Printf("Usage: ap_chat <message>");
                return;
            }
            
            String message = "";
            for (int i = 1; i < e.Args.Size(); i++)
            {
                if (i > 1) message = message .. " ";
                message = message .. e.Args[i];
            }
            
            Console.Printf("Archipelago chat message: %s", message);
        }
    }
}