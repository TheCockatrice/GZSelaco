#include "ap_definitions.h"
#include <random>
#include <sstream>
#include <iomanip>

// APNetworkVersion implementation
rapidjson::Value APNetworkVersion::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("major", major, allocator);
    obj.AddMember("minor", minor, allocator);
    obj.AddMember("build", build, allocator);
    obj.AddMember("class", rapidjson::Value(class_name.c_str(), allocator), allocator);
    return obj;
}

bool APNetworkVersion::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("major") && value["major"].IsInt()) {
        major = value["major"].GetInt();
    }
    if (value.HasMember("minor") && value["minor"].IsInt()) {
        minor = value["minor"].GetInt();
    }
    if (value.HasMember("build") && value["build"].IsInt()) {
        build = value["build"].GetInt();
    }
    if (value.HasMember("class") && value["class"].IsString()) {
        class_name = value["class"].GetString();
    }
    
    return true;
}

// APNetworkItem implementation
rapidjson::Value APNetworkItem::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("item", item, allocator);
    obj.AddMember("location", location, allocator);
    obj.AddMember("player", player, allocator);
    obj.AddMember("flags", flags, allocator);
    return obj;
}

bool APNetworkItem::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("item") && value["item"].IsInt64()) {
        item = value["item"].GetInt64();
    }
    if (value.HasMember("location") && value["location"].IsInt64()) {
        location = value["location"].GetInt64();
    }
    if (value.HasMember("player") && value["player"].IsInt()) {
        player = value["player"].GetInt();
    }
    if (value.HasMember("flags") && value["flags"].IsInt()) {
        flags = value["flags"].GetInt();
    }
    
    return true;
}

// APNetworkPlayer implementation
rapidjson::Value APNetworkPlayer::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("team", team, allocator);
    obj.AddMember("slot", slot, allocator);
    obj.AddMember("alias", rapidjson::Value(alias.c_str(), allocator), allocator);
    obj.AddMember("name", rapidjson::Value(name.c_str(), allocator), allocator);
    return obj;
}

bool APNetworkPlayer::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("team") && value["team"].IsInt()) {
        team = value["team"].GetInt();
    }
    if (value.HasMember("slot") && value["slot"].IsInt()) {
        slot = value["slot"].GetInt();
    }
    if (value.HasMember("alias") && value["alias"].IsString()) {
        alias = value["alias"].GetString();
    }
    if (value.HasMember("name") && value["name"].IsString()) {
        name = value["name"].GetString();
    }
    
    return true;
}

// APJSONMessagePart implementation
rapidjson::Value APJSONMessagePart::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
    obj.AddMember("text", rapidjson::Value(text.c_str(), allocator), allocator);
    if (!color.empty()) {
        obj.AddMember("color", rapidjson::Value(color.c_str(), allocator), allocator);
    }
    if (flags != 0) {
        obj.AddMember("flags", flags, allocator);
    }
    if (player != 0) {
        obj.AddMember("player", player, allocator);
    }
    return obj;
}

bool APJSONMessagePart::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("type") && value["type"].IsString()) {
        type = value["type"].GetString();
    }
    if (value.HasMember("text") && value["text"].IsString()) {
        text = value["text"].GetString();
    }
    if (value.HasMember("color") && value["color"].IsString()) {
        color = value["color"].GetString();
    }
    if (value.HasMember("flags") && value["flags"].IsInt()) {
        flags = value["flags"].GetInt();
    }
    if (value.HasMember("player") && value["player"].IsInt()) {
        player = value["player"].GetInt();
    }
    
    return true;
}

// Connect packet
rapidjson::Value APConnectPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("password", rapidjson::Value(password.c_str(), allocator), allocator);
    obj.AddMember("game", rapidjson::Value(game.c_str(), allocator), allocator);
    obj.AddMember("name", rapidjson::Value(name.c_str(), allocator), allocator);
    obj.AddMember("uuid", rapidjson::Value(uuid.c_str(), allocator), allocator);
    obj.AddMember("version", version.ToJSON(allocator), allocator);
    obj.AddMember("items_handling", items_handling, allocator);
    obj.AddMember("slot_data", slot_data, allocator);
    
    rapidjson::Value tags_array(rapidjson::kArrayType);
    for (const std::string& tag : tags) {
        tags_array.PushBack(rapidjson::Value(tag.c_str(), allocator), allocator);
    }
    obj.AddMember("tags", tags_array, allocator);
    
    return obj;
}

bool APConnectPacket::FromJSON(const rapidjson::Value& value)
{
    // Connect packets are client->server, typically don't need to parse
    return true;
}

// Sync packet
rapidjson::Value APSyncPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    return obj;
}

bool APSyncPacket::FromJSON(const rapidjson::Value& value)
{
    return true;
}

// LocationChecks packet
rapidjson::Value APLocationChecksPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    
    rapidjson::Value locations_array(rapidjson::kArrayType);
    for (int64_t location : locations) {
        locations_array.PushBack(location, allocator);
    }
    obj.AddMember("locations", locations_array, allocator);
    
    return obj;
}

bool APLocationChecksPacket::FromJSON(const rapidjson::Value& value)
{
    return true;
}

// StatusUpdate packet
rapidjson::Value APStatusUpdatePacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("status", static_cast<int32_t>(status), allocator);
    return obj;
}

bool APStatusUpdatePacket::FromJSON(const rapidjson::Value& value)
{
    return true;
}

// Say packet
rapidjson::Value APSayPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("text", rapidjson::Value(text.c_str(), allocator), allocator);
    return obj;
}

bool APSayPacket::FromJSON(const rapidjson::Value& value)
{
    return true;
}

// GetDataPackage packet
rapidjson::Value APGetDataPackagePacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    
    if (!games.empty()) {
        rapidjson::Value games_array(rapidjson::kArrayType);
        for (const std::string& game : games) {
            games_array.PushBack(rapidjson::Value(game.c_str(), allocator), allocator);
        }
        obj.AddMember("games", games_array, allocator);
    }
    
    return obj;
}

bool APGetDataPackagePacket::FromJSON(const rapidjson::Value& value)
{
    return true;
}

// RoomInfo packet (server->client)
rapidjson::Value APRoomInfoPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("version", version.ToJSON(allocator), allocator);
    obj.AddMember("generator_version", generator_version.ToJSON(allocator), allocator);
    obj.AddMember("password", password, allocator);
    obj.AddMember("hint_cost", hint_cost, allocator);
    obj.AddMember("location_check_points", location_check_points, allocator);
    obj.AddMember("seed_name", rapidjson::Value(seed_name.c_str(), allocator), allocator);
    obj.AddMember("time", time, allocator);
    
    rapidjson::Value tags_array(rapidjson::kArrayType);
    for (const std::string& tag : tags) {
        tags_array.PushBack(rapidjson::Value(tag.c_str(), allocator), allocator);
    }
    obj.AddMember("tags", tags_array, allocator);
    
    rapidjson::Value games_array(rapidjson::kArrayType);
    for (const std::string& game : games) {
        games_array.PushBack(rapidjson::Value(game.c_str(), allocator), allocator);
    }
    obj.AddMember("games", games_array, allocator);
    
    return obj;
}

bool APRoomInfoPacket::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("version") && value["version"].IsObject()) {
        version.FromJSON(value["version"]);
    }
    if (value.HasMember("generator_version") && value["generator_version"].IsObject()) {
        generator_version.FromJSON(value["generator_version"]);
    }
    if (value.HasMember("password") && value["password"].IsBool()) {
        password = value["password"].GetBool();
    }
    if (value.HasMember("hint_cost") && value["hint_cost"].IsInt()) {
        hint_cost = value["hint_cost"].GetInt();
    }
    if (value.HasMember("location_check_points") && value["location_check_points"].IsInt()) {
        location_check_points = value["location_check_points"].GetInt();
    }
    if (value.HasMember("seed_name") && value["seed_name"].IsString()) {
        seed_name = value["seed_name"].GetString();
    }
    if (value.HasMember("time") && value["time"].IsDouble()) {
        time = value["time"].GetDouble();
    }
    
    if (value.HasMember("tags") && value["tags"].IsArray()) {
        tags.clear();
        for (const auto& tag : value["tags"].GetArray()) {
            if (tag.IsString()) {
                tags.push_back(tag.GetString());
            }
        }
    }
    
    if (value.HasMember("games") && value["games"].IsArray()) {
        games.clear();
        for (const auto& game : value["games"].GetArray()) {
            if (game.IsString()) {
                games.push_back(game.GetString());
            }
        }
    }
    
    return true;
}

// Connected packet
rapidjson::Value APConnectedPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("team", team, allocator);
    obj.AddMember("slot", slot, allocator);
    obj.AddMember("hint_points", hint_points, allocator);
    
    rapidjson::Value players_array(rapidjson::kArrayType);
    for (const APNetworkPlayer& player : players) {
        players_array.PushBack(player.ToJSON(allocator), allocator);
    }
    obj.AddMember("players", players_array, allocator);
    
    rapidjson::Value missing_array(rapidjson::kArrayType);
    for (int64_t location : missing_locations) {
        missing_array.PushBack(location, allocator);
    }
    obj.AddMember("missing_locations", missing_array, allocator);
    
    rapidjson::Value checked_array(rapidjson::kArrayType);
    for (int64_t location : checked_locations) {
        checked_array.PushBack(location, allocator);
    }
    obj.AddMember("checked_locations", checked_array, allocator);
    
    return obj;
}

bool APConnectedPacket::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("team") && value["team"].IsInt()) {
        team = value["team"].GetInt();
    }
    if (value.HasMember("slot") && value["slot"].IsInt()) {
        slot = value["slot"].GetInt();
    }
    if (value.HasMember("hint_points") && value["hint_points"].IsInt()) {
        hint_points = value["hint_points"].GetInt();
    }
    
    if (value.HasMember("players") && value["players"].IsArray()) {
        players.clear();
        for (const auto& player_val : value["players"].GetArray()) {
            APNetworkPlayer player;
            if (player.FromJSON(player_val)) {
                players.push_back(player);
            }
        }
    }
    
    if (value.HasMember("missing_locations") && value["missing_locations"].IsArray()) {
        missing_locations.clear();
        for (const auto& loc : value["missing_locations"].GetArray()) {
            if (loc.IsInt64()) {
                missing_locations.push_back(loc.GetInt64());
            }
        }
    }
    
    if (value.HasMember("checked_locations") && value["checked_locations"].IsArray()) {
        checked_locations.clear();
        for (const auto& loc : value["checked_locations"].GetArray()) {
            if (loc.IsInt64()) {
                checked_locations.push_back(loc.GetInt64());
            }
        }
    }
    
    return true;
}

// ConnectionRefused packet
rapidjson::Value APConnectionRefusedPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    
    rapidjson::Value errors_array(rapidjson::kArrayType);
    for (const std::string& error : errors) {
        errors_array.PushBack(rapidjson::Value(error.c_str(), allocator), allocator);
    }
    obj.AddMember("errors", errors_array, allocator);
    
    return obj;
}

bool APConnectionRefusedPacket::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("errors") && value["errors"].IsArray()) {
        errors.clear();
        for (const auto& error : value["errors"].GetArray()) {
            if (error.IsString()) {
                errors.push_back(error.GetString());
            }
        }
    }
    
    return true;
}

// ReceivedItems packet
rapidjson::Value APReceivedItemsPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("index", index, allocator);
    
    rapidjson::Value items_array(rapidjson::kArrayType);
    for (const APNetworkItem& item : items) {
        items_array.PushBack(item.ToJSON(allocator), allocator);
    }
    obj.AddMember("items", items_array, allocator);
    
    return obj;
}

bool APReceivedItemsPacket::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("index") && value["index"].IsInt()) {
        index = value["index"].GetInt();
    }
    
    if (value.HasMember("items") && value["items"].IsArray()) {
        items.clear();
        for (const auto& item_val : value["items"].GetArray()) {
            APNetworkItem item;
            if (item.FromJSON(item_val)) {
                items.push_back(item);
            }
        }
    }
    
    return true;
}

// PrintJSON packet
rapidjson::Value APPrintJSONPacket::ToJSON(rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("cmd", rapidjson::Value(cmd.c_str(), allocator), allocator);
    obj.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
    
    rapidjson::Value data_array(rapidjson::kArrayType);
    for (const APJSONMessagePart& part : data) {
        data_array.PushBack(part.ToJSON(allocator), allocator);
    }
    obj.AddMember("data", data_array, allocator);
    
    return obj;
}

bool APPrintJSONPacket::FromJSON(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    
    if (value.HasMember("type") && value["type"].IsString()) {
        type = value["type"].GetString();
    }
    
    if (value.HasMember("data") && value["data"].IsArray()) {
        data.clear();
        for (const auto& part_val : value["data"].GetArray()) {
            APJSONMessagePart part;
            if (part.FromJSON(part_val)) {
                data.push_back(part);
            }
        }
    }
    
    return true;
}

// Utility functions
std::string APPacketToString(const APPacket& packet)
{
    rapidjson::Document doc;
    doc.SetObject();
    auto allocator = doc.GetAllocator();
    
    rapidjson::Value packet_obj = packet.ToJSON(allocator);
    doc.CopyFrom(packet_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

std::unique_ptr<APPacket> APPacketFromString(const std::string& json_str)
{
    rapidjson::Document doc;
    
    // Check if it's an array (batch) or single packet
    doc.Parse(json_str.c_str());
    if (doc.HasParseError()) {
        return nullptr;
    }
    
    rapidjson::Value* packet_obj = nullptr;
    if (doc.IsArray() && doc.Size() > 0) {
        packet_obj = &doc[0]; // Take first packet from array
    } else if (doc.IsObject()) {
        packet_obj = &doc;
    } else {
        return nullptr;
    }
    
    if (!packet_obj->HasMember("cmd") || !(*packet_obj)["cmd"].IsString()) {
        return nullptr;
    }
    
    std::string cmd = (*packet_obj)["cmd"].GetString();
    
    std::unique_ptr<APPacket> packet;
    
    if (cmd == "RoomInfo") {
        packet = std::make_unique<APRoomInfoPacket>();
    } else if (cmd == "Connected") {
        packet = std::make_unique<APConnectedPacket>();
    } else if (cmd == "ConnectionRefused") {
        packet = std::make_unique<APConnectionRefusedPacket>();
    } else if (cmd == "ReceivedItems") {
        packet = std::make_unique<APReceivedItemsPacket>();
    } else if (cmd == "PrintJSON") {
        packet = std::make_unique<APPrintJSONPacket>();
    } else {
        return nullptr; // Unknown packet type
    }
    
    if (packet && packet->FromJSON(*packet_obj)) {
        return packet;
    }
    
    return nullptr;
}

std::string GenerateUUID()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    };
    return ss.str();
}