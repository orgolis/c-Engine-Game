#pragma once

// ============================================================================
// Lobby protocol -- lobby codes over a relay (shared by the lobby server and
// the game). See docs/systems/ONLINE_PLAY.md, "Lobby codes".
// ============================================================================
//
// A host registers a lobby with the lobby server and gets a short code; a
// player who types the code gets a relay port on the server and connects the
// game (ENet) to it. Every connection goes OUT from a home network to the
// server, so no router has to let anything in -- the way a TURN relay works:
//
//   host game socket --CREATE------------------> server:control   -> CREATED code
//   joiner           --JOIN code----------------> server:control   -> SLOT port
//   server:control   --PEER code port-----------> host game socket (a joiner is coming)
//   host game socket --PUNCH code---------------> server:port      (opens the host's router
//                                                                    for that port)
//   joiner ENet  <--> server:port <--> host ENet  (the game's own packets, forwarded as they are)
//
// Control messages are one UDP datagram of text: the magic, then fields
// separated by tabs. The game's ENet socket carries both its own packets and
// these; the magic tells them apart (ENet's header never starts with it).

#include <cctype>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine::network::lobby {

inline constexpr const char* kMagic = "GWSLOBBY1\t";
inline constexpr size_t      kMagicSize = 10;
inline constexpr uint16_t    kDefaultPort = 7780;          // control; relay ports follow it
inline constexpr int         kCodeLength = 6;
// 32 letters and digits with nothing to mistake for another (no 0/O, no 1/I).
inline constexpr const char* kCodeAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

// Seconds. A host says it is there every kKeepSeconds; a lobby not heard from
// for kLobbyTimeout is closed, a relay port idle for kSlotTimeout is freed.
inline constexpr float kKeepSeconds = 4.0f;
inline constexpr float kLobbyTimeout = 20.0f;
inline constexpr float kSlotTimeout = 30.0f;

inline bool is_control(const void* data, size_t size) {
    if (size < kMagicSize) return false;
    const char* c = static_cast<const char*>(data);
    for (size_t i = 0; i < kMagicSize; ++i)
        if (c[i] != kMagic[i]) return false;
    return true;
}

/// A STUN Binding response (RFC 5389: its magic cookie at bytes 4..7). The
/// game's socket asks a STUN server for its public address (lobby_direct.h);
/// the answer arrives there among the game's own packets.
inline bool is_stun_response(const void* data, size_t size) {
    if (size < 20) return false;
    const auto* c = static_cast<const unsigned char*>(data);
    return c[0] == 0x01 && (c[1] == 0x01 || c[1] == 0x11) && c[4] == 0x21 && c[5] == 0x12 && c[6] == 0xA4 && c[7] == 0x42;
}

/// A datagram: the magic, then the fields, tab-separated. Tabs and line
/// breaks inside a field become spaces.
inline std::string make(const std::vector<std::string>& fields) {
    std::string out = kMagic;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out += '\t';
        for (char c : fields[i]) out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    return out;
}

/// The fields of a control datagram (empty when it is not one). At most 16
/// fields of 256 characters: a datagram from the internet is read with care.
inline std::vector<std::string> parse(const void* data, size_t size) {
    std::vector<std::string> fields;
    if (!is_control(data, size) || size > 2048) return fields;
    const char* c = static_cast<const char*>(data);
    std::string cur;
    for (size_t i = kMagicSize; i < size; ++i) {
        if (c[i] == '\t') {
            fields.push_back(cur);
            cur.clear();
            if (fields.size() >= 16) return {};
            continue;
        }
        if (cur.size() >= 256) return {};
        cur += c[i];
    }
    fields.push_back(cur);
    return fields;
}

/// A typed code made comparable: letters upper case, spaces and dashes gone.
/// "" when it is not a code.
inline std::string normalize_code(const std::string& typed) {
    std::string out;
    for (char ch : typed) {
        if (ch == ' ' || ch == '-' || ch == '_') continue;
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        bool ok = false;
        for (const char* a = kCodeAlphabet; *a; ++a) ok |= *a == u;
        if (!ok) return {};
        out += u;
    }
    return out.size() == static_cast<size_t>(kCodeLength) ? out : std::string();
}

template <class Rng>
std::string random_code(Rng& rng) {
    std::uniform_int_distribution<int> pick(0, 31);
    std::string code;
    for (int i = 0; i < kCodeLength; ++i) code += kCodeAlphabet[pick(rng)];
    return code;
}

/// What a JOIN error means to a player.
inline std::string explain_error(const std::string& reason) {
    if (reason == "unknown") return "No game with that code. Check the code -- or the host has closed the game.";
    if (reason == "full") return "That game is full.";
    if (reason == "game") return "That code is for another game (or another version of this game).";
    if (reason == "ports") return "The lobby server is busy (no free relay ports). Try again soon.";
    if (reason == "code") return "That is not a lobby code (6 letters and digits).";
    return "The lobby server refused: " + reason;
}

}  // namespace engine::network::lobby
