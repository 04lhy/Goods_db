#include "sql/handlerton.h"
#include "common/logger.h"

namespace goods_db {

// =============================================================================
// Global Engine Registry
// =============================================================================

namespace {

std::unordered_map<std::string, handlerton*>& GetEngineRegistry() {
    static std::unordered_map<std::string, handlerton*> registry;
    return registry;
}

std::mutex& GetRegistryMutex() {
    static std::mutex mtx;
    return mtx;
}

}  // namespace

bool register_engine(handlerton* engine) {
    if (!engine || !engine->name) {
        LOG_ERROR("register_engine: invalid engine pointer");
        return false;
    }

    std::lock_guard<std::mutex> lock(GetRegistryMutex());
    auto& registry = GetEngineRegistry();

    std::string name(engine->name);
    if (registry.find(name) != registry.end()) {
        LOG_WARN("register_engine: engine '{}' already registered", name);
        return false;
    }

    registry[name] = engine;

    // Call the engine's init callback
    if (engine->init) {
        int result = engine->init();
        if (result != 0) {
            LOG_ERROR("register_engine: engine '{}' init failed with code {}", name, result);
            registry.erase(name);
            return false;
        }
    }

    LOG_INFO("Engine '{}' registered successfully", name);
    return true;
}

handlerton* get_engine(const std::string& name) {
    std::lock_guard<std::mutex> lock(GetRegistryMutex());
    auto& registry = GetEngineRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) return nullptr;
    return it->second;
}

std::vector<handlerton*> get_all_engines() {
    std::lock_guard<std::mutex> lock(GetRegistryMutex());
    std::vector<handlerton*> engines;
    for (const auto& [name, engine] : GetEngineRegistry()) {
        engines.push_back(engine);
    }
    return engines;
}

bool unregister_engine(const std::string& name) {
    std::lock_guard<std::mutex> lock(GetRegistryMutex());
    auto& registry = GetEngineRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) return false;

    handlerton* engine = it->second;
    // Call deinit callback
    if (engine->deinit) {
        engine->deinit();
    }

    registry.erase(it);
    LOG_INFO("Engine '{}' unregistered", name);
    return true;
}

}  // namespace goods_db
