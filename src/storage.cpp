#include "storage.hpp"

#include "block.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr std::array<uint8_t, 4> MAGIC = {'N', 'M', 'C', 'R'};
constexpr uint32_t REGION_VERSION = 1;
constexpr int REGION_COLUMNS = 32;  // a region is 32 x 32 columns
constexpr std::size_t CHUNK_BLOCKS = CHUNK * CHUNK * CHUNK;
constexpr std::size_t COLUMN_BLOCKS = SECTIONS * CHUNK_BLOCKS;
constexpr uint32_t MAX_RUN = 0xFFFF;

void put_u16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 0; s < 32; s += 8) out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

// Bounds-checked little-endian reader: every read fails once the input runs out.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
    bool u16(uint32_t& v) { return read(2, v); }
    bool u32(uint32_t& v) { return read(4, v); }
    bool i32(int& v) {
        uint32_t u = 0;
        if (!read(4, u)) return false;
        v = static_cast<int>(static_cast<int32_t>(u));
        return true;
    }
    bool take(std::size_t n, std::vector<uint8_t>& out) {
        if (b_.size() - at_ < n) return false;
        out.assign(b_.begin() + static_cast<std::ptrdiff_t>(at_),
                   b_.begin() + static_cast<std::ptrdiff_t>(at_ + n));
        at_ += n;
        return true;
    }
    bool done() const { return at_ == b_.size(); }

private:
    bool read(std::size_t n, uint32_t& v) {
        if (b_.size() - at_ < n) return false;
        v = 0;
        for (std::size_t i = 0; i < n; ++i) v |= static_cast<uint32_t>(b_[at_ + i]) << (8 * i);
        at_ += n;
        return true;
    }

    const std::vector<uint8_t>& b_;
    std::size_t at_ = 0;
};

BlockId block_at(const Column& col, std::size_t i) {
    const auto& chunk = col[i / CHUNK_BLOCKS];
    if (!chunk) return blk::AIR;
    const BlockId id = chunk->blocks[i % CHUNK_BLOCKS];
    return id < blk::COUNT ? id : blk::AIR;  // never write an id the loader would reject
}

std::vector<uint8_t> encode(const Column& col) {
    std::vector<uint8_t> out;
    BlockId cur = block_at(col, 0);
    uint32_t run = 0;
    for (std::size_t i = 0; i < COLUMN_BLOCKS; ++i) {
        const BlockId b = block_at(col, i);
        if (b == cur && run < MAX_RUN) {
            ++run;
            continue;
        }
        put_u16(out, run);
        put_u16(out, cur);
        cur = b;
        run = 1;
    }
    put_u16(out, run);
    put_u16(out, cur);
    return out;
}

// Rejects zero runs, unknown ids, and runs that do not cover the column exactly.
std::optional<Column> decode(const std::vector<uint8_t>& bytes) {
    Column col;
    for (auto& c : col) {
        c = std::make_unique<Chunk>();
        c->modified = true;
    }
    Reader r(bytes);
    std::size_t i = 0;
    while (!r.done()) {
        uint32_t run = 0, id = 0;
        if (!r.u16(run) || !r.u16(id)) return std::nullopt;
        if (run == 0 || id >= blk::COUNT || run > COLUMN_BLOCKS - i) return std::nullopt;
        for (uint32_t k = 0; k < run; ++k, ++i)
            col[i / CHUNK_BLOCKS]->blocks[i % CHUNK_BLOCKS] = static_cast<BlockId>(id);
    }
    if (i != COLUMN_BLOCKS) return std::nullopt;
    return col;
}

using ColumnMap = std::map<std::pair<int, int>, std::vector<uint8_t>>;

// The whole file must be well formed, or the region counts as corrupt.
std::optional<ColumnMap> parse_region(const std::vector<uint8_t>& bytes, std::pair<int, int> region) {
    Reader r(bytes);
    std::vector<uint8_t> magic;
    uint32_t version = 0, count = 0;
    if (!r.take(MAGIC.size(), magic) || !std::equal(magic.begin(), magic.end(), MAGIC.begin()))
        return std::nullopt;
    if (!r.u32(version) || version != REGION_VERSION || !r.u32(count)) return std::nullopt;
    ColumnMap columns;
    for (uint32_t e = 0; e < count; ++e) {
        int cx = 0, cz = 0;
        uint32_t len = 0;
        std::vector<uint8_t> data;
        if (!r.i32(cx) || !r.i32(cz) || !r.u32(len) || !r.take(len, data)) return std::nullopt;
        if (floor_div(cx, REGION_COLUMNS) != region.first ||
            floor_div(cz, REGION_COLUMNS) != region.second)
            return std::nullopt;
        if (!decode(data)) return std::nullopt;
        if (!columns.emplace(std::make_pair(cx, cz), std::move(data)).second) return std::nullopt;
    }
    if (!r.done()) return std::nullopt;
    return columns;
}

std::optional<std::vector<uint8_t>> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return b;
}

// Writes to "<file>.tmp" and renames it over file, so a crash never leaves a half-written file.
bool write_file_atomic(const fs::path& file, const std::vector<uint8_t>& bytes) {
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

template <class T>
std::string number_text(T v) {
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    return std::string(buf.data(), res.ptr);
}

std::optional<uint32_t> parse_u32(std::string_view s) {
    uint32_t v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

std::optional<float> parse_float(std::string_view s) {
    float v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

}  // namespace

RegionStore::RegionStore(fs::path dir) : dir_(std::move(dir)) {
    fs::create_directories(dir_);
}

std::optional<Column> RegionStore::load_column(ColumnPos c) {
    std::vector<uint8_t> bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Region& reg =
            region_locked(Key{floor_div(c.x, REGION_COLUMNS), floor_div(c.z, REGION_COLUMNS)});
        auto it = reg.columns.find(Key{c.x, c.z});
        if (it == reg.columns.end()) return std::nullopt;
        bytes = it->second;
    }
    return decode(bytes);  // validated when the region was read; decoding outside the lock
}

void RegionStore::save_column(ColumnPos c, const Column& col) {
    std::vector<uint8_t> bytes = encode(col);
    std::lock_guard<std::mutex> lock(mutex_);
    Region& reg = region_locked(Key{floor_div(c.x, REGION_COLUMNS), floor_div(c.z, REGION_COLUMNS)});
    reg.columns.insert_or_assign(Key{c.x, c.z}, std::move(bytes));
    reg.dirty = true;
}

bool RegionStore::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool ok = true;
    for (auto& [key, reg] : regions_) {
        if (!reg.dirty) continue;
        std::vector<uint8_t> out(MAGIC.begin(), MAGIC.end());
        put_u32(out, REGION_VERSION);
        put_u32(out, static_cast<uint32_t>(reg.columns.size()));
        for (const auto& [pos, data] : reg.columns) {
            put_u32(out, static_cast<uint32_t>(pos.first));
            put_u32(out, static_cast<uint32_t>(pos.second));
            put_u32(out, static_cast<uint32_t>(data.size()));
            out.insert(out.end(), data.begin(), data.end());
        }
        if (write_file_atomic(region_path(key), out)) {
            reg.dirty = false;
        } else {
            ok = false;
        }
    }
    return ok;
}

RegionStore::Region& RegionStore::region_locked(Key r) {
    auto it = regions_.find(r);
    if (it != regions_.end()) return it->second;
    Region reg;
    if (auto bytes = read_file(region_path(r)))
        if (auto cols = parse_region(*bytes, r)) reg.columns = std::move(*cols);
    // A corrupt file counts as empty: its columns regenerate, and the next flush replaces it.
    return regions_.emplace(r, std::move(reg)).first->second;
}

fs::path RegionStore::region_path(Key r) const {
    return dir_ / ("r." + number_text(r.first) + "." + number_text(r.second) + ".bin");
}

bool save_meta(const fs::path& file, const WorldMeta& m) {
    const std::string text = "version=1\nseed=" + number_text(m.seed) + "\npx=" + number_text(m.px) +
                             "\npy=" + number_text(m.py) + "\npz=" + number_text(m.pz) +
                             "\nyaw=" + number_text(m.yaw) + "\npitch=" + number_text(m.pitch) + "\n";
    return write_file_atomic(file, std::vector<uint8_t>(text.begin(), text.end()));
}

std::optional<WorldMeta> load_meta(const fs::path& file) {
    const auto bytes = read_file(file);
    if (!bytes) return std::nullopt;
    const std::string text(bytes->begin(), bytes->end());

    std::optional<uint32_t> version, seed;
    std::optional<float> px, py, pz, yaw, pitch;
    // Skip a UTF-8 byte order mark, which some Windows editors add when saving.
    std::size_t start = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);  // edited on Windows
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;  // blank or stray text: ignored like unknown keys
        const std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "version") {
            if (!(version = parse_u32(value))) return std::nullopt;
        } else if (key == "seed") {
            if (!(seed = parse_u32(value))) return std::nullopt;
        } else if (key == "px" || key == "py" || key == "pz" || key == "yaw" || key == "pitch") {
            const std::optional<float> f = parse_float(value);
            if (!f) return std::nullopt;
            (key == "px" ? px : key == "py" ? py : key == "pz" ? pz : key == "yaw" ? yaw : pitch) = f;
        }  // unknown keys are ignored
    }
    if (version != 1u || !seed || !px || !py || !pz || !yaw || !pitch) return std::nullopt;
    return WorldMeta{*seed, *px, *py, *pz, *yaw, *pitch};
}
