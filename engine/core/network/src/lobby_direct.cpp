// Lobby codes without a lobby server (lobby_direct.h): STUN for the public
// address, the BitTorrent DHT (BEP 5) to find each other, a broadcast on the
// local network, and UDP hole punching. ENet's portable sockets only.

#include "lobby_direct.h"
#include "lobby_client.h"     // parse_endpoint
#include "lobby_protocol.h"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>

namespace engine::network::lobby {
namespace {

// ---- SHA-1 (the DHT's keys are SHA-1 sized; nothing secret rides on it) ------
Key sha1_bytes(const uint8_t* data, size_t len) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(data, data + len);
    const uint64_t bits = static_cast<uint64_t>(len) * 8u;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(m[off + 4 * i]) << 24 | uint32_t(m[off + 4 * i + 1]) << 16 |
                   uint32_t(m[off + 4 * i + 2]) << 8 | uint32_t(m[off + 4 * i + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    Key out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[static_cast<size_t>(i * 4 + j)] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

// ---- bencode (the DHT's messages), read with care: they come from anyone ------
struct BVal {
    enum class T { Int, Str, List, Dict } t = T::Str;
    int64_t i = 0;
    std::string s;
    std::vector<BVal> l;
    std::vector<std::pair<std::string, BVal>> d;
    const BVal* get(const char* k) const {
        if (t != T::Dict) return nullptr;
        for (const auto& kv : d) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const std::string* str(const char* k) const {
        const BVal* v = get(k);
        return v && v->t == T::Str ? &v->s : nullptr;
    }
};
bool bdecode(const char*& p, const char* e, BVal& out, int depth = 0) {
    if (p >= e || depth > 6) return false;
    if (*p == 'i') {
        ++p;
        const bool neg = p < e && *p == '-';
        if (neg) ++p;
        int64_t v = 0;
        int n = 0;
        while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; if (++n > 18) return false; }
        if (p >= e || *p != 'e' || n == 0) return false;
        ++p;
        out.t = BVal::T::Int;
        out.i = neg ? -v : v;
        return true;
    }
    if (*p == 'l' || *p == 'd') {
        const bool dict = *p == 'd';
        ++p;
        out.t = dict ? BVal::T::Dict : BVal::T::List;
        while (p < e && *p != 'e') {
            if (out.l.size() + out.d.size() >= 256) return false;
            if (dict) {
                BVal k, v;
                if (!bdecode(p, e, k, depth + 1) || k.t != BVal::T::Str || !bdecode(p, e, v, depth + 1)) return false;
                out.d.emplace_back(std::move(k.s), std::move(v));
            } else {
                BVal v;
                if (!bdecode(p, e, v, depth + 1)) return false;
                out.l.push_back(std::move(v));
            }
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    if (*p >= '0' && *p <= '9') {
        size_t len = 0;
        int n = 0;
        while (p < e && *p >= '0' && *p <= '9') { len = len * 10 + static_cast<size_t>(*p - '0'); ++p; if (++n > 5) return false; }
        if (p >= e || *p != ':') return false;
        ++p;
        if (static_cast<size_t>(e - p) < len) return false;
        out.t = BVal::T::Str;
        out.s.assign(p, len);
        p += len;
        return true;
    }
    return false;
}
std::string bstr(const std::string& s) { return std::to_string(s.size()) + ":" + s; }
std::string bstr(const Key& k) { return "20:" + std::string(reinterpret_cast<const char*>(k.data()), k.size()); }

// ---- addresses and sockets ------------------------------------------------------
struct Addr {
    uint32_t host = 0;   // network byte order, as ENet keeps it
    uint16_t port = 0;
    uint64_t id() const { return (static_cast<uint64_t>(host) << 16) | port; }
    bool operator==(const Addr& o) const { return host == o.host && port == o.port; }
};
std::string ip_of(const Addr& a) {
    ENetAddress e{};
    e.host = a.host;
    char b[64] = "";
    enet_address_get_host_ip(&e, b, sizeof b);
    return b;
}
bool addr_of(const std::string& ip, uint16_t port, Addr& out) {
    ENetAddress e{};
    if (enet_address_set_host_ip(&e, ip.c_str()) != 0) return false;
    out.host = e.host;
    out.port = port;
    return true;
}
const uint8_t* bytes_of(const uint32_t& host) { return reinterpret_cast<const uint8_t*>(&host); }
// Not somewhere on the internet: a DHT node there is a lie or a mistake.
bool unroutable(uint32_t host) {
    const uint8_t* b = bytes_of(host);
    return b[0] == 0 || b[0] == 10 || b[0] == 127 || (b[0] == 192 && b[1] == 168) || (b[0] == 172 && (b[1] & 0xF0) == 16) ||
           (b[0] == 169 && b[1] == 254) || (b[0] == 100 && (b[1] & 0xC0) == 64) || b[0] >= 224;
}
// Names to addresses (DNS: blocking, so it runs off the frame).
std::vector<Addr> resolve_list(const std::vector<std::string>& list) {
    std::vector<Addr> out;
    for (const auto& item : list) {
        Endpoint ep;
        if (!parse_endpoint(item, ep)) continue;
        ENetAddress e{};
        if (enet_address_set_host(&e, ep.host.c_str()) != 0) continue;
        out.push_back(Addr{e.host, ep.port});
    }
    return out;
}

// ENet's socket layer wants enet_initialize (Winsock on Windows); counted.
struct EnetUse {
    bool ok = enet_initialize() == 0;
    ~EnetUse() { if (ok) enet_deinitialize(); }
};

struct Udp {
    ENetSocket s = ENET_SOCKET_NULL;
    Udp() = default;
    Udp(const Udp&) = delete;
    Udp& operator=(const Udp&) = delete;
    Udp(Udp&& o) noexcept : s(o.s) { o.s = ENET_SOCKET_NULL; }
    Udp& operator=(Udp&& o) noexcept {
        if (this != &o) { close(); s = o.s; o.s = ENET_SOCKET_NULL; }
        return *this;
    }
    bool open(uint16_t port, bool broadcast, bool reuse) {
        close();
        s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (s == ENET_SOCKET_NULL) return false;
        enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
        if (broadcast) enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
        if (reuse) enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
        ENetAddress a{};
        a.host = ENET_HOST_ANY;
        a.port = port;
        if (enet_socket_bind(s, &a) < 0) { close(); return false; }
        return true;
    }
    bool is_open() const { return s != ENET_SOCKET_NULL; }
    void close() {
        if (s != ENET_SOCKET_NULL) enet_socket_destroy(s);
        s = ENET_SOCKET_NULL;
    }
    ~Udp() { close(); }
    void send(const Addr& to, const std::string& d) {
        if (!is_open()) return;
        ENetAddress a{};
        a.host = to.host;
        a.port = to.port;
        ENetBuffer b;
        b.data = const_cast<char*>(d.data());
        b.dataLength = d.size();
        enet_socket_send(s, &a, &b, 1);
    }
    int recv(Addr& from, char* buf, size_t cap) {
        if (!is_open()) return -1;
        ENetAddress a{};
        ENetBuffer b;
        b.data = buf;
        b.dataLength = cap;
        const int n = enet_socket_receive(s, &a, &b, 1);
        if (n > 0) { from.host = a.host; from.port = a.port; }
        return n;
    }
};

uint32_t broadcast_host() {
    ENetAddress e{};
    e.host = ENET_HOST_BROADCAST;
    return e.host;
}
uint32_t loopback_host() {
    Addr a;
    addr_of("127.0.0.1", 0, a);
    return a.host;
}

// ---- the DHT (BEP 5): get_peers and announce_peer, as a client -------------------
bool closer(const Key& target, const Key& a, const Key& b) {
    for (size_t i = 0; i < target.size(); ++i) {
        const uint8_t x = a[i] ^ target[i], y = b[i] ^ target[i];
        if (x != y) return x < y;
    }
    return false;
}

class Dht {
public:
    bool open(std::mt19937& rng) {
        for (auto& b : id_) b = static_cast<uint8_t>(rng());
        return sock_.open(0, false, false);
    }
    void bootstrap(const std::vector<Addr>& nodes, double now) {
        boot_ = nodes;
        for (auto& s : searches_) {
            if (s.done && s.c.empty()) { s.done = false; s.started = now; }
            seed(s);
        }
    }
    /// Look `key` up again: what is under it comes in through values(); with
    /// `port` >= 0 this computer's address goes under it once the closest nodes
    /// are found; `poll`: keep asking those nodes for what is under it.
    void search(const Key& key, int port, bool poll, double now) {
        Search* s = find(key);
        if (!s) {
            searches_.push_back(Search{});
            s = &searches_.back();
            s->key = key;
        } else {
            // Start again from the closest nodes it knows, and the entry points.
            std::vector<Cand> keep;
            for (const auto& c : s->c)
                if (c.has_id && c.replied && keep.size() < 16) {
                    Cand k = c;
                    k.queried = k.replied = k.failed = false;
                    k.token.clear();
                    keep.push_back(k);
                }
            s->c = keep;
        }
        s->port = port;
        s->poll = poll;
        s->done = false;
        s->started = now;
        s->queries = 0;
        seed(*s);
    }
    const std::vector<Addr>& values(const Key& key) {
        static const std::vector<Addr> none;
        const Search* s = find(key);
        return s ? s->values : none;
    }
    /// The values worth using. Some DHT nodes answer every lookup with
    /// addresses of their own choosing (junk, or someone else's computer): a
    /// value counts only when nodes on at least two different networks (/24)
    /// return it -- a real announcement is kept by the eight nodes nearest the
    /// key. At most `cap`, the best-confirmed first.
    std::vector<Addr> trusted(const Key& key, size_t cap = 8) {
        std::vector<std::pair<size_t, Addr>> ranked;
        if (Search* s = find(key))
            for (const auto& v : s->values) {
                std::set<uint32_t> nets;
                for (const auto& w : s->said[v.id()])
                    // A node whose id matches the key in more than 48 leading bits made
                    // its id up to look nearest (real ones share about 20): not a witness.
                    if (w.second <= 48) nets.insert(w.first & 0x00FFFFFFu);   // network order: a.b.c of a.b.c.d
                if (nets.size() >= 2) ranked.emplace_back(nets.size(), v);
            }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<Addr> out;
        for (const auto& r : ranked) if (out.size() < cap) out.push_back(r.second);
        return out;
    }
    bool done(const Key& key) { const Search* s = find(key); return s && s->done; }
    int replies(const Key& key) {
        const Search* s = find(key);
        int n = 0;
        if (s) for (const auto& c : s->c) n += c.replied ? 1 : 0;
        return n;
    }
    int announced(const Key& key) { const Search* s = find(key); return s ? s->announced : 0; }

    void update(double now) {
        char buf[2048];
        Addr from;
        int n;
        int budget = 400;
        while (budget-- > 0 && (n = sock_.recv(from, buf, sizeof buf)) > 0) handle(from, buf, static_cast<size_t>(n));
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->second.sent > 2.0) {
                if (Cand* c = cand(it->second.search, it->second.to)) c->failed = true;
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        for (size_t i = 0; i < searches_.size(); ++i) {
            Search& s = searches_[i];
            if (!s.done) step(static_cast<int>(i), now);
            else if (s.poll && now >= s.next_poll) {
                s.next_poll = now + 1.5;
                int asked = 0;
                for (auto& c : s.c)
                    if (c.has_id && c.replied) {
                        query(static_cast<int>(i), c.a, 2, now);
                        if (++asked == 8) break;
                    }
                if (asked == 0) search(s.key, s.port, s.poll, now);   // nobody to ask: look again
            }
        }
    }

private:
    struct Cand {
        Addr a;
        Key nid{};
        bool has_id = false, queried = false, replied = false, failed = false;
        std::string token;
    };
    struct Search {
        Key key{};
        std::vector<Cand> c;
        bool done = false, poll = false;
        double started = 0, next_poll = 0;
        int queries = 0, port = -1, announced = 0;
        std::set<uint64_t> seen;
        std::vector<Addr> values;
        // Who said so: for each value, the nodes that returned it (their address
        // and how many leading bits their id shares with the key).
        std::map<uint64_t, std::vector<std::pair<uint32_t, int>>> said;
    };
    struct Pending { int search = 0; Addr to; double sent = 0; int kind = 0; };   // 0 lookup, 1 announce, 2 poll

    Search* find(const Key& key) {
        for (auto& s : searches_) if (s.key == key) return &s;
        return nullptr;
    }
    Cand* cand(int si, const Addr& a) {
        if (si < 0 || static_cast<size_t>(si) >= searches_.size()) return nullptr;
        for (auto& c : searches_[static_cast<size_t>(si)].c) if (c.a == a) return &c;
        return nullptr;
    }
    void seed(Search& s) {
        for (const auto& b : boot_) {
            bool have = false;
            for (const auto& c : s.c) have |= c.a == b;
            if (!have) { Cand c; c.a = b; s.c.push_back(c); }
        }
    }
    void add(Search& s, const Addr& a, const Key& nid) {
        if (a.port == 0 || unroutable(a.host) || s.c.size() >= 600) return;
        for (auto& c : s.c)
            if (c.a == a) {
                if (!c.has_id) { c.nid = nid; c.has_id = true; }
                return;
            }
        Cand c;
        c.a = a;
        c.nid = nid;
        c.has_id = true;
        s.c.push_back(c);
    }
    std::string tid() {
        const uint16_t t = next_tid_++;
        return std::string{static_cast<char>(t >> 8), static_cast<char>(t & 0xFF)};
    }
    void query(int si, const Addr& to, int kind, double now) {
        const Search& s = searches_[static_cast<size_t>(si)];
        const std::string t = tid();
        const std::string msg = "d1:ad2:id" + bstr(id_) + "9:info_hash" + bstr(s.key) + "e1:q9:get_peers1:t" + bstr(t) + "1:y1:qe";
        sock_.send(to, msg);
        pending_[static_cast<uint16_t>((uint8_t(t[0]) << 8) | uint8_t(t[1]))] = Pending{si, to, now, kind};
    }
    void announce(int si, const Cand& c, double now) {
        const Search& s = searches_[static_cast<size_t>(si)];
        const std::string t = tid();
        const std::string msg = "d1:ad2:id" + bstr(id_) + "12:implied_porti0e9:info_hash" + bstr(s.key) + "4:porti" +
                                std::to_string(s.port) + "e5:token" + bstr(c.token) + "e1:q13:announce_peer1:t" + bstr(t) + "1:y1:qe";
        sock_.send(c.a, msg);
        pending_[static_cast<uint16_t>((uint8_t(t[0]) << 8) | uint8_t(t[1]))] = Pending{si, c.a, now, 1};
    }
    void handle(const Addr& from, const char* data, size_t size) {
        BVal msg;
        const char* p = data;
        if (!bdecode(p, data + size, msg) || msg.t != BVal::T::Dict) return;
        const std::string* t = msg.str("t");
        const std::string* y = msg.str("y");
        if (!t || !y || t->size() != 2) return;
        const uint16_t key = static_cast<uint16_t>((uint8_t((*t)[0]) << 8) | uint8_t((*t)[1]));
        auto it = pending_.find(key);
        if (it == pending_.end() || !(it->second.to == from)) return;
        const Pending pend = it->second;
        pending_.erase(it);
        Search& s = searches_[static_cast<size_t>(pend.search)];
        Cand* c = cand(pend.search, from);
        if (*y != "r") { if (c) c->failed = true; return; }
        const BVal* r = msg.get("r");
        if (!r || r->t != BVal::T::Dict) return;
        if (pend.kind == 1) { ++s.announced; return; }
        if (c) {
            c->replied = true;
            if (const std::string* nid = r->str("id"); nid && nid->size() == 20) {
                std::memcpy(c->nid.data(), nid->data(), 20);
                c->has_id = true;
            }
            if (const std::string* tok = r->str("token"); tok && tok->size() <= 64) c->token = *tok;
        }
        if (const std::string* nodes = r->str("nodes"))
            for (size_t i = 0; i + 26 <= nodes->size(); i += 26) {
                Key nid{};
                std::memcpy(nid.data(), nodes->data() + i, 20);
                Addr a;
                std::memcpy(&a.host, nodes->data() + i + 20, 4);
                a.port = static_cast<uint16_t>((uint8_t((*nodes)[i + 24]) << 8) | uint8_t((*nodes)[i + 25]));
                add(s, a, nid);
            }
        if (const BVal* vals = r->get("values"); vals && vals->t == BVal::T::List)
            for (const auto& v : vals->l) {
                if (v.t != BVal::T::Str || v.s.size() != 6 || s.values.size() >= 64) continue;
                Addr a;
                std::memcpy(&a.host, v.s.data(), 4);
                a.port = static_cast<uint16_t>((uint8_t(v.s[4]) << 8) | uint8_t(v.s[5]));
                if (a.port == 0 || a.host == 0) continue;
                if (s.seen.insert(a.id()).second) s.values.push_back(a);
                int bits = 0;
                if (c && c->has_id)
                    for (size_t b = 0; b < 160; ++b) {
                        if (((c->nid[b / 8] ^ s.key[b / 8]) >> (7 - b % 8)) & 1) break;
                        ++bits;
                    }
                auto& who = s.said[a.id()];
                bool dup = false;
                for (const auto& w : who) dup |= w.first == from.host;
                if (!dup && who.size() < 32) who.emplace_back(from.host, bits);
            }
    }

public:
    std::string dump(const Key& key) {
        Search* s = find(key);
        if (!s) return "(no search)\n";
        std::string out;
        int replied = 0;
        for (const auto& c : s->c) replied += c.replied ? 1 : 0;
        out += "replied " + std::to_string(replied) + ", values " + std::to_string(s->values.size()) + "\n";
        for (const auto& v : s->values) {
            const auto& who = s->said[v.id()];
            std::set<uint32_t> nets;
            int best = 0;
            for (const auto& w : who) { nets.insert(w.first & 0x00FFFFFFu); best = std::max(best, w.second); }
            out += "  " + ip_of(v) + ":" + std::to_string(v.port) + "  by " + std::to_string(who.size()) + " node(s), " +
                   std::to_string(nets.size()) + " /24s, closest id " + std::to_string(best) + " bits\n";
        }
        return out;
    }
private:
    void step(int si, double now) {
        Search& s = searches_[static_cast<size_t>(si)];
        if (s.c.empty()) {
            if (now - s.started > 15.0) { s.done = true; s.next_poll = now + 1.5; }
            return;
        }
        const Key target = s.key;
        std::stable_sort(s.c.begin(), s.c.end(), [&](const Cand& a, const Cand& b) {
            if (a.has_id != b.has_id) return a.has_id;
            return a.has_id && closer(target, a.nid, b.nid);
        });
        int inflight = 0;
        for (const auto& [k, pd] : pending_) inflight += (pd.search == si && pd.kind == 0) ? 1 : 0;
        const Cand* kth = nullptr;
        int replied = 0;
        for (const auto& c : s.c)
            if (c.has_id && c.replied && ++replied == 8) { kth = &c; break; }
        for (auto& c : s.c) {
            if (inflight >= 6) break;
            if (c.queried) continue;
            if (kth && (!c.has_id || !closer(target, c.nid, kth->nid))) continue;   // no closer than what answered
            c.queried = true;
            query(si, c.a, 0, now);
            ++inflight;
            ++s.queries;
        }
        if (inflight == 0 || now - s.started > 15.0 || s.queries > 300) {
            s.done = true;
            s.next_poll = now + 1.5;
            if (s.port > 0) {
                int sent = 0;
                for (const auto& c : s.c)
                    if (c.has_id && c.replied && !c.token.empty()) {
                        announce(si, c, now);
                        if (++sent == 8) break;
                    }
            }
        }
    }

    Udp sock_;
    Key id_{};
    std::vector<Addr> boot_;
    std::vector<Search> searches_;
    std::unordered_map<uint16_t, Pending> pending_;
    uint16_t next_tid_ = 1;
};

// ---- STUN, on the game's own socket ----------------------------------------------
struct Stun {
    struct Ask { Addr server; std::array<uint8_t, 12> tid{}; bool answered = false; Addr mapped; double sent = -1; };
    std::vector<Ask> asks;
    double started = -1;
    bool done = false, have = false, symmetric = false;
    Addr mapped;

    void begin(const std::vector<Addr>& servers, std::mt19937& rng, double now) {
        for (const auto& s : servers) {
            Ask a;
            a.server = s;
            for (auto& b : a.tid) b = static_cast<uint8_t>(rng());
            asks.push_back(a);
        }
        started = now;
        if (asks.empty()) done = true;
    }
    // Two servers asked first; the third only when one of them is silent.
    void update(Transport& t, double now) {
        if (done || started < 0) return;
        int answered = 0;
        for (const auto& a : asks) answered += a.answered ? 1 : 0;
        const size_t active = std::min<size_t>(asks.size(), now - started > 1.5 ? 3 : 2);
        for (size_t i = 0; i < active; ++i) {
            Ask& a = asks[i];
            if (a.answered || (a.sent >= 0 && now - a.sent < 0.4)) continue;
            const std::string req = stun_request(a.tid);
            t.send_raw(ip_of(a.server), a.server.port, req.data(), req.size());
            a.sent = now;
        }
        if (answered >= 2 || (answered == 1 && now - started > 3.0) || now - started > 4.0) {
            done = true;
            const Ask* first = nullptr;
            for (const auto& a : asks)
                if (a.answered) {
                    if (!first) first = &a;
                    else if (!(a.mapped == first->mapped)) symmetric = true;
                }
            if (first) { have = true; mapped = first->mapped; }
        }
    }
    bool on_raw(const Addr& from, const void* data, size_t size) {
        if (!is_stun_response(data, size)) return false;
        for (auto& a : asks) {
            if (a.answered || !(a.server.host == from.host)) continue;
            std::string ip;
            uint16_t port = 0;
            if (stun_mapped(data, size, a.tid, ip, port) && addr_of(ip, port, a.mapped)) a.answered = true;
        }
        return true;
    }
};

std::string nonce_of(std::mt19937& rng) {
    static const char* hex = "0123456789abcdef";
    std::string n;
    for (int i = 0; i < 12; ++i) n += hex[rng() & 15];
    return n;
}

}  // namespace

// ---- parts ---------------------------------------------------------------------------
Key sha1(const void* data, size_t size) { return sha1_bytes(static_cast<const uint8_t*>(data), size); }

Key dht_key(const std::string& game_id, const std::string& code, const char* role) {
    const std::string s = std::string("GWSLOBBY1|") + game_id + "|" + code + "|" + role;
    return sha1(s.data(), s.size());
}

std::string stun_request(const std::array<uint8_t, 12>& tid) {
    std::string r = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, static_cast<char>(0xA4), 0x42};
    r.append(reinterpret_cast<const char*>(tid.data()), tid.size());
    return r;
}

bool stun_mapped(const void* data, size_t size, const std::array<uint8_t, 12>& tid, std::string& ip, uint16_t& port) {
    const auto* d = static_cast<const uint8_t*>(data);
    if (!is_stun_response(data, size) || d[1] != 0x01 || std::memcmp(d + 8, tid.data(), 12) != 0) return false;
    const size_t len = std::min<size_t>(size - 20, (size_t(d[2]) << 8) | d[3]);
    bool found = false;
    for (size_t i = 20; i + 4 <= 20 + len;) {
        const uint16_t type = static_cast<uint16_t>((d[i] << 8) | d[i + 1]);
        const uint16_t alen = static_cast<uint16_t>((d[i + 2] << 8) | d[i + 3]);
        const uint8_t* v = d + i + 4;
        if (i + 4 + alen > 20 + len) break;
        if ((type == 0x0020 || (type == 0x0001 && !found)) && alen >= 8 && v[1] == 0x01) {   // IPv4
            uint16_t p = static_cast<uint16_t>((v[2] << 8) | v[3]);
            uint8_t a[4] = {v[4], v[5], v[6], v[7]};
            if (type == 0x0020) {
                p ^= 0x2112;
                const uint8_t cookie[4] = {0x21, 0x12, 0xA4, 0x42};
                for (int k = 0; k < 4; ++k) a[k] ^= cookie[k];
            }
            ip = std::to_string(a[0]) + "." + std::to_string(a[1]) + "." + std::to_string(a[2]) + "." + std::to_string(a[3]);
            port = p;
            found = true;
            if (type == 0x0020) return true;
        }
        i += 4 + ((alen + 3u) & ~3u);
    }
    return found;
}

// ==== the host ===========================================================================
struct DirectHost::Impl {
    Transport* t = nullptr;
    DirectConfig cfg;
    std::string game, title, code, err, reach, note;
    int max_players = 2;
    State st = State::Off;
    double now = 0;
    EnetUse enet;
    std::mt19937 rng{std::random_device{}()};
    Udp lan;
    Dht dht;
    bool dht_ok = false, resolved = false, stun_handled = false;
    std::future<std::pair<std::vector<Addr>, std::vector<Addr>>> resolving;
    Stun stun;
    Key host_key{}, join_key{};
    uint16_t game_port = 0;
    double next_research = 0;
    struct Joiner { Addr a; double first = 0, last = -10; bool answered = false; };
    std::map<uint64_t, Joiner> joiners;

    void on_raw(const std::string& ip, uint16_t port, const uint8_t* data, size_t size) {
        Addr from;
        if (!addr_of(ip, port, from)) return;
        if (stun.on_raw(from, data, size)) return;
        const auto f = parse(data, size);
        if (f.size() < 4 || f[0] != "DJOIN") return;
        auto& j = joiners[from.id()];
        if (j.first == 0) { j.a = from; j.first = now; }
        j.answered = true;
        std::string reply;
        const int players = static_cast<int>(t->peer_count()) + 1;
        if (f[2] != code) reply = make({"DERR", f[2], "unknown"});
        else if (f[1] != game) reply = make({"DERR", f[2], "game"});
        else if (players >= max_players) reply = make({"DERR", f[2], "full"});
        else reply = make({"DSLOT", code, f[3]});
        t->send_raw(ip, port, reply.data(), reply.size());
    }

    void update(float dt) {
        now += dt;
        if (st == State::Off || st == State::Failed) return;
        if (!resolved && resolving.valid() &&
            resolving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto r = resolving.get();
            resolved = true;
            stun.begin(r.first, rng, now);
            if (dht_ok) {
                dht.bootstrap(r.second, now);
                dht.search(join_key, -1, true, now);   // who wants to join: asked again every 1.5 s
            }
        }
        if (resolved) stun.update(*t, now);
        if (!stun_handled && (stun.done || !cfg.internet)) {
            stun_handled = true;
            if (stun.have && dht_ok) {
                dht.search(host_key, stun.mapped.port, false, now);   // this game's address under the code
                next_research = now + 240.0;
                reach = "internet";
                if (stun.symmetric)
                    note = "Your router changes ports for every connection, so friends on other networks may not get "
                           "through (a lobby server would).";
            } else {
                reach = "local";
                note = cfg.internet ? "No internet address found: friends on this network can join, others cannot."
                                    : "";
            }
            st = State::Open;
        }
        if (dht_ok) {
            dht.update(now);
            for (const auto& v : dht.trusted(join_key))
                if (!joiners.count(v.id()) && joiners.size() < 16) joiners[v.id()] = Joiner{v, now, -10, false};
            if (stun.have && now >= next_research) {   // the DHT forgets after a while: say it again
                next_research = now + 240.0;
                dht.search(host_key, stun.mapped.port, false, now);
                dht.search(join_key, -1, true, now);
            }
        }
        // Open the router for each joiner the DHT names, until it asks to join.
        const std::string punch = make({"DPUNCH", code});
        for (auto& [k, j] : joiners) {
            if (j.answered || now - j.first > 30.0) continue;
            if (now - j.last >= (now - j.first < 5.0 ? 0.25 : 1.0)) {
                t->send_raw(ip_of(j.a), j.a.port, punch.data(), punch.size());
                j.last = now;
            }
        }
        // The local network asking who has a code.
        char buf[512];
        Addr from;
        int n;
        while (lan.is_open() && (n = lan.recv(from, buf, sizeof buf)) > 0) {
            const auto f = parse(buf, static_cast<size_t>(n));
            if (f.size() >= 3 && f[0] == "FIND" && f[1] == game && f[2] == code)
                lan.send(from, make({"HERE", code, std::to_string(game_port)}));
        }
    }
};

DirectHost::DirectHost() : impl_(std::make_unique<Impl>()) {}
DirectHost::~DirectHost() { stop(); }

void DirectHost::start(Transport& transport, const std::string& game_id, int max_players, const std::string& title,
                       const DirectConfig& config) {
    stop();
    auto& p = *impl_;
    p.t = &transport;
    p.cfg = config;
    p.game = game_id;
    p.title = title;
    p.max_players = std::max(2, max_players);
    p.code = random_code(p.rng);
    p.host_key = dht_key(game_id, p.code, "host");
    p.join_key = dht_key(game_id, p.code, "join");
    p.game_port = transport.local_port();
    p.st = State::Opening;
    if (!p.enet.ok || p.game_port == 0) {
        p.st = State::Failed;
        p.err = "the game's network socket is not open";
        return;
    }
    transport.set_raw_handler([this](const std::string& ip, uint16_t port, const uint8_t* d, size_t n) {
        impl_->on_raw(ip, port, d, n);
    });
    if (p.cfg.lan) p.lan.open(p.cfg.lan_port, false, true);
    if (p.cfg.internet) {
        p.dht_ok = p.dht.open(p.rng);
        p.resolving = std::async(std::launch::async, [stun = p.cfg.stun_servers, boot = p.cfg.dht_bootstrap] {
            return std::make_pair(resolve_list(stun), resolve_list(boot));
        });
    }
    if (!p.lan.is_open() && !p.cfg.internet) {
        p.st = State::Failed;
        p.err = "could not listen on the local network (UDP port " + std::to_string(p.cfg.lan_port) + ")";
    }
}

void DirectHost::update(float dt) { impl_->update(dt); }

void DirectHost::stop() {
    auto& p = *impl_;
    if (p.st == State::Off) return;
    if (p.t) p.t->set_raw_handler(nullptr);
    p.lan.close();
    p.dht = Dht{};
    if (p.resolving.valid()) p.resolving.wait();
    p.resolving = {};
    p.stun = Stun{};
    p.joiners.clear();
    p.dht_ok = p.resolved = p.stun_handled = false;
    p.reach.clear();
    p.note.clear();
    p.st = State::Off;
    p.t = nullptr;
}

DirectHost::State DirectHost::state() const { return impl_->st; }
const std::string& DirectHost::code() const { return impl_->code; }
const std::string& DirectHost::error() const { return impl_->err; }
const std::string& DirectHost::reach() const { return impl_->reach; }
const std::string& DirectHost::note() const { return impl_->note; }
int DirectHost::joiners_seen() const { return static_cast<int>(impl_->joiners.size()); }
std::string DirectHost::debug() const {
    auto& p = *impl_;
    return "public " + (p.stun.have ? ip_of(p.stun.mapped) + ":" + std::to_string(p.stun.mapped.port) : std::string("?")) +
           "\n[host key] " + p.dht.dump(p.host_key) + "[join key] " + p.dht.dump(p.join_key);
}

// ==== the joiner ===========================================================================
struct DirectJoin::Impl {
    Transport* t = nullptr;
    DirectConfig cfg;
    std::string game, code, nonce, host, via, err, progress;
    uint16_t port = 0;
    State st = State::Off;
    double now = 0, last_find = -10, announced_at = -100;
    EnetUse enet;
    std::mt19937 rng{std::random_device{}()};
    Udp lan;
    Dht dht;
    bool dht_ok = false, resolved = false;
    std::future<std::pair<std::vector<Addr>, std::vector<Addr>>> resolving;
    Stun stun;
    Key host_key{}, join_key{};
    struct Cand { Addr a; bool local = false; double last = -10, first = 0; };
    std::map<uint64_t, Cand> cands;
    bool heard_host = false;   // a punch or an answer from a host came in

    void on_raw(const std::string& ip, uint16_t from_port, const uint8_t* data, size_t size) {
        Addr from;
        if (!addr_of(ip, from_port, from)) return;
        if (stun.on_raw(from, data, size)) return;
        if (st != State::Searching) return;
        const auto f = parse(data, size);
        if (f.size() < 2) return;
        if (f[0] == "DPUNCH" && f[1] == code) { heard_host = true; return; }
        if (f[0] == "DSLOT" && f.size() >= 3 && f[1] == code && f[2] == nonce) {
            host = ip;
            port = from_port;
            auto it = cands.find(from.id());
            via = (it != cands.end() && it->second.local) ? "this network" : "the internet";
            st = State::Found;
            return;
        }
        if (f[0] == "DERR" && f.size() >= 3 && f[1] == code && cands.count(from.id())) {
            err = explain_error(f[2]);
            st = State::Failed;
        }
    }

    void update(float dt) {
        now += dt;
        if (st != State::Searching) return;
        if (!resolved && resolving.valid() &&
            resolving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto r = resolving.get();
            resolved = true;
            stun.begin(r.first, rng, now);
            if (dht_ok) {
                dht.bootstrap(r.second, now);
                dht.search(host_key, -1, true, now);   // the host's address, asked for until found
            }
        }
        if (resolved) stun.update(*t, now);
        // Our own address under the code's "join" key, so the host opens its router for us.
        if (dht_ok && stun.have && now - announced_at > 20.0) {
            announced_at = now;
            dht.search(join_key, stun.mapped.port, false, now);
        }
        if (dht_ok) {
            dht.update(now);
            for (const auto& v : dht.trusted(host_key))
                if (!cands.count(v.id()) && cands.size() < 16) cands[v.id()] = Cand{v, false, -10, now};
        }
        // Who has this code on the local network (and this PC)?
        if (lan.is_open()) {
            if (now - last_find >= 0.5) {
                last_find = now;
                const std::string find = make({"FIND", game, code});
                lan.send(Addr{broadcast_host(), cfg.lan_port}, find);
                lan.send(Addr{loopback_host(), cfg.lan_port}, find);
            }
            char buf[512];
            Addr from;
            int n;
            while ((n = lan.recv(from, buf, sizeof buf)) > 0) {
                const auto f = parse(buf, static_cast<size_t>(n));
                if (f.size() >= 3 && f[0] == "HERE" && f[1] == code) {
                    const int gp = std::atoi(f[2].c_str());
                    if (gp <= 0 || gp > 65535) continue;
                    const Addr a{from.host, static_cast<uint16_t>(gp)};
                    if (!cands.count(a.id())) cands[a.id()] = Cand{a, true, -10, now};
                    else cands[a.id()].local = true;
                }
            }
        }
        // Ask every host found to let us in, from the game's own socket.
        const std::string ask = make({"DJOIN", game, code, nonce});
        for (auto& [k, c] : cands)
            if (now - c.last >= (c.local ? 0.3 : 0.5) && now - c.first < 20.0) {
                t->send_raw(ip_of(c.a), c.a.port, ask.data(), ask.size());
                c.last = now;
            }
        progress = cands.empty() ? "Looking for game " + code + "..." : "Found game " + code + " -- connecting...";
        if (now > cfg.join_timeout) {
            st = State::Failed;
            // Both behind this same router (the host's public address is ours):
            // the local network should have found it -- a firewall is in the way.
            bool same_router = stun.have && !cands.empty();
            for (const auto& [k, c] : cands) same_router &= !c.local && c.a.host == stun.mapped.host;
            if (same_router)
                err = "Found game " + code + " on this same internet connection, but the local network did not "
                      "answer: let the game through the firewall on the host's computer (Windows asks the first "
                      "time it hosts), or join with the host's local address under Direct connection.";
            else if (!cands.empty() || heard_host)
                err = "Found game " + code + " but could not reach it: one of the two networks blocks direct "
                      "connections (some mobile and carrier networks do). A lobby server gets through those.";
            else if (cfg.internet && !stun.have)
                err = "No game " + code + " on this network, and no internet connection was found to look further.";
            else
                err = "No game with the code " + code + " was found. Check the code -- and that the host's game is still open.";
        }
    }
};

DirectJoin::DirectJoin() : impl_(std::make_unique<Impl>()) {}
DirectJoin::~DirectJoin() { stop(); }

void DirectJoin::start(Transport& transport, const std::string& game_id, const std::string& code,
                       const DirectConfig& config) {
    stop();
    auto& p = *impl_;
    p.t = &transport;
    p.cfg = config;
    p.game = game_id;
    p.code = code;
    p.nonce = nonce_of(p.rng);
    p.host_key = dht_key(game_id, code, "host");
    p.join_key = dht_key(game_id, code, "join");
    p.st = State::Searching;
    p.now = 0;
    if (!p.enet.ok) {
        p.st = State::Failed;
        p.err = "The network could not be started.";
        return;
    }
    transport.set_raw_handler([this](const std::string& ip, uint16_t port, const uint8_t* d, size_t n) {
        impl_->on_raw(ip, port, d, n);
    });
    if (p.cfg.lan) p.lan.open(0, true, false);
    if (p.cfg.internet) {
        p.dht_ok = p.dht.open(p.rng);
        p.resolving = std::async(std::launch::async, [stun = p.cfg.stun_servers, boot = p.cfg.dht_bootstrap] {
            return std::make_pair(resolve_list(stun), resolve_list(boot));
        });
    }
    p.progress = "Looking for game " + code + "...";
}

void DirectJoin::update(float dt) { impl_->update(dt); }

void DirectJoin::stop() {
    auto& p = *impl_;
    if (p.st == State::Off) return;
    if (p.t) p.t->set_raw_handler(nullptr);
    p.lan.close();
    p.dht = Dht{};
    if (p.resolving.valid()) p.resolving.wait();
    p.resolving = {};
    p.stun = Stun{};
    p.cands.clear();
    p.dht_ok = p.resolved = p.heard_host = false;
    p.st = State::Off;
    p.t = nullptr;
}

DirectJoin::State DirectJoin::state() const { return impl_->st; }
const std::string& DirectJoin::host() const { return impl_->host; }
uint16_t DirectJoin::port() const { return impl_->port; }
const std::string& DirectJoin::via() const { return impl_->via; }
const std::string& DirectJoin::error() const { return impl_->err; }
const std::string& DirectJoin::progress() const { return impl_->progress; }
std::string DirectJoin::debug() const {
    auto& p = *impl_;
    return "public " + (p.stun.have ? ip_of(p.stun.mapped) + ":" + std::to_string(p.stun.mapped.port) : std::string("?")) +
           "\n[host key] " + p.dht.dump(p.host_key) + "[join key] " + p.dht.dump(p.join_key);
}

}  // namespace engine::network::lobby
