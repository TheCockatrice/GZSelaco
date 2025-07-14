#pragma once

#include "websocket_client.h"
#include "ap_definitions.h"
#include <memory>
#include <functional>
#include <unordered_set>
#include <unordered_map>

/**
 * Main Archipelago client that handles connection and communication
 * with Archipelago multiworld servers.
 */
class ArchipelagoClient
{
public:
    ArchipelagoClient();
    ~ArchipelagoClient();
    
    // Connection management
    bool Connect(const std::string& address, int port, const std::string& slot_name, const std::string& password = "");
    void Disconnect();
    bool IsConnected() const;
    APConnectionState GetConnectionState() const { return connection_state_; }
    
    // Authentication
    bool Authenticate(const std::string& slot_name, const std::string& password = "");
    
    // Location and item handling
    void CheckLocation(int64_t location_id);
    void CheckLocations(const std::vector<int64_t>& location_ids);
    bool IsLocationChecked(int64_t location_id) const;
    
    // Item management
    std::vector<APNetworkItem> GetPendingItems();
    void ClearPendingItems();
    int32_t GetReceivedItemCount() const { return received_item_count_; }
    
    // Status updates
    void UpdateStatus(APClientStatus status);
    APClientStatus GetStatus() const { return current_status_; }
    
    // Chat
    void SendChatMessage(const std::string& message);
    
    // Data requests
    void RequestDataPackage(const std::vector<std::string>& games = {});
    void SyncData();
    
    // Callback registration
    void SetConnectedCallback(std::function<void()> callback) { on_connected_ = callback; }
    void SetDisconnectedCallback(std::function<void()> callback) { on_disconnected_ = callback; }
    void SetItemReceivedCallback(std::function<void(const APNetworkItem&)> callback) { on_item_received_ = callback; }
    void SetLocationCheckedCallback(std::function<void(int64_t)> callback) { on_location_checked_ = callback; }
    void SetChatMessageCallback(std::function<void(const std::string&)> callback) { on_chat_message_ = callback; }
    void SetErrorCallback(std::function<void(const std::string&)> callback) { on_error_ = callback; }
    
    // Game information
    const std::vector<APNetworkPlayer>& GetPlayers() const { return players_; }
    const std::vector<int64_t>& GetMissingLocations() const { return missing_locations_; }
    const std::vector<int64_t>& GetCheckedLocations() const { return checked_locations_; }
    int32_t GetTeam() const { return team_; }
    int32_t GetSlot() const { return slot_; }
    int32_t GetHintPoints() const { return hint_points_; }
    
    // Configuration
    void SetAutoReconnect(bool enabled) { auto_reconnect_ = enabled; }
    void SetReconnectDelay(int milliseconds) { reconnect_delay_ = milliseconds; }
    
    // Statistics
    size_t GetMessagesSent() const;
    size_t GetMessagesReceived() const;
    
private:
    // WebSocket event handlers
    void OnWebSocketConnected();
    void OnWebSocketDisconnected(int code, const std::string& reason);
    void OnWebSocketMessage(const WSMessage& message);
    void OnWebSocketError(const std::string& error);
    
    // Protocol packet handlers
    void HandleRoomInfo(const APRoomInfoPacket& packet);
    void HandleConnected(const APConnectedPacket& packet);
    void HandleConnectionRefused(const APConnectionRefusedPacket& packet);
    void HandleReceivedItems(const APReceivedItemsPacket& packet);
    void HandleLocationInfo(const APLocationInfoPacket& packet);
    void HandleRoomUpdate(const APRoomUpdatePacket& packet);
    void HandlePrintJSON(const APPrintJSONPacket& packet);
    
    // Packet processing
    void ProcessIncomingPacket(const std::string& json_data);
    void SendPacket(const APPacket& packet);
    
    // State management
    void SetConnectionState(APConnectionState state);
    void ResetState();
    
    // Auto-reconnection
    void StartReconnectTimer();
    void OnReconnectTimer();
    
    // WebSocket client
    std::unique_ptr<WebSocketClient> ws_client_;
    
    // Connection state
    APConnectionState connection_state_;
    std::string server_address_;
    int server_port_;
    std::string slot_name_;
    std::string password_;
    bool auto_reconnect_;
    int reconnect_delay_;
    
    // Game state
    int32_t team_;
    int32_t slot_;
    std::vector<APNetworkPlayer> players_;
    std::vector<int64_t> missing_locations_;
    std::vector<int64_t> checked_locations_;
    std::unordered_set<int64_t> checked_locations_set_;
    std::vector<APNetworkItem> pending_items_;
    int32_t received_item_count_;
    int32_t hint_points_;
    APClientStatus current_status_;
    
    // Room information
    APNetworkVersion server_version_;
    std::vector<std::string> server_tags_;
    bool requires_password_;
    std::map<std::string, APPermission> permissions_;
    int32_t hint_cost_;
    int32_t location_check_points_;
    std::vector<std::string> games_;
    std::string seed_name_;
    
    // UUID for this client session
    std::string client_uuid_;
    
    // Callbacks
    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
    std::function<void(const APNetworkItem&)> on_item_received_;
    std::function<void(int64_t)> on_location_checked_;
    std::function<void(const std::string&)> on_chat_message_;
    std::function<void(const std::string&)> on_error_;
    
    // Threading for reconnection
    std::unique_ptr<std::thread> reconnect_thread_;
    std::atomic<bool> should_reconnect_;
};

/**
 * Utility class for managing Archipelago connections in Selaco
 */
class SelacoDArchipelagoManager
{
public:
    static SelacoDArchipelagoManager& GetInstance();
    
    // Singleton access to the client
    ArchipelagoClient& GetClient() { return *client_; }
    
    // Game-specific helpers
    void Initialize();
    void Shutdown();
    
    // Location helpers for Selaco
    void CheckSelacoDLocation(int selaco_location_id);
    bool IsSelacoDLocationChecked(int selaco_location_id);
    
    // Item helpers for Selaco
    std::vector<APNetworkItem> GetPendingSelacoDItems();
    void ProcessSelacoDItem(const APNetworkItem& item);
    
    // Configuration
    void LoadConfig();
    void SaveConfig();
    
private:
    SelacoDArchipelagoManager();
    ~SelacoDArchipelagoManager();
    
    // Convert between Selaco IDs and Archipelago IDs
    int64_t SelacoDLocationToAP(int selaco_id);
    int64_t APLocationToSelaco(int64_t ap_id);
    
    std::unique_ptr<ArchipelagoClient> client_;
    
    // Configuration
    std::string config_file_path_;
    struct Config
    {
        std::string server_address = "archipelago.gg";
        int server_port = 38281;
        std::string slot_name;
        std::string password;
        bool auto_connect = false;
        bool auto_reconnect = true;
        int reconnect_delay = 3000;
        bool show_notifications = true;
        bool show_chat = true;
    } config_;
};