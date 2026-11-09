#include "storage.hpp"

#include "block.hpp"
#include "block_entities.hpp"
#include "items.hpp"
#include "mobs.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace fs = std::filesystem;

namespace {

constexpr std::array<uint8_t, 4> MAGIC = {'N', 'M', 'C', 'R'};
constexpr uint32_t REGION_VERSION = 3;  // written; every version from 1 up to it loads
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
    bool u8(uint32_t& v) { return read(1, v); }
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
    bool f32(float& v) {
        uint32_t u = 0;
        if (!read(4, u)) return false;
        v = std::bit_cast<float>(u);
        return true;
    }
    bool done() const { return at_ == b_.size(); }
    std::size_t left() const { return b_.size() - at_; }

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

// ---- Block entities (v2) ----
// Section: u32 be_count, then per entity i32 x, y, z (world), u8 kind (BlockEntity index), payload.
// A stack is u16 id, u8 count, u16 damage; an empty stack is written as 0, 0, 0.
// Kiln: 4 stacks (in, in, fuel, out), f32 burn_left, burn_total, progress. Crate: 27 stacks.
// Cairn: u32 n, n stacks.

void put_stack(std::vector<uint8_t>& out, const ItemStack& st) {
    const ItemStack e = st.empty() ? ItemStack{} : st;
    put_u16(out, e.id);
    out.push_back(e.count);
    put_u16(out, e.damage);
}

void put_f32(std::vector<uint8_t>& out, float v) {
    put_u32(out, std::bit_cast<uint32_t>(v));
}

std::vector<uint8_t> encode_entities(const Column& col, ColumnPos c) {
    std::vector<uint8_t> out;
    uint32_t count = 0;
    put_u32(out, 0);  // patched below
    for (std::size_t y = 0; y < col.size(); ++y) {
        if (!col[y]) continue;
        for (const auto& [i, be] : col[y]->entities) {
            put_u32(out, static_cast<uint32_t>(c.x * CHUNK + i % CHUNK));
            put_u32(out, static_cast<uint32_t>(static_cast<int>(y) * CHUNK + i / (CHUNK * CHUNK)));
            put_u32(out, static_cast<uint32_t>(c.z * CHUNK + (i / CHUNK) % CHUNK));
            out.push_back(static_cast<uint8_t>(be.index()));
            if (const auto* k = std::get_if<KilnState>(&be)) {
                for (const ItemStack& st : {k->in[0], k->in[1], k->fuel, k->out}) put_stack(out, st);
                put_f32(out, k->burn_left);
                put_f32(out, k->burn_total);
                put_f32(out, k->progress);
            } else if (const auto* cr = std::get_if<CrateState>(&be)) {
                for (const ItemStack& st : cr->slots) put_stack(out, st);
            } else {
                const auto& items = std::get<CairnState>(be).items;
                put_u32(out, static_cast<uint32_t>(items.size()));
                for (const ItemStack& st : items) put_stack(out, st);
            }
            ++count;
        }
    }
    for (int k = 0; k < 4; ++k) out[static_cast<std::size_t>(k)] = static_cast<uint8_t>((count >> (8 * k)) & 0xFF);
    return out;
}

// A stack must name a real item, fit its max_stack, and carry damage only below a tool's
// durability; count 0 reads as an empty stack.
bool read_stack(Reader& r, ItemStack& st) {
    uint32_t id = 0, count = 0, damage = 0;
    if (!r.u16(id) || !r.u8(count) || !r.u16(damage)) return false;
    if (count == 0) {
        st = ItemStack{};
        return true;
    }
    const ItemDef& d = item_def(static_cast<ItemId>(id));
    if (d.max_stack == 0 || count > d.max_stack) return false;
    if (d.durability == 0 ? damage != 0 : damage >= d.durability) return false;
    st = ItemStack{static_cast<ItemId>(id), static_cast<uint8_t>(count), static_cast<uint16_t>(damage)};
    return true;
}

bool read_time(Reader& r, float& v) {
    return r.f32(v) && std::isfinite(v) && v >= 0.f;
}

constexpr std::size_t ENTITY_HEAD = 13, STACK_BYTES = 5;

// Reads a whole entity section into col (chunks already allocated); false on any error.
bool decode_entities(Reader& r, ColumnPos c, Column* col) {
    uint32_t count = 0;
    if (!r.u32(count)) return false;
    if (count > r.left() / (ENTITY_HEAD + 4)) return false;  // every entity takes at least 17 bytes
    for (uint32_t e = 0; e < count; ++e) {
        int x = 0, y = 0, z = 0;
        uint32_t kind = 0;
        if (!r.i32(x) || !r.i32(y) || !r.i32(z) || !r.u8(kind)) return false;
        if (floor_div(x, CHUNK) != c.x || floor_div(z, CHUNK) != c.z || y < 0 || y >= WORLD_HEIGHT) return false;
        BlockEntity be;
        if (kind == 0) {
            KilnState k;
            for (ItemStack* st : {&k.in[0], &k.in[1], &k.fuel, &k.out})
                if (!read_stack(r, *st)) return false;
            if (!read_time(r, k.burn_left) || !read_time(r, k.burn_total) || !read_time(r, k.progress)) return false;
            be = k;
        } else if (kind == 1) {
            CrateState cr;
            for (ItemStack& st : cr.slots)
                if (!read_stack(r, st)) return false;
            be = cr;
        } else if (kind == 2) {
            uint32_t n = 0;
            if (!r.u32(n) || n > r.left() / STACK_BYTES) return false;
            CairnState cairn;
            cairn.items.resize(n);
            for (ItemStack& st : cairn.items)
                if (!read_stack(r, st)) return false;
            be = std::move(cairn);
        } else {
            return false;
        }
        const auto i = static_cast<uint16_t>(Chunk::index(floor_mod(x, CHUNK), y % CHUNK, floor_mod(z, CHUNK)));
        if (col == nullptr) continue;  // validating only
        auto& slot = (*col)[static_cast<std::size_t>(y / CHUNK)]->entities;
        if (!slot.emplace(i, std::move(be)).second) return false;  // two entities in one cell
    }
    return true;
}

// ---- Creatures (v3) ----
// Section: u32 mob_count, then per mob u8 type, f32 x, y, z, yaw, health.

constexpr std::size_t MOB_BYTES = 21;
constexpr float MOB_MIN_Y = -64.f, MOB_MAX_Y = 320.f;  // feet heights a saved creature may have

// A passive type, finite values, health > 0, feet inside column c and within the height range.
// Range checks happen on the floats, before anything casts them to int.
bool savable(const SavedMob& m, ColumnPos c) {
    if (m.type >= static_cast<uint8_t>(MobType::COUNT) || mob_def(static_cast<MobType>(m.type)).hostile) return false;
    if (!std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.z) || !std::isfinite(m.yaw) ||
        !std::isfinite(m.health) || !(m.health > 0.f))
        return false;
    const auto x0 = static_cast<float>(c.x * CHUNK), z0 = static_cast<float>(c.z * CHUNK);
    return m.x >= x0 && m.x < x0 + CHUNK && m.z >= z0 && m.z < z0 + CHUNK && m.y >= MOB_MIN_Y && m.y <= MOB_MAX_Y;
}

std::vector<uint8_t> encode_mobs(const Column& col, ColumnPos c) {
    std::vector<uint8_t> out;
    uint32_t count = 0;
    for (const auto& ch : col)
        if (ch)
            for (const SavedMob& m : ch->mobs) count += savable(m, c) ? 1u : 0u;
    put_u32(out, count);
    for (const auto& ch : col) {
        if (!ch) continue;
        for (const SavedMob& m : ch->mobs) {
            if (!savable(m, c)) continue;  // never write what the loader rejects
            out.push_back(m.type);
            for (float v : {m.x, m.y, m.z, m.yaw, m.health}) put_f32(out, v);
        }
    }
    return out;
}

// Reads a whole creature section into col (chunks already allocated, or nullptr to validate).
bool decode_mobs(Reader& r, ColumnPos c, Column* col) {
    uint32_t count = 0;
    if (!r.u32(count) || count > r.left() / MOB_BYTES) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t type = 0;
        SavedMob m{};
        if (!r.u8(type) || !r.f32(m.x) || !r.f32(m.y) || !r.f32(m.z) || !r.f32(m.yaw) || !r.f32(m.health))
            return false;
        m.type = static_cast<uint8_t>(type);
        if (!savable(m, c)) return false;
        if (col == nullptr) continue;
        const int cy = static_cast<int>(std::clamp(m.y, 0.f, static_cast<float>(WORLD_HEIGHT - 1))) / CHUNK;
        (*col)[static_cast<std::size_t>(cy)]->mobs.push_back(m);
    }
    return true;
}

std::optional<Column> decode_column(const RegionColumn& d, ColumnPos c) {
    std::optional<Column> col = decode(d.blocks);
    if (!col) return std::nullopt;
    Reader r(d.entities);
    if (!decode_entities(r, c, &*col) || !r.done()) return std::nullopt;
    Reader m(d.mobs);
    if (!decode_mobs(m, c, &*col) || !m.done()) return std::nullopt;
    return col;
}

using ColumnMap = std::map<std::pair<int, int>, RegionColumn>;

// The whole file must be well formed, or the region counts as corrupt. Version 1 files have no
// entity sections; they load as columns without block entities. Versions 1 and 2 have no creature
// sections; they load as columns without creatures.
std::optional<ColumnMap> parse_region(const std::vector<uint8_t>& bytes, std::pair<int, int> region) {
    Reader r(bytes);
    std::vector<uint8_t> magic;
    uint32_t version = 0, count = 0;
    if (!r.take(MAGIC.size(), magic) || !std::equal(magic.begin(), magic.end(), MAGIC.begin()))
        return std::nullopt;
    if (!r.u32(version) || version < 1 || version > REGION_VERSION || !r.u32(count)) return std::nullopt;
    ColumnMap columns;
    for (uint32_t e = 0; e < count; ++e) {
        int cx = 0, cz = 0;
        uint32_t len = 0;
        RegionColumn data;
        if (!r.i32(cx) || !r.i32(cz) || !r.u32(len) || !r.take(len, data.blocks)) return std::nullopt;
        if (floor_div(cx, REGION_COLUMNS) != region.first ||
            floor_div(cz, REGION_COLUMNS) != region.second)
            return std::nullopt;
        if (version >= 2) {
            // Validate the entity section in place, then keep exactly its bytes.
            Reader probe = r;
            if (!decode_entities(probe, ColumnPos{cx, cz}, nullptr)) return std::nullopt;
            if (!r.take(r.left() - probe.left(), data.entities)) return std::nullopt;
        } else {
            put_u32(data.entities, 0);
        }
        if (version >= 3) {
            Reader probe = r;
            if (!decode_mobs(probe, ColumnPos{cx, cz}, nullptr)) return std::nullopt;
            if (!r.take(r.left() - probe.left(), data.mobs)) return std::nullopt;
        } else {
            put_u32(data.mobs, 0);
        }
        if (!decode_column(data, ColumnPos{cx, cz})) return std::nullopt;  // also catches duplicate cells
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

// The whole of s as a finite float or double.
template <class T>
std::optional<T> parse_finite(std::string_view s) {
    T v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<float> parse_float(std::string_view s) {
    return parse_finite<float>(s);
}

constexpr double DEFAULT_TIME = 6000;

// Splits s at commas into exactly n fields; nullopt for any other count.
template <std::size_t N>
std::optional<std::array<std::string_view, N>> split_fields(std::string_view s) {
    std::array<std::string_view, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t comma = s.find(',');
        if ((comma == std::string_view::npos) != (i == N - 1)) return std::nullopt;
        out[i] = s.substr(0, comma);
        if (comma != std::string_view::npos) s.remove_prefix(comma + 1);
    }
    return out;
}

// `vitals` (M5): health, hunger, saturation, air, exhaustion, regen_timer, starve_timer. Any
// field that is not a finite number, or a wrong field count, gives full vitals; values are clamped.
Vitals parse_vitals(std::string_view s) {
    const auto fields = split_fields<7>(s);
    if (!fields) return Vitals{};
    std::array<float, 7> f{};
    for (std::size_t i = 0; i < f.size(); ++i) {
        const std::optional<float> v = parse_float((*fields)[i]);
        if (!v) return Vitals{};
        f[i] = *v;
    }
    Vitals v;
    v.health = std::clamp(f[0], 0.f, MAX_HEALTH);
    v.hunger = std::clamp(f[1], 0.f, MAX_HUNGER);
    v.saturation = std::clamp(f[2], 0.f, v.hunger);
    v.air = std::clamp(f[3], 0.f, MAX_AIR);
    // Below 4 after a drain, plus up to 6 from regeneration before the next tick drains it.
    v.exhaustion = std::clamp(f[4], 0.f, 10.f);
    v.regen_timer = std::clamp(f[5], 0.f, 4.f);
    v.starve_timer = std::clamp(f[6], 0.f, 4.f);
    return v;
}

std::string vitals_text(const Vitals& v) {
    const float f[7] = {v.health, v.hunger, v.saturation, v.air, v.exhaustion, v.regen_timer, v.starve_timer};
    std::string out;
    for (int i = 0; i < 7; ++i) out += (i > 0 ? "," : "") + number_text(f[i]);
    return out;
}

// `spawn` (M5): the bedroll cell x,y,z as integers, y in 0..255; anything else is no bedroll.
std::optional<BlockPos> parse_spawn(std::string_view s) {
    const auto fields = split_fields<3>(s);
    if (!fields) return std::nullopt;
    std::array<int, 3> c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        const std::string_view t = (*fields)[i];
        const auto res = std::from_chars(t.data(), t.data() + t.size(), c[i]);
        if (t.empty() || res.ec != std::errc{} || res.ptr != t.data() + t.size()) return std::nullopt;
    }
    if (c[1] < 0 || c[1] >= WORLD_HEIGHT) return std::nullopt;
    return BlockPos{c[0], c[1], c[2]};
}

}  // namespace

RegionStore::RegionStore(fs::path dir) : dir_(std::move(dir)) {
    fs::create_directories(dir_);
}

std::optional<Column> RegionStore::load_column(ColumnPos c) {
    RegionColumn bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Region& reg =
            region_locked(Key{floor_div(c.x, REGION_COLUMNS), floor_div(c.z, REGION_COLUMNS)});
        auto it = reg.columns.find(Key{c.x, c.z});
        if (it == reg.columns.end()) return std::nullopt;
        bytes = it->second;
    }
    return decode_column(bytes, c);  // validated when the region was read; decoding outside the lock
}

void RegionStore::save_column(ColumnPos c, const Column& col) {
    RegionColumn bytes{encode(col), encode_entities(col, c), encode_mobs(col, c)};
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
            put_u32(out, static_cast<uint32_t>(data.blocks.size()));
            out.insert(out.end(), data.blocks.begin(), data.blocks.end());
            out.insert(out.end(), data.entities.begin(), data.entities.end());
            out.insert(out.end(), data.mobs.begin(), data.mobs.end());
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

namespace {

std::string stack_list(std::span<const ItemStack> slots) {
    std::string out;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const ItemStack e = slots[i].empty() ? ItemStack{} : slots[i];
        if (i > 0) out += ',';
        out += number_text(e.id) + ':' + number_text(static_cast<int>(e.count)) + ':' + number_text(e.damage);
    }
    return out;
}

// One id:count:damage entry; nullopt when it is not a valid stack for `kind`.
std::optional<ItemStack> parse_stack(std::string_view e, ArmorSlot armor) {
    const std::size_t a = e.find(':');
    const std::size_t b = a == std::string_view::npos ? a : e.find(':', a + 1);
    if (b == std::string_view::npos) return std::nullopt;
    const auto id = parse_u32(e.substr(0, a)), count = parse_u32(e.substr(a + 1, b - a - 1)),
               damage = parse_u32(e.substr(b + 1));
    if (!id || !count || !damage || *id > 0xFFFF) return std::nullopt;
    const ItemDef& d = item_def(static_cast<ItemId>(*id));
    if (*count == 0 || d.max_stack == 0 || *count > d.max_stack) return std::nullopt;
    if (d.durability == 0 ? *damage != 0 : *damage >= d.durability) return std::nullopt;
    if (armor != ArmorSlot::None && d.armor != armor) return std::nullopt;
    return ItemStack{static_cast<ItemId>(*id), static_cast<uint8_t>(*count), static_cast<uint16_t>(*damage)};
}

// Fills `slots` from a comma-separated list; invalid entries and missing ones stay empty.
void parse_stack_list(std::string_view v, std::span<ItemStack> slots, bool armor) {
    constexpr ArmorSlot ARMOR_ORDER[4] = {ArmorSlot::Head, ArmorSlot::Chest, ArmorSlot::Legs, ArmorSlot::Feet};
    for (ItemStack& s : slots) s = ItemStack{};
    std::size_t i = 0;
    while (i < slots.size()) {
        const std::size_t comma = v.find(',');
        const std::string_view entry = v.substr(0, comma);
        const std::optional<ItemStack> st = parse_stack(entry, armor ? ARMOR_ORDER[i] : ArmorSlot::None);
        if (st) slots[i] = *st;
        ++i;
        if (comma == std::string_view::npos) break;
        v.remove_prefix(comma + 1);
    }
}

}  // namespace

bool save_meta(const fs::path& file, const WorldMeta& m) {
    std::string text = "version=1\nseed=" + number_text(m.seed) + "\npx=" + number_text(m.px) +
                       "\npy=" + number_text(m.py) + "\npz=" + number_text(m.pz) +
                       "\nyaw=" + number_text(m.yaw) + "\npitch=" + number_text(m.pitch) +
                       "\ntime=" + number_text(m.time) +
                       "\nmode=" + (m.mode == GameMode::Creative ? "creative" : "survival") +
                       "\ninv=" + stack_list(m.inv.slots) + "\narmor=" + stack_list(m.inv.armor) +
                       "\nsel=" + number_text(m.inv.selected) + "\nvitals=" + vitals_text(m.vitals) + "\n";
    if (m.spawn)
        text += "spawn=" + number_text(m.spawn->x) + "," + number_text(m.spawn->y) + "," + number_text(m.spawn->z) +
                "\n";
    return write_file_atomic(file, std::vector<uint8_t>(text.begin(), text.end()));
}

std::optional<WorldMeta> load_meta(const fs::path& file) {
    const auto bytes = read_file(file);
    if (!bytes) return std::nullopt;
    const std::string text(bytes->begin(), bytes->end());

    std::optional<uint32_t> version, seed;
    std::optional<float> px, py, pz, yaw, pitch;
    double time = DEFAULT_TIME;
    GameMode mode = GameMode::Survival;
    Inventory inv;
    Vitals vitals;
    std::optional<BlockPos> spawn;
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
        } else if (key == "time") {
            // Optional: a bad value falls back to the default instead of rejecting the world.
            const std::optional<double> t = parse_finite<double>(value);
            time = t && *t >= 0 ? *t : DEFAULT_TIME;
        } else if (key == "mode") {
            mode = value == "creative" ? GameMode::Creative : GameMode::Survival;
        } else if (key == "inv") {
            parse_stack_list(value, inv.slots, false);
        } else if (key == "armor") {
            parse_stack_list(value, inv.armor, true);
        } else if (key == "sel") {
            const std::optional<uint32_t> sel = parse_u32(value);
            inv.selected = sel && *sel <= 8 ? static_cast<int>(*sel) : 0;
        } else if (key == "vitals") {
            vitals = parse_vitals(value);
        } else if (key == "spawn") {
            spawn = parse_spawn(value);
        }  // unknown keys are ignored
    }
    if (version != 1u || !seed || !px || !py || !pz || !yaw || !pitch) return std::nullopt;
    WorldMeta m{*seed, *px, *py, *pz, *yaw, *pitch};
    m.time = time;
    m.mode = mode;
    m.inv = inv;
    m.vitals = vitals;
    m.spawn = spawn;
    return m;
}
