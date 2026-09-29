// =============================================================================
//  StormDB 2.0 — embedded geospatial time-series database for StormSense AI
// =============================================================================
//  A single-file, dependency-free C++17 storage engine for IoT weather
//  telemetry and geo-fenced alert subscriptions.
//
//  ENGINE FEATURES
//    * Write-ahead log (WAL): every commit is one atomic, CRC32-protected
//      transaction frame, flushed with fsync (durable across power loss).
//    * Crash recovery: on open the log is replayed; a torn or corrupt tail is
//      detected by checksum and truncated, so the database is always consistent.
//    * Multi-statement transactions: begin / commit / rollback.
//    * Exclusive file lock (flock) so two processes cannot corrupt one file.
//    * Reader/writer concurrency (std::shared_mutex): many readers, one writer.
//    * Indexes: 1-degree spatial grid (radius / box / nearest-k), ordered time
//      index (range / retention purge), per-station index (latest / history).
//    * Log compaction (checkpoint) that atomically rewrites live data only.
//    * Validation of every record (coordinate, pressure, wind, humidity ranges).
//    * Analytics: windowed aggregates (count / avg / min / max).
//    * CSV bulk import (all-or-nothing) and CSV export, JSON or table output.
//    * Integrity checker and a built-in self-test suite (`stormdb selftest`).
//
//  All seeded/demo data is DEMO SIMULATION data, not real measurements.
//
//  BUILD   g++ -std=c++17 -O2 -Wall -Wextra -pthread -o stormdb stormdb.cpp
//  USAGE   stormdb [--json] [--no-sync] <db-file> [command ...]     (no command = shell)
//          stormdb selftest
// =============================================================================
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace stormdb {

using i64 = std::int64_t;
using u64 = std::uint64_t;

class DbError : public std::runtime_error { using std::runtime_error::runtime_error; };

// ----------------------------------------------------------------------------
//  Utilities
// ----------------------------------------------------------------------------
inline std::uint32_t crc32(const std::string& s, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (unsigned char ch : s) crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline double haversineKm(double a1, double o1, double a2, double o2) {
    const double p = M_PI / 180.0, d = std::sin((a2 - a1) * p / 2), e = std::sin((o2 - o1) * p / 2);
    const double h = d * d + std::cos(a1 * p) * std::cos(a2 * p) * e * e;
    return 12742.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

inline std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '|': o += "\\p"; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    return o;
}
inline std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char n = s[++i];
        o += n == 'p' ? '|' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return o;
}
inline std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : line) {
        if (c == '|') { v.push_back(cur); cur.clear(); } else cur += c;
    }
    v.push_back(cur);
    return v;
}
inline std::string fmt(double v) { char b[40]; std::snprintf(b, sizeof b, "%.10g", v); return b; }
inline std::string hex8(std::uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }
inline i64 nowTs() { return static_cast<i64>(std::time(nullptr)); }
inline std::string isoTime(i64 ts) {
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

// ----------------------------------------------------------------------------
//  Records and validation
// ----------------------------------------------------------------------------
struct Telemetry {
    u64 id = 0;
    std::string station;
    i64 ts = 0;
    double lat = 0, lon = 0, pressure = 0, wind = 0, humidity = 0;
};
struct Subscription {
    u64 id = 0;
    std::string name, audience;
    double lat = 0, lon = 0, radius_km = 0;
    i64 created = 0;
};

inline void validate(const Telemetry& t) {
    if (t.station.empty() || t.station.size() > 64) throw DbError("station must be 1-64 characters");
    if (!(t.lat >= -90 && t.lat <= 90)) throw DbError("latitude out of range [-90, 90]");
    if (!(t.lon >= -180 && t.lon <= 180)) throw DbError("longitude out of range [-180, 180]");
    if (!(t.pressure >= 800 && t.pressure <= 1100)) throw DbError("pressure out of range [800, 1100] hPa");
    if (!(t.wind >= 0 && t.wind <= 500)) throw DbError("wind out of range [0, 500] km/h");
    if (!(t.humidity >= 0 && t.humidity <= 100)) throw DbError("humidity out of range [0, 100] %");
}
inline void validate(const Subscription& s) {
    if (s.name.empty() || s.name.size() > 128) throw DbError("name must be 1-128 characters");
    if (s.audience != "farmer" && s.audience != "municipal" && s.audience != "responder")
        throw DbError("audience must be farmer, municipal or responder");
    if (!(s.lat >= -90 && s.lat <= 90) || !(s.lon >= -180 && s.lon <= 180)) throw DbError("coordinates out of range");
    if (!(s.radius_km > 0 && s.radius_km <= 2000)) throw DbError("radius must be in (0, 2000] km");
}

// ----------------------------------------------------------------------------
//  Log operations (the unit of replication inside a transaction)
// ----------------------------------------------------------------------------
struct Op {
    enum Kind { PutT, PutS, DelT, DelS } kind = PutT;
    Telemetry t;
    Subscription s;
    u64 id = 0;
};

inline std::string encodeOp(const Op& o) {
    std::ostringstream x;
    switch (o.kind) {
        case Op::PutT:
            x << "PT|" << o.t.id << '|' << esc(o.t.station) << '|' << o.t.ts << '|' << fmt(o.t.lat) << '|' << fmt(o.t.lon)
              << '|' << fmt(o.t.pressure) << '|' << fmt(o.t.wind) << '|' << fmt(o.t.humidity);
            break;
        case Op::PutS:
            x << "PS|" << o.s.id << '|' << esc(o.s.name) << '|' << esc(o.s.audience) << '|' << fmt(o.s.lat) << '|'
              << fmt(o.s.lon) << '|' << fmt(o.s.radius_km) << '|' << o.s.created;
            break;
        case Op::DelT: x << "DT|" << o.id; break;
        case Op::DelS: x << "DS|" << o.id; break;
    }
    return x.str();
}

inline Op decodeOp(const std::string& line) {
    auto f = splitFields(line);
    Op o;
    if (f[0] == "PT" && f.size() == 9) {
        o.kind = Op::PutT;
        o.t = {std::stoull(f[1]), unesc(f[2]), std::stoll(f[3]), std::stod(f[4]), std::stod(f[5]),
               std::stod(f[6]), std::stod(f[7]), std::stod(f[8])};
    } else if (f[0] == "PS" && f.size() == 8) {
        o.kind = Op::PutS;
        o.s = {std::stoull(f[1]), unesc(f[2]), unesc(f[3]), std::stod(f[4]), std::stod(f[5]), std::stod(f[6]), std::stoll(f[7])};
    } else if (f[0] == "DT" && f.size() == 2) {
        o.kind = Op::DelT; o.id = std::stoull(f[1]);
    } else if (f[0] == "DS" && f.size() == 2) {
        o.kind = Op::DelS; o.id = std::stoull(f[1]);
    } else {
        throw DbError("malformed log record");
    }
    return o;
}

// ----------------------------------------------------------------------------
//  Portable OS layer (POSIX and Windows) — the only platform-specific code
// ----------------------------------------------------------------------------
namespace sys {
enum OpenResult { kOpenFailed = -1, kLocked = -2 };

#if defined(_WIN32)
// Opens read/write/append with an exclusive share mode (a second open fails = "locked").
inline int openLocked(const std::string& p, bool truncate) {
    int fd = -1;
    const int flags = _O_RDWR | _O_CREAT | _O_APPEND | _O_BINARY | (truncate ? _O_TRUNC : 0);
    const errno_t e = _sopen_s(&fd, p.c_str(), flags, _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (e != 0) return e == EACCES ? kLocked : kOpenFailed;
    return fd;
}
inline void closeFd(int fd) { _close(fd); }
inline bool writeAll(int fd, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        const int n = _write(fd, d.data() + off, static_cast<unsigned>(std::min<size_t>(d.size() - off, 1u << 20)));
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}
inline bool fsyncFd(int fd) { return _commit(fd) == 0; }
inline bool truncateFd(int fd, size_t n) { return _chsize_s(fd, static_cast<long long>(n)) == 0; }
inline size_t sizeFd(int fd) { const long long e = _lseeki64(fd, 0, SEEK_END); return e < 0 ? 0 : static_cast<size_t>(e); }
inline bool readAllFd(int fd, std::string& out) {
    out.assign(sizeFd(fd), '\0');
    if (_lseeki64(fd, 0, SEEK_SET) < 0) return false;
    size_t off = 0;
    while (off < out.size()) {
        const int n = _read(fd, &out[off], static_cast<unsigned>(std::min<size_t>(out.size() - off, 1u << 20)));
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}
inline bool replaceFile(const std::string& from, const std::string& to) {
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
inline void fsyncDir(const std::string&) {}
inline bool isTty() { return _isatty(_fileno(stdin)) != 0; }
#else
inline int openLocked(const std::string& p, bool truncate) {
    const int fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | (truncate ? O_TRUNC : 0), 0644);
    if (fd < 0) return kOpenFailed;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) { ::close(fd); return kLocked; }
    return fd;
}
inline void closeFd(int fd) { ::close(fd); }
inline bool writeAll(int fd, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        const ssize_t n = ::write(fd, d.data() + off, d.size() - off);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        off += static_cast<size_t>(n);
    }
    return true;
}
inline bool fsyncFd(int fd) { return ::fsync(fd) == 0; }
inline bool truncateFd(int fd, size_t n) { return ::ftruncate(fd, static_cast<off_t>(n)) == 0; }
inline size_t sizeFd(int fd) {
    struct stat st{};
    return ::fstat(fd, &st) == 0 ? static_cast<size_t>(st.st_size) : 0;
}
inline bool readAllFd(int fd, std::string& out) {
    out.assign(sizeFd(fd), '\0');
    size_t off = 0;
    while (off < out.size()) {
        const ssize_t n = ::pread(fd, &out[off], out.size() - off, static_cast<off_t>(off));
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}
inline bool replaceFile(const std::string& from, const std::string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }
inline void fsyncDir(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    const int d = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (d >= 0) { ::fsync(d); ::close(d); }
}
inline bool isTty() { return ::isatty(STDIN_FILENO) != 0; }
#endif
}  // namespace sys

// ----------------------------------------------------------------------------
//  Write-ahead log file: exclusive lock, durable appends, tail truncation
// ----------------------------------------------------------------------------
class Wal {
    int fd_ = -1;
    std::string path_;
    bool sync_ = true;

public:
    Wal() = default;
    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;
    ~Wal() { close(); }

    void open(const std::string& path, bool sync, bool truncate = false) {
        close();
        path_ = path; sync_ = sync;
        const int fd = sys::openLocked(path, truncate);
        if (fd == sys::kLocked) throw DbError("database '" + path + "' is locked by another process (or access denied)");
        if (fd < 0) throw DbError("cannot open '" + path + "': " + std::strerror(errno));
        fd_ = fd;
    }
    void close() { if (fd_ >= 0) { sys::closeFd(fd_); fd_ = -1; } }
    size_t size() const { return sys::sizeFd(fd_); }
    std::string readAll() const {
        std::string s;
        if (!sys::readAllFd(fd_, s)) throw DbError("read failed");
        return s;
    }
    void append(const std::string& data) {
        if (!sys::writeAll(fd_, data)) throw DbError(std::string("write failed: ") + std::strerror(errno));
        flush();
    }
    void flush() { if (sync_ && !sys::fsyncFd(fd_)) throw DbError("fsync failed"); }
    void truncateTo(size_t n) {
        if (!sys::truncateFd(fd_, n)) throw DbError("truncate failed");
        flush();
    }
    const std::string& path() const { return path_; }
};

// ----------------------------------------------------------------------------
//  Spatial grid index (1-degree cells, dateline-aware)
// ----------------------------------------------------------------------------
class GridIndex {
    std::unordered_map<int, std::vector<u64>> cells_;
    size_t count_ = 0;

    static int normLon(int lo) { return ((lo + 180) % 360 + 360) % 360 - 180; }
    static int key(int la, int lo) { return (la + 90) * 400 + (normLon(lo) + 180); }
    static int cellLat(double lat) { return static_cast<int>(std::floor(lat)); }
    static int cellLon(double lon) { return static_cast<int>(std::floor(lon)); }

public:
    void insert(u64 id, double lat, double lon) {
        cells_[key(cellLat(lat), cellLon(lon))].push_back(id);
        ++count_;
    }
    void erase(u64 id, double lat, double lon) {
        auto it = cells_.find(key(cellLat(lat), cellLon(lon)));
        if (it == cells_.end()) return;
        auto& v = it->second;
        auto p = std::find(v.begin(), v.end(), id);
        if (p == v.end()) return;
        *p = v.back(); v.pop_back(); --count_;
        if (v.empty()) cells_.erase(it);
    }
    // Visits ids of every point in cells that overlap the bounding box of the search circle.
    template <class F>
    void visit(double lat, double lon, double km, F&& f) const {
        // Exact angular extent of the search circle (great-circle geometry, not a flat approximation).
        const double dlat = km / 111.0;                      // degrees; slightly generous
        const double rad = (km / 6371.0);                    // angular radius in radians
        const double latRad = lat * M_PI / 180.0;
        double dlon = 180.0;                                 // circle contains a pole -> all longitudes
        if (std::abs(latRad) + rad < M_PI / 2) {
            dlon = std::asin(std::min(1.0, std::sin(rad) / std::cos(latRad))) * 180.0 / M_PI + 0.5;
        }
        const int la0 = std::max(-90, cellLat(lat - dlat)), la1 = std::min(90, cellLat(lat + dlat));
        int lo0 = cellLon(lon - dlon), lo1 = cellLon(lon + dlon);
        if (lo1 - lo0 >= 359) { lo0 = -180; lo1 = 179; }
        for (int la = la0; la <= la1; ++la)
            for (int lo = lo0; lo <= lo1; ++lo) {
                auto it = cells_.find(key(la, lo));
                if (it == cells_.end()) continue;
                for (u64 id : it->second) f(id);
            }
    }
    size_t size() const { return count_; }
    size_t cellCount() const { return cells_.size(); }
};

// ----------------------------------------------------------------------------
//  Query result types
// ----------------------------------------------------------------------------
struct Hit { double km; Telemetry t; };
struct Fence { double km; Subscription s; };
struct Agg {
    size_t count = 0;
    double avgP = 0, minP = 0, maxP = 0, avgW = 0, minW = 0, maxW = 0, avgH = 0, minH = 0, maxH = 0;
};
struct Stats {
    size_t telemetry = 0, subscriptions = 0, stations = 0, cells = 0, walBytes = 0, recoveredBytes = 0;
    u64 commits = 0;
    i64 firstTs = 0, lastTs = 0;
};

// ----------------------------------------------------------------------------
//  Database engine
// ----------------------------------------------------------------------------
class Database {
    std::string path_;
    bool sync_;
    Wal wal_;
    mutable std::shared_mutex mu_;

    std::unordered_map<u64, Telemetry> tel_;
    std::unordered_map<u64, Subscription> subs_;
    GridIndex grid_;
    std::set<std::pair<i64, u64>> byTime_;
    std::map<std::string, std::set<std::pair<i64, u64>>> byStation_;

    std::atomic<u64> nextT_{1}, nextS_{1};
    u64 txid_ = 0, commits_ = 0;
    size_t recovered_ = 0;
    bool inTxn_ = false;
    std::vector<Op> pending_;

    static constexpr const char* kHeader = "STORMDB|1\n";

    // --- state mutation (caller holds the unique lock or is single-threaded in ctor)
    void indexT(const Telemetry& t) {
        grid_.insert(t.id, t.lat, t.lon);
        byTime_.insert({t.ts, t.id});
        byStation_[t.station].insert({t.ts, t.id});
    }
    void unindexT(const Telemetry& t) {
        grid_.erase(t.id, t.lat, t.lon);
        byTime_.erase({t.ts, t.id});
        auto it = byStation_.find(t.station);
        if (it != byStation_.end()) { it->second.erase({t.ts, t.id}); if (it->second.empty()) byStation_.erase(it); }
    }
    void apply(const Op& op) {
        switch (op.kind) {
            case Op::PutT: {
                auto old = tel_.find(op.t.id);
                if (old != tel_.end()) unindexT(old->second);
                tel_[op.t.id] = op.t;
                indexT(op.t);
                if (op.t.id >= nextT_) nextT_ = op.t.id + 1;
                break;
            }
            case Op::PutS:
                subs_[op.s.id] = op.s;
                if (op.s.id >= nextS_) nextS_ = op.s.id + 1;
                break;
            case Op::DelT: {
                auto it = tel_.find(op.id);
                if (it != tel_.end()) { unindexT(it->second); tel_.erase(it); }
                break;
            }
            case Op::DelS: subs_.erase(op.id); break;
        }
    }

    // --- durability: one atomic transaction frame per commit
    static std::string frame(u64 txid, const std::vector<Op>& ops) {
        std::string body;
        for (const auto& o : ops) body += encodeOp(o) + "\n";
        return "B|" + std::to_string(txid) + "\n" + body + "C|" + std::to_string(txid) + "|" + hex8(crc32(body)) + "\n";
    }
    void commitOps(const std::vector<Op>& ops) {
        if (ops.empty()) return;
        std::unique_lock lk(mu_);
        const u64 tx = ++txid_;
        wal_.append(frame(tx, ops));  // durable before it becomes visible
        for (const auto& o : ops) apply(o);
        ++commits_;
    }
    void submit(std::vector<Op> ops) {
        {
            std::unique_lock lk(mu_);
            if (inTxn_) { pending_.insert(pending_.end(), ops.begin(), ops.end()); return; }
        }
        commitOps(ops);
    }

    void replay() {
        std::string data = wal_.readAll();
        if (data.empty()) { wal_.append(kHeader); return; }
        const size_t hdr = std::string(kHeader).size();
        if (data.compare(0, hdr, kHeader) != 0) throw DbError("'" + path_ + "' is not a StormDB file");
        size_t pos = hdr, lastGood = hdr;
        std::vector<Op> pend;
        std::string crcbuf;
        u64 curTx = 0;
        bool open = false;
        while (pos < data.size()) {
            const size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) break;  // torn final line
            const std::string line = data.substr(pos, nl - pos);
            const size_t next = nl + 1;
            if (line.rfind("B|", 0) == 0) {
                open = true; curTx = std::strtoull(line.c_str() + 2, nullptr, 10);
                pend.clear(); crcbuf.clear();
            } else if (line.rfind("C|", 0) == 0) {
                auto f = splitFields(line);
                if (!open || f.size() != 3 || std::strtoull(f[1].c_str(), nullptr, 10) != curTx ||
                    f[2] != hex8(crc32(crcbuf)))
                    break;  // corrupt commit marker
                for (const auto& o : pend) apply(o);
                txid_ = std::max(txid_, curTx);
                lastGood = next; open = false; pend.clear();
            } else {
                if (!open) break;
                try { pend.push_back(decodeOp(line)); } catch (const std::exception&) { break; }
                crcbuf += line; crcbuf += '\n';
            }
            pos = next;
        }
        recovered_ = data.size() - lastGood;
        if (recovered_ > 0) wal_.truncateTo(lastGood);  // discard the uncommitted / corrupt tail
    }

public:
    explicit Database(const std::string& path, bool sync = true) : path_(path), sync_(sync) {
        wal_.open(path, sync);
        replay();
    }

    // ---------------- writes ----------------
    u64 addTelemetry(Telemetry t) {
        if (t.ts == 0) t.ts = nowTs();
        validate(t);
        t.id = nextT_++;
        Op o; o.kind = Op::PutT; o.t = t;
        submit({o});
        return t.id;
    }
    u64 addSubscription(Subscription s) {
        s.created = s.created ? s.created : nowTs();
        validate(s);
        s.id = nextS_++;
        Op o; o.kind = Op::PutS; o.s = s;
        submit({o});
        return s.id;
    }
    bool removeTelemetry(u64 id) {
        { std::shared_lock lk(mu_); if (!tel_.count(id)) return false; }
        Op o; o.kind = Op::DelT; o.id = id; submit({o});
        return true;
    }
    bool removeSubscription(u64 id) {
        { std::shared_lock lk(mu_); if (!subs_.count(id)) return false; }
        Op o; o.kind = Op::DelS; o.id = id; submit({o});
        return true;
    }
    // Retention: deletes all telemetry strictly older than `ts` in one atomic commit.
    size_t purgeOlderThan(i64 ts) {
        std::vector<Op> ops;
        {
            std::shared_lock lk(mu_);
            for (auto it = byTime_.begin(); it != byTime_.end() && it->first < ts; ++it) {
                Op o; o.kind = Op::DelT; o.id = it->second; ops.push_back(o);
            }
        }
        const size_t n = ops.size();
        submit(std::move(ops));
        return n;
    }
    // CSV: station,lat,lon,pressure,wind,humidity[,ts]. All rows commit together or none do.
    size_t importCsv(const std::string& file) {
        std::ifstream in(file);
        if (!in) throw DbError("cannot read '" + file + "'");
        std::vector<Op> ops;
        std::string line;
        size_t lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> c;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, ',')) c.push_back(cell);
            if (lineNo == 1 && !c.empty() && c[0] == "station") continue;  // header row
            try {
                if (c.size() < 6) throw DbError("expected at least 6 columns");
                Telemetry t;
                t.station = c[0];
                t.lat = std::stod(c[1]); t.lon = std::stod(c[2]);
                t.pressure = std::stod(c[3]); t.wind = std::stod(c[4]); t.humidity = std::stod(c[5]);
                t.ts = c.size() > 6 ? std::stoll(c[6]) : nowTs();
                validate(t);
                t.id = nextT_++;
                Op o; o.kind = Op::PutT; o.t = t; ops.push_back(o);
            } catch (const std::exception& e) {
                throw DbError("import aborted at line " + std::to_string(lineNo) + ": " + e.what());
            }
        }
        const size_t n = ops.size();
        submit(std::move(ops));
        return n;
    }
    void exportCsv(const std::string& file) const {
        std::ofstream out(file);
        if (!out) throw DbError("cannot write '" + file + "'");
        out << "station,lat,lon,pressure,wind,humidity,ts\n";
        std::shared_lock lk(mu_);
        for (const auto& [ts, id] : byTime_) {
            const auto& t = tel_.at(id);
            out << t.station << ',' << fmt(t.lat) << ',' << fmt(t.lon) << ',' << fmt(t.pressure) << ','
                << fmt(t.wind) << ',' << fmt(t.humidity) << ',' << ts << '\n';
        }
    }

    // ---------------- transactions ----------------
    void begin() {
        std::unique_lock lk(mu_);
        if (inTxn_) throw DbError("a transaction is already open");
        inTxn_ = true; pending_.clear();
    }
    size_t commit() {
        std::vector<Op> ops;
        {
            std::unique_lock lk(mu_);
            if (!inTxn_) throw DbError("no open transaction");
            ops.swap(pending_); inTxn_ = false;
        }
        commitOps(ops);
        return ops.size();
    }
    size_t rollback() {
        std::unique_lock lk(mu_);
        if (!inTxn_) throw DbError("no open transaction");
        const size_t n = pending_.size();
        pending_.clear(); inTxn_ = false;
        return n;
    }
    bool inTransaction() const { std::shared_lock lk(mu_); return inTxn_; }

    // ---------------- reads ----------------
    std::vector<Hit> withinRadius(double lat, double lon, double km, size_t limit = 0) const {
        std::shared_lock lk(mu_);
        return radiusLocked(lat, lon, km, limit);
    }
    std::vector<Hit> nearest(double lat, double lon, size_t k) const {
        std::shared_lock lk(mu_);
        double r = 50;
        std::vector<Hit> hits;
        for (;;) {  // expanding search: once >= k hits are inside r, the first k are exact
            hits = radiusLocked(lat, lon, r, 0);
            if (hits.size() >= k || r >= 20100) break;
            r *= 2;
        }
        if (hits.size() > k) hits.resize(k);
        return hits;
    }
    std::vector<Hit> inBox(double lat0, double lon0, double lat1, double lon1) const {
        if (lat0 > lat1) std::swap(lat0, lat1);
        if (lon0 > lon1) std::swap(lon0, lon1);
        const double cl = (lat0 + lat1) / 2, co = (lon0 + lon1) / 2;
        const double km = haversineKm(cl, co, lat1, lon1) + 1.0;
        std::vector<Hit> out;
        for (auto& h : withinRadius(cl, co, km))
            if (h.t.lat >= lat0 && h.t.lat <= lat1 && h.t.lon >= lon0 && h.t.lon <= lon1) out.push_back(h);
        return out;
    }
    std::vector<Telemetry> range(i64 from, i64 to, size_t limit = 0) const {
        std::shared_lock lk(mu_);
        std::vector<Telemetry> out;
        for (auto it = byTime_.lower_bound({from, 0}); it != byTime_.end() && it->first <= to; ++it) {
            out.push_back(tel_.at(it->second));
            if (limit && out.size() >= limit) break;
        }
        return out;
    }
    std::vector<Telemetry> history(const std::string& station, size_t limit) const {
        std::shared_lock lk(mu_);
        std::vector<Telemetry> out;
        auto it = byStation_.find(station);
        if (it == byStation_.end()) return out;
        for (auto r = it->second.rbegin(); r != it->second.rend() && out.size() < limit; ++r) out.push_back(tel_.at(r->second));
        return out;
    }
    std::vector<Telemetry> latestPerStation() const {
        std::shared_lock lk(mu_);
        std::vector<Telemetry> out;
        for (const auto& [name, set] : byStation_) out.push_back(tel_.at(set.rbegin()->second));
        return out;
    }
    Agg aggregate(i64 since, const std::string& station = "") const {
        std::shared_lock lk(mu_);
        Agg a;
        auto take = [&](const Telemetry& t) {
            if (a.count == 0) { a.minP = a.maxP = t.pressure; a.minW = a.maxW = t.wind; a.minH = a.maxH = t.humidity; }
            a.minP = std::min(a.minP, t.pressure); a.maxP = std::max(a.maxP, t.pressure);
            a.minW = std::min(a.minW, t.wind);     a.maxW = std::max(a.maxW, t.wind);
            a.minH = std::min(a.minH, t.humidity); a.maxH = std::max(a.maxH, t.humidity);
            a.avgP += t.pressure; a.avgW += t.wind; a.avgH += t.humidity; ++a.count;
        };
        if (station.empty()) {
            for (auto it = byTime_.lower_bound({since, 0}); it != byTime_.end(); ++it) take(tel_.at(it->second));
        } else {
            auto s = byStation_.find(station);
            if (s != byStation_.end())
                for (auto it = s->second.lower_bound({since, 0}); it != s->second.end(); ++it) take(tel_.at(it->second));
        }
        if (a.count) { a.avgP /= a.count; a.avgW /= a.count; a.avgH /= a.count; }
        return a;
    }
    // Which subscriptions' geo-fences contain this point (who should be alerted).
    std::vector<Fence> fence(double lat, double lon) const {
        std::shared_lock lk(mu_);
        std::vector<Fence> out;
        for (const auto& [id, s] : subs_) {
            const double d = haversineKm(lat, lon, s.lat, s.lon);
            if (d <= s.radius_km) out.push_back({d, s});
        }
        std::sort(out.begin(), out.end(), [](const Fence& a, const Fence& b) { return a.km < b.km; });
        return out;
    }
    std::vector<Subscription> subscriptions() const {
        std::shared_lock lk(mu_);
        std::vector<Subscription> out;
        for (const auto& [id, s] : subs_) out.push_back(s);
        std::sort(out.begin(), out.end(), [](const Subscription& a, const Subscription& b) { return a.id < b.id; });
        return out;
    }
    Stats stats() const {
        std::shared_lock lk(mu_);
        Stats s;
        s.telemetry = tel_.size(); s.subscriptions = subs_.size(); s.stations = byStation_.size();
        s.cells = grid_.cellCount(); s.walBytes = wal_.size(); s.recoveredBytes = recovered_; s.commits = commits_;
        if (!byTime_.empty()) { s.firstTs = byTime_.begin()->first; s.lastTs = byTime_.rbegin()->first; }
        return s;
    }

    // ---------------- maintenance ----------------
    // Checkpoint: rewrites the log with live records only, then atomically swaps it in.
    std::pair<size_t, size_t> compact() {
        std::unique_lock lk(mu_);
        if (inTxn_) throw DbError("cannot compact during an open transaction");
        const size_t before = wal_.size();
        const std::string tmpPath = path_ + ".compact";
        {
            Wal tmp;
            tmp.open(tmpPath, true, true);
            tmp.append(kHeader);
            std::vector<Op> batch;
            auto flush = [&] {
                if (batch.empty()) return;
                tmp.append(frame(++txid_, batch));
                batch.clear();
            };
            for (const auto& [ts, id] : byTime_) {
                Op o; o.kind = Op::PutT; o.t = tel_.at(id); batch.push_back(o);
                if (batch.size() >= 500) flush();
            }
            for (const auto& [id, s] : subs_) { Op o; o.kind = Op::PutS; o.s = s; batch.push_back(o); }
            flush();
        }  // tmp is closed and durable here
        wal_.close();  // Windows cannot replace an open file, so release it first
        const bool swapped = sys::replaceFile(tmpPath, path_);
        wal_.open(path_, sync_);  // reopen (the original file if the swap failed)
        if (!swapped) throw DbError("could not replace the log during compaction");
        sys::fsyncDir(path_);
        return {before, wal_.size()};
    }
    // Cross-checks every index against the primary tables. Empty result = healthy.
    std::vector<std::string> verify() const {
        std::shared_lock lk(mu_);
        std::vector<std::string> problems;
        if (grid_.size() != tel_.size()) problems.push_back("grid index size mismatch");
        if (byTime_.size() != tel_.size()) problems.push_back("time index size mismatch");
        size_t n = 0;
        for (const auto& [name, set] : byStation_) n += set.size();
        if (n != tel_.size()) problems.push_back("station index size mismatch");
        for (const auto& [id, t] : tel_) {
            if (!byTime_.count({t.ts, id})) problems.push_back("telemetry " + std::to_string(id) + " missing from time index");
            const u64 tid = id;  // copy: lambdas cannot capture structured bindings before C++20
            bool found = false;
            grid_.visit(t.lat, t.lon, 1.0, [&](u64 g) { found = found || g == tid; });
            if (!found) problems.push_back("telemetry " + std::to_string(id) + " missing from grid index");
        }
        return problems;
    }
    const std::string& path() const { return path_; }

private:
    std::vector<Hit> radiusLocked(double lat, double lon, double km, size_t limit) const {
        std::vector<Hit> out;
        grid_.visit(lat, lon, km, [&](u64 id) {
            const Telemetry& t = tel_.at(id);
            const double d = haversineKm(lat, lon, t.lat, t.lon);
            if (d <= km) out.push_back({d, t});
        });
        std::sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.km < b.km; });
        if (limit && out.size() > limit) out.resize(limit);
        return out;
    }
};

}  // namespace stormdb

// =============================================================================
//  Command shell: one-shot commands and an interactive REPL
// =============================================================================
namespace shell {
using namespace stormdb;

struct Table {
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> rows;
};

inline bool isNumber(const std::string& s) {
    if (s.empty() || !(std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-' || s[0] == '.')) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}
inline std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else o += c;
    }
    return o + "\"";
}
inline void print(const Table& t, bool json, std::ostream& os) {
    if (json) {
        os << "[";
        for (size_t r = 0; r < t.rows.size(); ++r) {
            os << (r ? "," : "") << "\n  {";
            for (size_t c = 0; c < t.head.size(); ++c) {
                const std::string& v = t.rows[r][c];
                os << (c ? ", " : "") << jsonStr(t.head[c]) << ": " << (isNumber(v) ? v : jsonStr(v));
            }
            os << "}";
        }
        os << (t.rows.empty() ? "]\n" : "\n]\n");
        return;
    }
    std::vector<size_t> w(t.head.size());
    for (size_t c = 0; c < w.size(); ++c) w[c] = t.head[c].size();
    for (const auto& r : t.rows) for (size_t c = 0; c < w.size(); ++c) w[c] = std::max(w[c], r[c].size());
    auto line = [&](const std::vector<std::string>& r) {
        for (size_t c = 0; c < w.size(); ++c) os << std::left << std::setw(static_cast<int>(w[c]) + 2) << r[c];
        os << "\n";
    };
    line(t.head);
    for (const auto& r : t.rows) line(r);
    os << "(" << t.rows.size() << " row" << (t.rows.size() == 1 ? "" : "s") << ")\n";
}

inline Table telTable(const std::vector<Telemetry>& v) {
    Table t{{"id", "station", "time_utc", "ts", "lat", "lon", "pressure_hpa", "wind_kmh", "humidity_pct"}, {}};
    for (const auto& x : v)
        t.rows.push_back({std::to_string(x.id), x.station, isoTime(x.ts), std::to_string(x.ts), fmt(x.lat), fmt(x.lon),
                          fmt(x.pressure), fmt(x.wind), fmt(x.humidity)});
    return t;
}
inline Table hitTable(const std::vector<Hit>& hits) {
    std::vector<Telemetry> v;
    for (const auto& h : hits) v.push_back(h.t);
    Table t = telTable(v);
    t.head.insert(t.head.begin(), "distance_km");
    for (size_t i = 0; i < hits.size(); ++i) {
        char b[32]; std::snprintf(b, sizeof b, "%.2f", hits[i].km);
        t.rows[i].insert(t.rows[i].begin(), b);
    }
    return t;
}
inline Table subTable(const std::vector<Subscription>& v, const std::vector<Fence>* fences = nullptr) {
    Table t{{"id", "name", "audience", "lat", "lon", "radius_km"}, {}};
    if (fences) t.head.insert(t.head.begin(), "distance_km");
    for (size_t i = 0; i < v.size(); ++i) {
        std::vector<std::string> r{std::to_string(v[i].id), v[i].name, v[i].audience, fmt(v[i].lat), fmt(v[i].lon), fmt(v[i].radius_km)};
        if (fences) { char b[32]; std::snprintf(b, sizeof b, "%.2f", (*fences)[i].km); r.insert(r.begin(), b); }
        t.rows.push_back(r);
    }
    return t;
}

inline std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, has = false;
    char q = 0;
    for (char c : line) {
        if (quoted) { if (c == q) quoted = false; else cur += c; }
        else if (c == '"' || c == '\'') { quoted = true; q = c; has = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) {
            if (has || !cur.empty()) { out.push_back(cur); cur.clear(); has = false; }
        } else cur += c;
    }
    if (quoted) throw DbError("unterminated quote");
    if (has || !cur.empty()) out.push_back(cur);
    return out;
}
inline double num(const std::string& s, const char* what) {
    try { size_t n = 0; double v = std::stod(s, &n); if (n == s.size()) return v; } catch (...) {}
    throw DbError(std::string("invalid number for ") + what + ": '" + s + "'");
}
inline i64 timeArg(const std::string& s) {  // "now", "now-3600" (seconds ago) or a unix timestamp
    if (s == "now") return nowTs();
    if (s.rfind("now-", 0) == 0) return nowTs() - static_cast<i64>(num(s.substr(4), "seconds"));
    return static_cast<i64>(num(s, "timestamp"));
}

inline const char* kHelp =
    "Commands\n"
    "  put-telemetry <station> <lat> <lon> <hPa> <km/h> <hum%> [ts]   insert a reading\n"
    "  put-sub <name> <farmer|municipal|responder> <lat> <lon> <km>   add a geo-fence subscription\n"
    "  del-telemetry <id> | del-sub <id> | subs\n"
    "  radius <lat> <lon> <km>        readings inside a circle, nearest first\n"
    "  nearest <lat> <lon> <k>        k nearest readings\n"
    "  box <lat0> <lon0> <lat1> <lon1>   readings inside a bounding box\n"
    "  range <from> <to>              readings in a time window (unix ts, 'now', 'now-3600')\n"
    "  station <name> [limit]         newest readings from one station\n"
    "  latest                         newest reading of every station\n"
    "  agg <since> [station]          count / avg / min / max over a window\n"
    "  fence <lat> <lon>              subscriptions whose geo-fence contains the point\n"
    "  begin | commit | rollback      multi-statement transaction (shell mode)\n"
    "  purge <older-than>             delete readings older than a time (atomic)\n"
    "  import <csv> | export <csv>    bulk load / dump (station,lat,lon,pressure,wind,humidity[,ts])\n"
    "  compact | stats | check | seed-demo | help | quit\n";

// Runs one command. Returns false when the shell should exit.
inline bool execute(Database& db, const std::vector<std::string>& a, bool json, std::ostream& os) {
    if (a.empty()) return true;
    const std::string& c = a[0];
    auto need = [&](size_t n) { if (a.size() < n) throw DbError("missing arguments; type 'help'"); };
    if (c == "quit" || c == "exit") return false;
    if (c == "help") os << kHelp;
    else if (c == "put-telemetry") {
        need(7);
        Telemetry t;
        t.station = a[1]; t.lat = num(a[2], "lat"); t.lon = num(a[3], "lon"); t.pressure = num(a[4], "pressure");
        t.wind = num(a[5], "wind"); t.humidity = num(a[6], "humidity");
        t.ts = a.size() > 7 ? timeArg(a[7]) : nowTs();
        os << "ok id=" << db.addTelemetry(t) << (db.inTransaction() ? " (pending)" : "") << "\n";
    } else if (c == "put-sub") {
        need(6);
        Subscription s;
        s.name = a[1]; s.audience = a[2]; s.lat = num(a[3], "lat"); s.lon = num(a[4], "lon"); s.radius_km = num(a[5], "radius");
        os << "ok id=" << db.addSubscription(s) << "\n";
    } else if (c == "del-telemetry") { need(2); os << (db.removeTelemetry(static_cast<u64>(num(a[1], "id"))) ? "deleted\n" : "not found\n"); }
    else if (c == "del-sub") { need(2); os << (db.removeSubscription(static_cast<u64>(num(a[1], "id"))) ? "deleted\n" : "not found\n"); }
    else if (c == "subs") print(subTable(db.subscriptions()), json, os);
    else if (c == "radius") { need(4); print(hitTable(db.withinRadius(num(a[1], "lat"), num(a[2], "lon"), num(a[3], "km"))), json, os); }
    else if (c == "nearest") { need(4); print(hitTable(db.nearest(num(a[1], "lat"), num(a[2], "lon"), static_cast<size_t>(num(a[3], "k")))), json, os); }
    else if (c == "box") { need(5); print(hitTable(db.inBox(num(a[1], "lat0"), num(a[2], "lon0"), num(a[3], "lat1"), num(a[4], "lon1"))), json, os); }
    else if (c == "range") { need(3); print(telTable(db.range(timeArg(a[1]), timeArg(a[2]))), json, os); }
    else if (c == "station") { need(2); print(telTable(db.history(a[1], a.size() > 2 ? static_cast<size_t>(num(a[2], "limit")) : 20)), json, os); }
    else if (c == "latest") print(telTable(db.latestPerStation()), json, os);
    else if (c == "agg") {
        need(2);
        Agg g = db.aggregate(timeArg(a[1]), a.size() > 2 ? a[2] : "");
        Table t{{"metric", "count", "avg", "min", "max"}, {}};
        t.rows.push_back({"pressure_hpa", std::to_string(g.count), fmt(g.avgP), fmt(g.minP), fmt(g.maxP)});
        t.rows.push_back({"wind_kmh", std::to_string(g.count), fmt(g.avgW), fmt(g.minW), fmt(g.maxW)});
        t.rows.push_back({"humidity_pct", std::to_string(g.count), fmt(g.avgH), fmt(g.minH), fmt(g.maxH)});
        print(t, json, os);
    } else if (c == "fence") {
        need(3);
        auto f = db.fence(num(a[1], "lat"), num(a[2], "lon"));
        std::vector<Subscription> v;
        for (auto& x : f) v.push_back(x.s);
        print(subTable(v, &f), json, os);
    } else if (c == "begin") { db.begin(); os << "transaction started\n"; }
    else if (c == "commit") { os << "committed " << db.commit() << " operation(s)\n"; }
    else if (c == "rollback") { os << "rolled back " << db.rollback() << " operation(s)\n"; }
    else if (c == "purge") { need(2); os << "purged " << db.purgeOlderThan(timeArg(a[1])) << " reading(s)\n"; }
    else if (c == "import") { need(2); os << "imported " << db.importCsv(a[1]) << " reading(s)\n"; }
    else if (c == "export") { need(2); db.exportCsv(a[1]); os << "exported to " << a[1] << "\n"; }
    else if (c == "compact") { auto r = db.compact(); os << "log " << r.first << " -> " << r.second << " bytes\n"; }
    else if (c == "check") {
        auto p = db.verify();
        if (p.empty()) os << "integrity OK\n";
        for (auto& s : p) os << "PROBLEM: " << s << "\n";
    } else if (c == "stats") {
        Stats s = db.stats();
        Table t{{"key", "value"}, {}};
        t.rows = {{"telemetry_rows", std::to_string(s.telemetry)}, {"subscriptions", std::to_string(s.subscriptions)},
                  {"stations", std::to_string(s.stations)}, {"grid_cells", std::to_string(s.cells)},
                  {"log_bytes", std::to_string(s.walBytes)}, {"commits_this_session", std::to_string(s.commits)},
                  {"recovered_tail_bytes", std::to_string(s.recoveredBytes)},
                  {"oldest_reading", s.telemetry ? isoTime(s.firstTs) : "-"}, {"newest_reading", s.telemetry ? isoTime(s.lastTs) : "-"}};
        print(t, json, os);
    } else if (c == "seed-demo") {  // DEMO SIMULATION rows along the scripted demo storm track
        struct R { const char* n; double la, lo, p, w, h; };
        const R rows[] = {{"DEMO-NODE-A", 16.2, 66.3, 964, 128, 91}, {"DEMO-NODE-B", 18.7, 69.1, 971, 104, 88},
                          {"DEMO-NODE-C", 21.1, 71.5, 980, 86, 84}, {"DEMO-NODE-D", 23.0, 73.2, 992, 61, 79}};
        db.begin();
        int i = 0;
        for (const auto& r : rows) db.addTelemetry({0, r.n, nowTs() - 600 * i++, r.la, r.lo, r.p, r.w, r.h});
        Subscription s1{0, "DEMO farm cooperative", "farmer", 21.0, 71.4, 120, 0};
        Subscription s2{0, "DEMO municipal office", "municipal", 18.9, 69.0, 150, 0};
        db.addSubscription(s1); db.addSubscription(s2);
        os << "seeded " << db.commit() << " operations (DEMO SIMULATION data)\n";
    } else throw DbError("unknown command '" + c + "'; type 'help'");
    return true;
}

}  // namespace shell

// =============================================================================
//  Self-test suite (run with: stormdb selftest)
// =============================================================================
namespace selftest {
using namespace stormdb;

inline int run() {
    int pass = 0, fail = 0;
    auto check = [&](bool ok, const std::string& name) {
        std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name << "\n";
        ok ? ++pass : ++fail;
    };
    const char* td = std::getenv("TMPDIR");
    if (!td) td = std::getenv("TEMP");
    if (!td) td = std::getenv("TMP");
    const std::string path = std::string(td ? td : "/tmp") + "/stormdb_selftest_" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
    std::remove(path.c_str());
    std::cout << "StormDB self-test\n";
    auto mk = [](const std::string& st, double la, double lo, i64 ts = 0) {
        return Telemetry{0, st, ts, la, lo, 980, 60, 70};
    };
    std::vector<std::pair<double, double>> pts;
    {
        Database db(path, false);
        check(db.stats().telemetry == 0, "opens an empty database");
        bool threw = false;
        try { db.addTelemetry(mk("X", 95, 10)); } catch (const DbError&) { threw = true; }
        check(threw, "rejects out-of-range latitude");
        threw = false;
        try { Database second(path, false); } catch (const DbError&) { threw = true; }
        check(threw, "second handle is refused by the file lock");

        unsigned seed = 12345;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
        db.begin();
        for (int i = 0; i < 3000; ++i) {
            double la = -60 + rnd() * 120, lo = -180 + rnd() * 360;
            pts.push_back({la, lo});
            db.addTelemetry(mk("S" + std::to_string(i % 40), la, lo, 1000000 + i));
        }
        db.commit();
        check(db.stats().telemetry == 3000, "transaction commits 3000 rows");
        bool same = true;
        for (int q = 0; q < 25 && same; ++q) {
            double la = -50 + rnd() * 100, lo = -180 + rnd() * 360, km = 100 + rnd() * 2500;
            size_t brute = 0;
            for (auto& p : pts) brute += haversineKm(la, lo, p.first, p.second) <= km;
            same = brute == db.withinRadius(la, lo, km).size();
        }
        check(same, "radius index matches brute force (incl. dateline wrap)");
        auto nn = db.nearest(10, 10, 5);
        bool sorted = nn.size() == 5;
        for (size_t i = 1; i < nn.size(); ++i) sorted = sorted && nn[i - 1].km <= nn[i].km;
        double best = 1e18;
        for (auto& p : pts) best = std::min(best, haversineKm(10, 10, p.first, p.second));
        check(sorted && std::abs(nn[0].km - best) < 1e-9, "nearest-k is ordered and exact");

        db.begin();
        db.addTelemetry(mk("TMP", 1, 1));
        check(db.rollback() == 1 && db.stats().telemetry == 3000, "rollback discards pending writes");

        Subscription s{0, "Farm", "farmer", 20, 70, 100, 0};
        db.addSubscription(s);
        check(db.fence(20.5, 70.5).size() == 1 && db.fence(40, 70).empty(), "geo-fence containment");
        check(db.aggregate(0).count == 3000 && std::abs(db.aggregate(0).avgP - 980) < 1e-9, "aggregate over window");
        check(db.range(1000000, 1000009).size() == 10, "time range query");
        check(db.purgeOlderThan(1000100) == 100 && db.stats().telemetry == 2900, "retention purge");
        check(db.verify().empty(), "index integrity check");
    }
    {
        Database db(path, false);
        check(db.stats().telemetry == 2900 && db.subscriptions().size() == 1, "state survives restart (WAL replay)");
    }
    {   // simulate a crash mid-commit: a transaction with no commit marker plus a half-written line
        std::ofstream f(path, std::ios::app | std::ios::binary);
        f << "B|9999\nPT|999999|GHOST|1|1|1|980|60|70\nPT|99999";
    }
    {
        Database db(path, false);
        Stats s = db.stats();
        check(s.recoveredBytes > 0 && s.telemetry == 2900, "torn tail detected, truncated, data intact");
        db.addTelemetry(mk("POST", 5, 5));
    }
    {
        Database db(path, false);
        check(db.stats().telemetry == 2901 && db.history("GHOST", 5).empty(), "writes after recovery persist");
        for (u64 id = 1; id <= 1500; ++id) db.removeTelemetry(id);
        auto sizes = db.compact();
        check(sizes.second < sizes.first, "compaction shrinks the log");
        db.addTelemetry(mk("AFTER", 6, 6));
    }
    {
        Database db(path, false);
        check(db.verify().empty() && db.subscriptions().size() == 1, "database valid after compaction and reopen");
        const size_t before = db.stats().telemetry;
        std::vector<std::thread> th;
        for (int w = 0; w < 4; ++w)
            th.emplace_back([&db, w, &mk] { for (int i = 0; i < 250; ++i) db.addTelemetry(mk("W" + std::to_string(w), 1 + w, 1 + i % 50)); });
        for (int r = 0; r < 2; ++r)
            th.emplace_back([&db] { for (int i = 0; i < 200; ++i) { db.withinRadius(2, 20, 3000); db.latestPerStation(); } });
        for (auto& t : th) t.join();
        check(db.stats().telemetry == before + 1000 && db.verify().empty(), "4 writers + 2 readers run concurrently");
    }
    std::remove(path.c_str());
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
}  // namespace selftest

// =============================================================================
//  main
// =============================================================================
int main(int argc, char** argv) {
    bool json = false, sync = true;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json") json = true; else if (a == "--no-sync") sync = false; else args.push_back(a);
    }
    if (args.empty()) {
        std::cerr << "usage: stormdb [--json] [--no-sync] <db-file> [command ...]\n       stormdb selftest\n";
        return 1;
    }
    if (args[0] == "selftest") return selftest::run();
    try {
        stormdb::Database db(args[0], sync);
        if (args.size() > 1) {
            shell::execute(db, std::vector<std::string>(args.begin() + 1, args.end()), json, std::cout);
            return 0;
        }
        const bool tty = stormdb::sys::isTty();
        if (tty) std::cout << "StormDB 2.0 — " << db.path() << " (type 'help', 'quit' to exit)\n";
        std::string line;
        for (;;) {
            if (tty) std::cout << (db.inTransaction() ? "stormdb*> " : "stormdb> ") << std::flush;
            if (!std::getline(std::cin, line)) break;
            try {
                if (!shell::execute(db, shell::tokenize(line), json, std::cout)) break;
            } catch (const stormdb::DbError& e) { std::cout << "error: " << e.what() << "\n"; }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
