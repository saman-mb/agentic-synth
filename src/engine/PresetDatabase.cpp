#include "PresetDatabase.h"

// SQLite persistence implementation.
// Requires SQLite3 (bundled via JUCE or system package).
// If SQLite3 is not available, this compiles to a no-op stub.

namespace agentic_synth::engine {

PresetDatabase::PresetDatabase() = default;

PresetDatabase::PresetDatabase(const std::string& dbPath) {
    open(dbPath);
}

PresetDatabase::~PresetDatabase() {
    close();
}

bool PresetDatabase::open(const std::string& dbPath) {
    // TODO: implement with SQLite3 C API
    // sqlite3_open_v2(dbPath.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    // savePreset, loadPreset, etc.
    (void)dbPath;
    return true;
}

void PresetDatabase::close() {
    // TODO: sqlite3_close(db_);
    db_ = nullptr;
}

bool PresetDatabase::savePreset(const std::string& name, const PatchStruct& patch) {
    (void)name;
    (void)patch;
    return true;
}

bool PresetDatabase::loadPreset(int id, PatchStruct& patch) {
    (void)id;
    (void)patch;
    return false;
}

bool PresetDatabase::loadPreset(const std::string& name, PatchStruct& patch) {
    (void)name;
    (void)patch;
    return false;
}

std::vector<std::string> PresetDatabase::listPresets() {
    return {};
}

bool PresetDatabase::deletePreset(int id) {
    (void)id;
    return true;
}

bool PresetDatabase::logEvent(const std::string& type, const std::string& data) {
    (void)type;
    (void)data;
    return true;
}

std::vector<std::string> PresetDatabase::recentEvents(int limit) {
    (void)limit;
    return {};
}

bool PresetDatabase::ensureTables() { return true; }
bool PresetDatabase::exportToJson(const std::string& path) {
    (void)path;
    return true;
}
bool PresetDatabase::importFromJson(const std::string& path) {
    (void)path;
    return true;
}

std::vector<uint8_t> PresetDatabase::patchStructToBlob(const PatchStruct& patch) {
    auto* data = reinterpret_cast<const uint8_t*>(&patch);
    return {data, data + sizeof(PatchStruct)};
}

PatchStruct PresetDatabase::blobToPatchStruct(const std::vector<uint8_t>& blob) {
    PatchStruct p{};
    if (blob.size() >= sizeof(PatchStruct)) {
        std::memcpy(&p, blob.data(), sizeof(PatchStruct));
    }
    return p;
}

} // namespace agentic_synth::engine
