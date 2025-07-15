#include "archipelago_client.h"
#include <random>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <algorithm>

ArchipelagoClient::ArchipelagoClient()
    : connection_state_(APConnectionState::Disconnected)
    , server_port_(38281)
    , auto_reconnect_(true)
    , reconnect_delay_(3000)
    , team_(0)
    , slot_(0)
    , received_item_count_(0)
    , hint_points_(0)
    , current_status_(APClientStatus::CLIENT_UNKNOWN)
    , requires_password_(false)
    , should_reconnect_(false)
{
    ws_client_ = std::make_unique<WebSocketClient>();
    
    // Set up WebSocket callbacks
    ws_client_->SetConnectedCallback([this]() { OnWebSocketConnected(); });
    ws_client_->SetDisconnectedCallback([this](int code, const std::string& reason) { 
        OnWebSocketDisconnected(code, reason); 
    });
    ws_client_->SetMessageCallback([this](const WSMessage& message) { 
        OnWebSocketMessage(message); 
    });
    ws_client_->SetErrorCallback([this](const std::string& error) { 
        OnWebSocketError(error); 
    });
    
    // Generate unique client UUID
    client_uuid_ = GenerateUUID();
}

ArchipelagoClient::~ArchipelagoClient()
{
    Disconnect();
    should_reconnect_ = false;
    
    if (reconnect_thread_ && reconnect_thread_->joinable()) {
        reconnect_thread_->join();
    }
}

bool ArchipelagoClient::Connect(const std::string& address, int port, const std::string& slot_name, const std::string& password)
{
    if (connection_state_ == APConnectionState::Connected || connection_state_ == APConnectionState::Connecting) {
        return false;
    }
    
    server_address_ = address;
    server_port_ = port;
    slot_name_ = slot_name;
    password_ = password;
    
    SetConnectionState(APConnectionState::Connecting);
    
    // Connect to WebSocket
    std::string ws_url = "ws://" + address + ":" + std::to_string(port) + "/";
    return ws_client_->Connect(ws_url);
}

void ArchipelagoClient::Disconnect()
{
    if (ws_client_) {
        ws_client_->Disconnect();
    }
    SetConnectionState(APConnectionState::Disconnected);
}

bool ArchipelagoClient::IsConnected() const
{
    return connection_state_ == APConnectionState::Connected;
}

bool ArchipelagoClient::Authenticate(const std::string& slot_name, const std::string& password)
{
    if (!ws_client_->IsConnected()) {
        return false;
    }
    
    // Create Connect packet
    APConnectPacket connect_packet;
    connect_packet.password = password;
    connect_packet.name = slot_name;
    connect_packet.uuid = client_uuid_;
    connect_packet.version.major = 0;
    connect_packet.version.minor = 5;
    connect_packet.version.build = 1;
    connect_packet.items_handling = 0b111; // Get all items
    connect_packet.tags.push_back("AP");
    connect_packet.slot_data = true;
    
    SendPacket(connect_packet);
    return true;
}

void ArchipelagoClient::CheckLocation(int64_t location_id)
{
    CheckLocations({location_id});
}

void ArchipelagoClient::CheckLocations(const std::vector<int64_t>& location_ids)
{
    if (!IsConnected()) return;
    
    // Update our local state
    for (int64_t location_id : location_ids) {
        if (checked_locations_set_.find(location_id) == checked_locations_set_.end()) {
            checked_locations_.push_back(location_id);
            checked_locations_set_.insert(location_id);
            
            // Remove from missing locations
            auto it = std::find(missing_locations_.begin(), missing_locations_.end(), location_id);
            if (it != missing_locations_.end()) {
                missing_locations_.erase(it);
            }
            
            if (on_location_checked_) {
                on_location_checked_(location_id);
            }
        }
    }
    
    // Send to server
    APLocationChecksPacket packet;
    packet.locations = location_ids;
    SendPacket(packet);
}

bool ArchipelagoClient::IsLocationChecked(int64_t location_id) const
{
    return checked_locations_set_.find(location_id) != checked_locations_set_.end();
}

std::vector<APNetworkItem> ArchipelagoClient::GetPendingItems()
{
    std::vector<APNetworkItem> items = pending_items_;
    return items;
}

void ArchipelagoClient::ClearPendingItems()
{
    pending_items_.clear();
}

void ArchipelagoClient::UpdateStatus(APClientStatus status)
{
    current_status_ = status;
    
    if (!IsConnected()) return;
    
    APStatusUpdatePacket packet;
    packet.status = status;
    SendPacket(packet);
}

void ArchipelagoClient::SendChatMessage(const std::string& message)
{
    if (!IsConnected()) return;
    
    APSayPacket packet;
    packet.text = message;
    SendPacket(packet);
}

void ArchipelagoClient::RequestDataPackage(const std::vector<std::string>& games)
{
    if (!ws_client_->IsConnected()) return;
    
    APGetDataPackagePacket packet;
    packet.games = games;
    SendPacket(packet);
}

void ArchipelagoClient::SyncData()
{
    if (!IsConnected()) return;
    
    APSyncPacket packet;
    SendPacket(packet);
}

size_t ArchipelagoClient::GetMessagesSent() const
{
    return ws_client_ ? ws_client_->GetMessagesSent() : 0;
}

size_t ArchipelagoClient::GetMessagesReceived() const
{
    return ws_client_ ? ws_client_->GetMessagesReceived() : 0;
}

void ArchipelagoClient::OnWebSocketConnected()
{
    std::cout << "WebSocket connected to Archipelago server" << std::endl;
    // Connection established, wait for RoomInfo packet
}

void ArchipelagoClient::OnWebSocketDisconnected(int code, const std::string& reason)
{
    std::cout << "WebSocket disconnected: " << reason << " (code: " << code << ")" << std::endl;
    
    SetConnectionState(APConnectionState::Disconnected);
    
    if (on_disconnected_) {
        on_disconnected_();
    }
    
    if (auto_reconnect_ && should_reconnect_) {
        StartReconnectTimer();
    }
}

void ArchipelagoClient::OnWebSocketMessage(const WSMessage& message)
{
    if (message.type == WSMessageType::Text) {
        ProcessIncomingPacket(message.data);
    }
}

void ArchipelagoClient::OnWebSocketError(const std::string& error)
{
    std::cout << "WebSocket error: " << error << std::endl;
    
    if (on_error_) {
        on_error_(error);
    }
    
    SetConnectionState(APConnectionState::Failed);
}

void ArchipelagoClient::ProcessIncomingPacket(const std::string& json_data)
{
    try {
        auto packet = APPacketFromString(json_data);
        if (!packet) {
            std::cout << "Failed to parse packet: " << json_data << std::endl;
            return;
        }
        
        // Handle different packet types
        if (packet->cmd == "RoomInfo") {
            auto* room_info = dynamic_cast<APRoomInfoPacket*>(packet.get());
            if (room_info) HandleRoomInfo(*room_info);
        }
        else if (packet->cmd == "Connected") {
            auto* connected = dynamic_cast<APConnectedPacket*>(packet.get());
            if (connected) HandleConnected(*connected);
        }
        else if (packet->cmd == "ConnectionRefused") {
            auto* refused = dynamic_cast<APConnectionRefusedPacket*>(packet.get());
            if (refused) HandleConnectionRefused(*refused);
        }
        else if (packet->cmd == "ReceivedItems") {
            auto* items = dynamic_cast<APReceivedItemsPacket*>(packet.get());
            if (items) HandleReceivedItems(*items);
        }
        else if (packet->cmd == "PrintJSON") {
            auto* print_json = dynamic_cast<APPrintJSONPacket*>(packet.get());
            if (print_json) HandlePrintJSON(*print_json);
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error processing packet: " << e.what() << std::endl;
    }
}

void ArchipelagoClient::SendPacket(const APPacket& packet)
{
    if (!ws_client_ || !ws_client_->IsConnected()) {
        return;
    }
    
    try {
        std::string json_str = APPacketToString(packet);
        ws_client_->SendText("[" + json_str + "]");
    } catch (const std::exception& e) {
        std::cout << "Error sending packet: " << e.what() << std::endl;
    }
}

void ArchipelagoClient::HandleRoomInfo(const APRoomInfoPacket& packet)
{
    std::cout << "Received RoomInfo for seed: " << packet.seed_name << std::endl;
    
    server_version_ = packet.version;
    server_tags_ = packet.tags;
    requires_password_ = packet.password;
    permissions_ = packet.permissions;
    hint_cost_ = packet.hint_cost;
    location_check_points_ = packet.location_check_points;
    games_ = packet.games;
    seed_name_ = packet.seed_name;
    
    // Now authenticate
    if (!slot_name_.empty()) {
        Authenticate(slot_name_, password_);
    }
}

void ArchipelagoClient::HandleConnected(const APConnectedPacket& packet)
{
    std::cout << "Successfully connected to Archipelago as " << slot_name_ 
              << " (Team " << packet.team << ", Slot " << packet.slot << ")" << std::endl;
    
    team_ = packet.team;
    slot_ = packet.slot;
    players_ = packet.players;
    missing_locations_ = packet.missing_locations;
    checked_locations_ = packet.checked_locations;
    hint_points_ = packet.hint_points;
    
    // Build checked locations set for fast lookup
    checked_locations_set_.clear();
    for (int64_t location_id : checked_locations_) {
        checked_locations_set_.insert(location_id);
    }
    
    SetConnectionState(APConnectionState::Connected);
    
    // Update status to ready
    UpdateStatus(APClientStatus::CLIENT_READY);
    
    if (on_connected_) {
        on_connected_();
    }
}

void ArchipelagoClient::HandleConnectionRefused(const APConnectionRefusedPacket& packet)
{
    std::cout << "Connection refused by server:" << std::endl;
    for (const std::string& error : packet.errors) {
        std::cout << "  - " << error << std::endl;
    }
    
    SetConnectionState(APConnectionState::Failed);
    
    if (on_error_) {
        std::string error_msg = "Connection refused: ";
        for (const std::string& error : packet.errors) {
            error_msg += error + " ";
        }
        on_error_(error_msg);
    }
}

void ArchipelagoClient::HandleReceivedItems(const APReceivedItemsPacket& packet)
{
    std::cout << "Received " << packet.items.size() << " items from Archipelago" << std::endl;
    
    if (packet.index == 0) {
        // Full resync - replace all items
        pending_items_ = packet.items;
        received_item_count_ = packet.items.size();
    } else {
        // Incremental update - add new items
        for (const APNetworkItem& item : packet.items) {
            pending_items_.push_back(item);
            received_item_count_++;
            
            if (on_item_received_) {
                on_item_received_(item);
            }
        }
    }
}

void ArchipelagoClient::HandleLocationInfo(const APLocationInfoPacket& packet)
{
    // Handle scouted location information
    std::cout << "Received location info for " << packet.locations.size() << " locations" << std::endl;
}

void ArchipelagoClient::HandleRoomUpdate(const APRoomUpdatePacket& packet)
{
    // Handle room updates (player changes, new location checks, etc.)
    std::cout << "Received room update" << std::endl;
}

void ArchipelagoClient::HandlePrintJSON(const APPrintJSONPacket& packet)
{
    // Convert PrintJSON to readable text and call chat callback
    std::string message;
    for (const APJSONMessagePart& part : packet.data) {
        if (!part.text.empty()) {
            message += part.text;
        }
    }
    
    if (!message.empty() && on_chat_message_) {
        on_chat_message_(message);
    }
}

void ArchipelagoClient::SetConnectionState(APConnectionState state)
{
    connection_state_ = state;
}

void ArchipelagoClient::ResetState()
{
    team_ = 0;
    slot_ = 0;
    players_.clear();
    missing_locations_.clear();
    checked_locations_.clear();
    checked_locations_set_.clear();
    pending_items_.clear();
    received_item_count_ = 0;
    hint_points_ = 0;
    current_status_ = APClientStatus::CLIENT_UNKNOWN;
}

void ArchipelagoClient::StartReconnectTimer()
{
    should_reconnect_ = true;
    reconnect_thread_ = std::make_unique<std::thread>([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_));
        OnReconnectTimer();
    });
}

void ArchipelagoClient::OnReconnectTimer()
{
    if (should_reconnect_ && connection_state_ == APConnectionState::Disconnected) {
        std::cout << "Attempting to reconnect to Archipelago..." << std::endl;
        Connect(server_address_, server_port_, slot_name_, password_);
    }
}

// SelacoDArchipelagoManager implementation

SelacoDArchipelagoManager& SelacoDArchipelagoManager::GetInstance()
{
    static SelacoDArchipelagoManager instance;
    return instance;
}

SelacoDArchipelagoManager::SelacoDArchipelagoManager()
{
    client_ = std::make_unique<ArchipelagoClient>();
    config_file_path_ = "archipelago.cfg";
}

SelacoDArchipelagoManager::~SelacoDArchipelagoManager()
{
    Shutdown();
}

void SelacoDArchipelagoManager::Initialize()
{
    LoadConfig();
    
    // Set up client callbacks
    client_->SetConnectedCallback([this]() {
        std::cout << "Connected to Archipelago!" << std::endl;
    });
    
    client_->SetDisconnectedCallback([this]() {
        std::cout << "Disconnected from Archipelago" << std::endl;
    });
    
    client_->SetItemReceivedCallback([this](const APNetworkItem& item) {
        ProcessSelacoDItem(item);
    });
    
    client_->SetErrorCallback([this](const std::string& error) {
        std::cout << "Archipelago error: " << error << std::endl;
    });
    
    // Auto-connect if enabled
    if (config_.auto_connect && !config_.slot_name.empty()) {
        client_->Connect(config_.server_address, config_.server_port, 
                        config_.slot_name, config_.password);
    }
}

void SelacoDArchipelagoManager::Shutdown()
{
    if (client_) {
        client_->Disconnect();
    }
    SaveConfig();
}

void SelacoDArchipelagoManager::CheckSelacoDLocation(int selaco_location_id)
{
    int64_t ap_location_id = SelacoDLocationToAP(selaco_location_id);
    client_->CheckLocation(ap_location_id);
}

bool SelacoDArchipelagoManager::IsSelacoDLocationChecked(int selaco_location_id)
{
    int64_t ap_location_id = SelacoDLocationToAP(selaco_location_id);
    return client_->IsLocationChecked(ap_location_id);
}

std::vector<APNetworkItem> SelacoDArchipelagoManager::GetPendingSelacoDItems()
{
    return client_->GetPendingItems();
}

void SelacoDArchipelagoManager::ProcessSelacoDItem(const APNetworkItem& item)
{
    // Convert AP item to Selaco item and give to player
    std::cout << "Processing Selaco item: " << item.item << std::endl;
    
    // This would need to be integrated with Selaco's item system
    // For now, just log the item
}

int64_t SelacoDArchipelagoManager::SelacoDLocationToAP(int selaco_id)
{
    return SelacoDefs::LOCATION_BASE + selaco_id;
}

int64_t SelacoDArchipelagoManager::APLocationToSelaco(int64_t ap_id)
{
    return ap_id - SelacoDefs::LOCATION_BASE;
}

void SelacoDArchipelagoManager::LoadConfig()
{
    // Simple config loading (could be enhanced with proper config parsing)
    std::ifstream file(config_file_path_);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("server_address=") == 0) {
                config_.server_address = line.substr(15);
            }
            else if (line.find("server_port=") == 0) {
                config_.server_port = std::stoi(line.substr(12));
            }
            else if (line.find("slot_name=") == 0) {
                config_.slot_name = line.substr(10);
            }
            else if (line.find("password=") == 0) {
                config_.password = line.substr(9);
            }
            else if (line.find("auto_connect=") == 0) {
                config_.auto_connect = (line.substr(13) == "true");
            }
        }
        file.close();
    }
}

void SelacoDArchipelagoManager::SaveConfig()
{
    std::ofstream file(config_file_path_);
    if (file.is_open()) {
        file << "server_address=" << config_.server_address << std::endl;
        file << "server_port=" << config_.server_port << std::endl;
        file << "slot_name=" << config_.slot_name << std::endl;
        file << "password=" << config_.password << std::endl;
        file << "auto_connect=" << (config_.auto_connect ? "true" : "false") << std::endl;
        file.close();
    }
}