#pragma once

// ============================================================================
// G10 · Multiplayer social — parties, guilds, chat, trade, leaderboards.
// ============================================================================
//
// Session/social state that rides the Networking stack (Stage 7): the gameplay
// layer owns the DATA + rules (who is in a party, who receives a chat message,
// what a trade swaps), and the net layer replicates it. All logic here is
// deterministic + testable offline; the transport/replication is the integration
// point (like the G5 trade primitive). Players are addressed by entity handle
// in-session (a stable NetId for persistence).

#include "world.h"
#include "components.h"           // Transform (proximity chat)
#include "gameplay_items.h"
#include "gameplay_inventory.h"
#include "gameplay_economy.h"     // wallet_*, transfer_item
#include "gameplay_crafting.h"    // ItemStack

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

using PlayerId = uint32_t;   // entity handle in-session

// ---------------- data ----------------
struct Party {
    uint64_t id = 0;
    PlayerId leader = 0;
    std::vector<PlayerId> members;
    bool shared_quests = true;
};
struct GuildMember { PlayerId player; int rank; };   // 0 = member … higher = officer/leader
struct Guild { uint64_t id = 0; std::string name; std::vector<GuildMember> members; };

struct TradeOffer { std::vector<ItemStack> items; float gold = 0.0f; bool confirmed = false; };
struct TradeSession { uint64_t id = 0; PlayerId a = 0, b = 0; TradeOffer offer_a, offer_b; bool open = true; };

struct LeaderboardEntry { std::string name; float score = 0.0f; };

struct SocialState {
    std::vector<Party>            parties;
    std::vector<Guild>            guilds;
    std::vector<TradeSession>     trades;
    std::vector<LeaderboardEntry> leaderboard;
    uint64_t next_id = 1;
    uint64_t alloc_id() { return next_id++; }
};

// ---------------- parties ----------------
inline Party* find_party(SocialState& s, uint64_t id) {
    for (auto& p : s.parties) if (p.id == id) return &p;
    return nullptr;
}
inline Party* party_of(SocialState& s, PlayerId p) {
    for (auto& pt : s.parties) for (PlayerId m : pt.members) if (m == p) return &pt;
    return nullptr;
}
inline uint64_t create_party(SocialState& s, PlayerId leader) {
    if (party_of(s, leader)) return 0;
    Party pt; pt.id = s.alloc_id(); pt.leader = leader; pt.members.push_back(leader);
    s.parties.push_back(std::move(pt));
    return s.parties.back().id;
}
inline bool join_party(SocialState& s, uint64_t id, PlayerId p) {
    Party* pt = find_party(s, id);
    if (!pt || party_of(s, p)) return false;
    pt->members.push_back(p);
    return true;
}
inline void disband_if_empty(SocialState& s, uint64_t id) {
    for (size_t i = 0; i < s.parties.size(); ++i)
        if (s.parties[i].id == id && s.parties[i].members.empty()) { s.parties.erase(s.parties.begin() + i); return; }
}
inline bool leave_party(SocialState& s, PlayerId p) {
    Party* pt = party_of(s, p);
    if (!pt) return false;
    const uint64_t id = pt->id;
    pt->members.erase(std::remove(pt->members.begin(), pt->members.end(), p), pt->members.end());
    if (pt->leader == p && !pt->members.empty()) pt->leader = pt->members.front();
    disband_if_empty(s, id);
    return true;
}
inline bool kick_from_party(SocialState& s, PlayerId leader, PlayerId target) {
    Party* pt = party_of(s, leader);
    if (!pt || pt->leader != leader || target == leader) return false;
    const size_t before = pt->members.size();
    pt->members.erase(std::remove(pt->members.begin(), pt->members.end(), target), pt->members.end());
    return pt->members.size() < before;
}

// ---------------- guilds ----------------
inline Guild* find_guild(SocialState& s, uint64_t id) {
    for (auto& g : s.guilds) if (g.id == id) return &g;
    return nullptr;
}
inline Guild* guild_of(SocialState& s, PlayerId p) {
    for (auto& g : s.guilds) for (auto& m : g.members) if (m.player == p) return &g;
    return nullptr;
}
inline int guild_rank(Guild& g, PlayerId p) {
    for (auto& m : g.members) if (m.player == p) return m.rank;
    return -1;
}
inline uint64_t create_guild(SocialState& s, const std::string& name, PlayerId founder) {
    if (guild_of(s, founder)) return 0;
    Guild g; g.id = s.alloc_id(); g.name = name; g.members.push_back({founder, 2});   // founder = leader (rank 2)
    s.guilds.push_back(std::move(g));
    return s.guilds.back().id;
}
inline bool join_guild(SocialState& s, uint64_t id, PlayerId p) {
    Guild* g = find_guild(s, id);
    if (!g || guild_of(s, p)) return false;
    g->members.push_back({p, 0});
    return true;
}
inline bool set_guild_rank(SocialState& s, uint64_t id, PlayerId officer, PlayerId target, int rank) {
    Guild* g = find_guild(s, id);
    if (!g) return false;
    if (guild_rank(*g, officer) <= guild_rank(*g, target) || rank >= guild_rank(*g, officer)) return false;  // can't promote at/above self
    for (auto& m : g->members) if (m.player == target) { m.rank = rank; return true; }
    return false;
}
inline bool leave_guild(SocialState& s, PlayerId p) {
    Guild* g = guild_of(s, p);
    if (!g) return false;
    g->members.erase(std::remove_if(g->members.begin(), g->members.end(),
                                    [&](const GuildMember& m) { return m.player == p; }), g->members.end());
    return true;
}

// ---------------- chat routing ----------------
enum class ChatChannel { Say, Party, Guild, Whisper, Global };

// Who should receive a message on `channel` from `sender`? (Delivery is the net
// layer's job.) `all_online` is the full player set; `say_range` bounds proximity.
inline std::vector<PlayerId> chat_recipients(World& w, SocialState& s, PlayerId sender, ChatChannel channel,
                                             PlayerId whisper_target, float say_range,
                                             const std::vector<PlayerId>& all_online) {
    switch (channel) {
        case ChatChannel::Global:  return all_online;
        case ChatChannel::Whisper: return {sender, whisper_target};
        case ChatChannel::Party: {
            Party* pt = party_of(s, sender);
            return pt ? pt->members : std::vector<PlayerId>{sender};
        }
        case ChatChannel::Guild: {
            Guild* g = guild_of(s, sender);
            if (!g) return {sender};
            std::vector<PlayerId> r; r.reserve(g->members.size());
            for (auto& m : g->members) r.push_back(m.player);
            return r;
        }
        case ChatChannel::Say: {
            std::vector<PlayerId> r;
            const Transform* st = w.try_get<Transform>(static_cast<Entity>(sender));
            if (!st) return {sender};
            for (PlayerId p : all_online) {
                const Transform* pt = w.try_get<Transform>(static_cast<Entity>(p));
                if (pt && glm::distance(pt->position, st->position) <= say_range) r.push_back(p);
            }
            if (r.empty()) r.push_back(sender);
            return r;
        }
    }
    return {sender};
}

// ---------------- trade ----------------
inline TradeSession* find_trade(SocialState& s, uint64_t id) {
    for (auto& t : s.trades) if (t.id == id) return &t;
    return nullptr;
}
inline uint64_t open_trade(SocialState& s, PlayerId a, PlayerId b) {
    TradeSession t; t.id = s.alloc_id(); t.a = a; t.b = b;
    s.trades.push_back(std::move(t));
    return s.trades.back().id;
}
inline TradeOffer* trade_side(TradeSession& t, PlayerId who) {
    if (who == t.a) return &t.offer_a;
    if (who == t.b) return &t.offer_b;
    return nullptr;
}
inline bool trade_offer_item(TradeSession& t, PlayerId who, const std::string& item_id, int qty) {
    TradeOffer* o = trade_side(t, who);
    if (!o || !t.open) return false;
    o->items.push_back({item_id, qty});
    o->confirmed = false;                 // any change re-opens confirmation
    t.offer_a.confirmed = t.offer_a.confirmed && who != t.a;
    t.offer_b.confirmed = t.offer_b.confirmed && who != t.b;
    return true;
}
inline bool trade_set_gold(TradeSession& t, PlayerId who, float gold) {
    TradeOffer* o = trade_side(t, who);
    if (!o || !t.open) return false;
    o->gold = gold; o->confirmed = false;
    return true;
}
inline bool trade_confirm(TradeSession& t, PlayerId who) {
    TradeOffer* o = trade_side(t, who);
    if (!o || !t.open) return false;
    o->confirmed = true;
    return true;
}
// Execute the swap once both sides confirm (validates items + gold first).
inline bool trade_complete(World& w, SocialState& s, uint64_t id) {
    TradeSession* t = find_trade(s, id);
    if (!t || !t->open || !t->offer_a.confirmed || !t->offer_b.confirmed) return false;
    const Entity ea = static_cast<Entity>(t->a), eb = static_cast<Entity>(t->b);
    auto has_all = [&](Entity e, const TradeOffer& o) {
        const Inventory* inv = w.try_get<Inventory>(e);
        for (const auto& it : o.items) if (!inv || inventory_count(*inv, it.item_id) < it.quantity) return false;
        return wallet_balance(w, e) >= o.gold;
    };
    if (!has_all(ea, t->offer_a) || !has_all(eb, t->offer_b)) return false;
    for (const auto& it : t->offer_a.items) transfer_item(w, ea, eb, it.item_id, it.quantity);
    for (const auto& it : t->offer_b.items) transfer_item(w, eb, ea, it.item_id, it.quantity);
    if (t->offer_a.gold > 0.0f) { wallet_spend(w, ea, t->offer_a.gold); wallet_add(w, eb, t->offer_a.gold); }
    if (t->offer_b.gold > 0.0f) { wallet_spend(w, eb, t->offer_b.gold); wallet_add(w, ea, t->offer_b.gold); }
    t->open = false;
    return true;
}

// ---------------- leaderboard ----------------
inline void submit_score(SocialState& s, const std::string& name, float score) {
    bool found = false;
    for (auto& e : s.leaderboard)
        if (e.name == name) { if (score > e.score) e.score = score; found = true; break; }
    if (!found) s.leaderboard.push_back({name, score});
    std::sort(s.leaderboard.begin(), s.leaderboard.end(),
              [](const LeaderboardEntry& x, const LeaderboardEntry& y) { return x.score > y.score; });
}
inline int rank_of(SocialState& s, const std::string& name) {
    for (size_t i = 0; i < s.leaderboard.size(); ++i) if (s.leaderboard[i].name == name) return static_cast<int>(i) + 1;
    return 0;   // not present
}
inline std::vector<LeaderboardEntry> top_scores(SocialState& s, int n) {
    std::vector<LeaderboardEntry> r;
    for (int i = 0; i < n && i < static_cast<int>(s.leaderboard.size()); ++i) r.push_back(s.leaderboard[i]);
    return r;
}

}  // namespace schizo::ecs
