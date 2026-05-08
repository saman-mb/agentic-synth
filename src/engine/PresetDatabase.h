#pragma once

#include "engine/PatchStruct.h"
#include <string>
#include <vector>
#include <functional>

namespace agentic_synth::engine {

// SQLite persistence for presets and session events.
// Thread-safe, uses WAL mode for concurrent reads.

class PresetDatabase {
public:
    PresetDatabase();
    explicit PresetDatabase(const std::string& dbPath);
    ~PresetDatabase();

    PresetDatabase(const PresetDatabase&) = delete;
    PresetDatabase& operator=(const PresetDatabase&) = delete;

    // Open/close
    bool open(const std::string& dbPath);
    void close();

    // Presets
    bool savePreset(const std::string& name, const PatchStruct& patch);
    bool loadPreset(int id, PatchStruct& patch);
    bool loadPreset(const std::string& name, PatchStruct& patch);
    std::vector<std::string> listPresets();
    bool deletePreset(int id);

    // Session events
    bool logEvent(const std::string& type, const std::string& data);
    std::vector<std::string> recentEvents(int limit = 50);

    // Migration
    bool exportToJson(const std::string& path);
    bool importFromJson(const std::string& path);

private:
    void* db_ = nullptr;  // opaque SQLite3 handle
    bool ensureTables();
    PatchStruct blobToPatchStruct(const std::vector<uint8_t>& blob);
    std::vector<uint8_t> patchStructToBlob(const PatchStruct& patch);
};

} // namespace agentic_synth::engine
