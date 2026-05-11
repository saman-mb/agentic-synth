#include "agent/DictionaryService.h"

namespace agentic_synth::agent {

DictionaryService::DictionaryService(mapper::SemanticMapper& mapper) : mapper_(mapper) {}

std::string DictionaryService::getDictionaryJson() const {
    return "{\"type\":\"dictionary_data\",\"entries\":" + mapper_.dumpAllToJson() + "}";
}

void DictionaryService::saveDictionary(const std::string& json) {
    mapper_.parseAndSaveCustomEntries(json, "descriptor_dataset_custom.json");
}

void DictionaryService::loadDictionary(const std::string& path) { mapper_.loadCustomEntries(path); }

} // namespace agentic_synth::agent
