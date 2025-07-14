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
    int64 itemId;
    int64 locationId;
    int32 playerId;
    int32 flags;
    String itemName;    // Resolved name if available
    String playerName;  // Resolved name if available
}

// Player information
struct ArchipelagoPlayer
{
    int32 team;
    int32 slot;
    String alias;
    String name;
}

// Main Archipelago manager class
class ArchipelagoManager : EventHandler
{
    // Connection management
    static bool ConnectToServer(String address, int port, String slotName, String password = "")
    {
        Console.Printf("Connecting to Archipelago server: %s:%d as %s", address, port, slotName);
        return CallArchipelagoFunction("Connect", address, port, slotName, password);
    }
    
    static void Disconnect()
    {
        CallArchipelagoFunction("Disconnect");
    }
    
    static bool IsConnected()
    {
        return GetArchipelagoInt("IsConnected") != 0;
    }
    
    static EArchipelagoConnectionState GetConnectionState()
    {
        return EArchipelagoConnectionState(GetArchipelagoInt("GetConnectionState"));
    }
    
    static String GetConnectionStatusString()
    {
        EArchipelagoConnectionState state = GetConnectionState();
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
    static void CheckLocation(int64 locationId)
    {
        Console.Printf("Checking Archipelago location: %d", locationId);
        CallArchipelagoFunction("CheckLocation", String.Format("%d", locationId));
    }
    
    static void CheckSelacoDLocation(int selacoLocationId)
    {
        // Convert Selaco location ID to Archipelago ID
        int64 apLocationId = 200000 + selacoLocationId; // Using LOCATION_BASE from definitions
        CheckLocation(apLocationId);
    }
    
    static bool IsLocationChecked(int64 locationId)
    {
        return GetArchipelagoInt("IsLocationChecked", String.Format("%d", locationId)) != 0;
    }
    
    static bool IsSelacoDLocationChecked(int selacoLocationId)
    {
        int64 apLocationId = 200000 + selacoLocationId;
        return IsLocationChecked(apLocationId);
    }
    
    // Item management
    static Array<ArchipelagoItem> GetPendingItems()
    {
        Array<ArchipelagoItem> items;
        String itemData = GetArchipelagoString("GetPendingItems");
        
        // Parse item data string (simplified format: "itemId,locationId,playerId,flags;...")
        Array<String> itemStrings;
        itemData.Split(itemStrings, ";");
        
        for (int i = 0; i < itemStrings.Size(); i++)
        {
            if (itemStrings[i].Length() > 0)
            {
                Array<String> parts;
                itemStrings[i].Split(parts, ",");
                
                if (parts.Size() >= 4)
                {
                    ArchipelagoItem item;
                    item.itemId = parts[0].ToInt();
                    item.locationId = parts[1].ToInt();
                    item.playerId = parts[2].ToInt();
                    item.flags = parts[3].ToInt();
                    items.Push(item);
                }
            }
        }
        
        return items;
    }
    
    static void ClearPendingItems()
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
    static void SendChatMessage(String message)
    {
        CallArchipelagoFunction("SendChat", message);
    }
    
    // Game information
    static Array<ArchipelagoPlayer> GetPlayers()
    {
        Array<ArchipelagoPlayer> players;
        String playerData = GetArchipelagoString("GetPlayers");
        
        // Parse player data (simplified format: "team,slot,alias,name;...")
        Array<String> playerStrings;
        playerData.Split(playerStrings, ";");
        
        for (int i = 0; i < playerStrings.Size(); i++)
        {
            if (playerStrings[i].Length() > 0)
            {
                Array<String> parts;
                playerStrings[i].Split(parts, ",");
                
                if (parts.Size() >= 4)
                {
                    ArchipelagoPlayer player;
                    player.team = parts[0].ToInt();
                    player.slot = parts[1].ToInt();
                    player.alias = parts[2];
                    player.name = parts[3];
                    players.Push(player);
                }
            }
        }
        
        return players;
    }
    
    static int GetTeam()
    {
        return GetArchipelagoInt("GetTeam");
    }
    
    static int GetSlot()
    {
        return GetArchipelagoInt("GetSlot");
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
        Array<ArchipelagoItem> items = ArchipelagoManager.GetPendingItems();
        
        for (int i = 0; i < items.Size(); i++)
        {
            ArchipelagoItem item = items[i];
            Console.Printf("Processing received item: %d from player %d", 
                         item.itemId, item.playerId);
            
            // Convert Archipelago item to Selaco item
            GiveSelacoDItem(item);
        }
        
        if (items.Size() > 0)
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
    
    // Give received item to player
    private static void GiveSelacoDItem(ArchipelagoItem item)
    {
        // Convert AP item ID to Selaco item and give to player
        // This would integrate with Selaco's inventory system
        Console.Printf("Giving Selaco item for AP item %d", item.itemId);
    }
}

// HUD element to display Archipelago status
class ArchipelagoStatusHUD : HUDMessageBase
{
    private bool showStatus;
    private String statusText;
    private double lastUpdate;
    
    override void BeginHUD(int width, int height)
    {
        showStatus = CVar.GetCVar("ap_show_hud", players[consoleplayer]).GetBool();
        
        if (showStatus && (level.totaltime - lastUpdate) > 35) // Update every second
        {
            UpdateStatusText();
            lastUpdate = level.totaltime;
        }
    }
    
    override void DrawHUD()
    {
        if (!showStatus || statusText.Length() == 0) return;
        
        // Draw status in top-right corner
        int width, height;
        [width, height] = Screen.GetViewWindow();
        
        HUDFont font = HUDFont.Create("SmallFont");
        int textWidth = font.StringWidth(statusText);
        
        DrawString(font, statusText, (width - textWidth - 10, 10), 
                  DI_SCREEN_TOP_RIGHT, Font.CR_WHITE);
    }
    
    private void UpdateStatusText()
    {
        if (ArchipelagoManager.IsConnected())
        {
            statusText = String.Format("AP: Connected (T%d/S%d)", 
                                     ArchipelagoManager.GetTeam(), 
                                     ArchipelagoManager.GetSlot());
        }
        else
        {
            EArchipelagoConnectionState state = ArchipelagoManager.GetConnectionState();
            statusText = "AP: " .. ArchipelagoManager.GetConnectionStatusString();
        }
    }
}

// Console commands for Archipelago
class ArchipelagoConsoleCommands : EventHandler
{
    override void ConsoleProcess(ConsoleEvent e)
    {
        Array<String> args;
        e.Args.Split(args, " ");
        
        if (args.Size() == 0) return;
        
        String cmd = args[0];
        cmd.ToLower();
        
        if (cmd == "ap_connect")
        {
            if (args.Size() < 4)
            {
                Console.Printf("Usage: ap_connect <server> <port> <slot_name> [password]");
                return;
            }
            
            String server = args[1];
            int port = args[2].ToInt();
            String slotName = args[3];
            String password = args.Size() > 4 ? args[4] : "";
            
            ArchipelagoManager.ConnectToServer(server, port, slotName, password);
        }
        else if (cmd == "ap_disconnect")
        {
            ArchipelagoManager.Disconnect();
        }
        else if (cmd == "ap_status")
        {
            Console.Printf("=== Archipelago Status ===");
            Console.Printf("Connection: %s", ArchipelagoManager.GetConnectionStatusString());
            
            if (ArchipelagoManager.IsConnected())
            {
                Console.Printf("Team: %d, Slot: %d", 
                             ArchipelagoManager.GetTeam(), 
                             ArchipelagoManager.GetSlot());
                Console.Printf("Hint Points: %d", ArchipelagoManager.GetHintPoints());
                Console.Printf("Items Received: %d", ArchipelagoManager.GetReceivedItemCount());
                Console.Printf("Messages Sent: %d, Received: %d",
                             ArchipelagoManager.GetMessagesSent(),
                             ArchipelagoManager.GetMessagesReceived());
                
                Array<ArchipelagoPlayer> players = ArchipelagoManager.GetPlayers();
                Console.Printf("Players in multiworld: %d", players.Size());
            }
        }
        else if (cmd == "ap_check")
        {
            if (args.Size() < 2)
            {
                Console.Printf("Usage: ap_check <location_id>");
                return;
            }
            
            int locationId = args[1].ToInt();
            ArchipelagoManager.CheckSelacoDLocation(locationId);
        }
        else if (cmd == "ap_items")
        {
            Array<ArchipelagoItem> items = ArchipelagoManager.GetPendingItems();
            Console.Printf("Pending items: %d", items.Size());
            
            for (int i = 0; i < items.Size(); i++)
            {
                ArchipelagoItem item = items[i];
                Console.Printf("  Item %d: ID=%d, Location=%d, Player=%d, Flags=%d",
                             i + 1, item.itemId, item.locationId, 
                             item.playerId, item.flags);
            }
        }
        else if (cmd == "ap_process_items")
        {
            ArchipelagoHelpers.ProcessReceivedItems();
        }
        else if (cmd == "ap_chat")
        {
            if (args.Size() < 2)
            {
                Console.Printf("Usage: ap_chat <message>");
                return;
            }
            
            String message = "";
            for (int i = 1; i < args.Size(); i++)
            {
                if (i > 1) message = message .. " ";
                message = message .. args[i];
            }
            
            ArchipelagoManager.SendChatMessage(message);
        }
    }
}