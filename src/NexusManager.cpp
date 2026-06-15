/**
 * @file NexusManager.cpp
 * @brief Implements the NexusManager singleton with thread-safe synchronization.
 */

#include "NexusManager.h"
#include "FileSystem.h"
#include <algorithm>
#include <iostream>
#include <mutex> // ADDED FOR SYNCHRONIZATION

namespace nexusdb {

// Global mutex to ensure thread-safe table creation and deletion
static std::mutex managerMutex;

NexusManager& NexusManager::getInstance() {
    static NexusManager instance;
    return instance;
}

void NexusManager::initialize(const std::string& dataDir) {
    dataDir_ = dataDir;
    if (!fs::exists(dataDir_)) {
        fs::createDirectory(dataDir_);
    }
    discoverTables();
}

void NexusManager::discoverTables() {
    tables_.clear();
    currentTable_ = nullptr;

    if (!fs::exists(dataDir_)) return;

    std::vector<std::string> files = fs::listFiles(dataDir_, ".csv");
    for (const auto& f : files) {
        try {
            tables_.emplace_back(f);
        } catch (const std::exception& e) {
            std::cerr << "[warn] Skipping " << fs::filename(f)
                      << ": " << e.what() << "\n";
        }
    }
}

std::vector<std::string> NexusManager::showTables() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& t : tables_) names.push_back(t.getName());
    return names;
}

bool NexusManager::useTable(const std::string& name) {
    Table* t = findTable(name);
    if (t) { currentTable_ = t; return true; }
    return false;
}

void NexusManager::clearCurrentTable() { currentTable_ = nullptr; }
Table* NexusManager::getCurrentTable() { return currentTable_; }

Table* NexusManager::findTable(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& t : tables_) {
        std::string tLower = t.getName();
        std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);
        if (tLower == lower) return &t;
    }
    return nullptr;
}

bool NexusManager::createTable(const std::string& name,
                               const std::vector<Column>& schema,
                               std::string& errMsg) {
    // --- SYNCHRONIZATION ADDED HERE ---
    // Locks the thread to prevent data corruption during concurrent creation
    std::lock_guard<std::mutex> lock(managerMutex); 

    if (findTable(name)) { errMsg = "Table '" + name + "' already exists."; return false; }
    if (schema.empty()) { errMsg = "Cannot create a table with no columns."; return false; }

    std::string filePath = fs::joinPath(dataDir_, name + ".csv");
    Table newTable(name, schema);
    newTable.setFilePath(filePath);
    newTable.save();
    tables_.push_back(std::move(newTable));
    return true;
}

bool NexusManager::dropTable(const std::string& name, std::string& errMsg) {
    // --- SYNCHRONIZATION ADDED HERE ---
    // Locks the thread to safely delete files without race conditions
    std::lock_guard<std::mutex> lock(managerMutex);

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (auto it = tables_.begin(); it != tables_.end(); ++it) {
        std::string tLower = it->getName();
        std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);
        if (tLower == lower) {
            if (currentTable_ == &(*it)) currentTable_ = nullptr;
            std::string fp = it->getFilePath();
            if (fs::exists(fp)) fs::removeFile(fp);
            tables_.erase(it);
            return true;
        }
    }
    errMsg = "Table '" + name + "' not found.";
    return false;
}

const std::string& NexusManager::getDataDir() const { return dataDir_; }

} // namespace nexusdb
