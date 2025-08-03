#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/sd_mmc_card/sd_mmc_card.h"

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <map>

namespace esphome {
namespace storage {

// Forward declarations
class StorageActionFactory;

// Structure pour configuration des fichiers
struct FileConfig {
  std::string id;
  std::string path;
  size_t chunk_size;
  
  FileConfig(const std::string &id, const std::string &path, size_t chunk_size)
    : id(id), path(path), chunk_size(chunk_size) {}
};

// Structure pour le cache
struct CacheEntry {
  std::vector<uint8_t> data;
  uint32_t last_access;
  size_t size;
  
  CacheEntry() : last_access(0), size(0) {}
  CacheEntry(const std::vector<uint8_t>& d) : data(d), last_access(millis()), size(d.size()) {}
};

// Composant principal Storage
class StorageComponent : public Component {
 public:
  StorageComponent() = default;
  
  // Méthodes du composant ESPHome
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration
  void set_platform(const std::string &platform) { platform_name_ = platform; }
  void set_sd_component(esphome::sd_mmc_card::SDMMCCard *sd_card) { sd_card_ = sd_card; }
  void set_enable_global_bypass(bool enable) { enable_global_bypass_ = enable; }
  void set_cache_size(size_t size) { cache_size_ = size; }
  void set_auto_http_intercept(bool enable) { auto_http_intercept_ = enable; }
  
  // Gestion des fichiers
  void add_file(const std::string &path, size_t chunk_size = 1024);
  void add_file_with_id(const std::string &id, const std::string &path, size_t chunk_size = 1024);
  
  // Accès direct aux fichiers SD
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool file_exists_direct(const std::string &path);
  size_t get_file_size_direct(const std::string &path);
  
  // Streaming de fichiers
  void stream_file_direct(const std::string &path, std::function<void(const uint8_t*, size_t)> callback);
  void stream_file_chunked(const std::string &path, size_t chunk_size, std::function<void(const uint8_t*, size_t)> callback);
  
  // Gestion du cache
  void clear_cache();
  void remove_from_cache(const std::string &path);
  size_t get_cache_usage() const;
  
  // Getters pour configuration
  bool get_enable_global_bypass() const { return enable_global_bypass_; }
  size_t get_cache_size() const { return cache_size_; }
  bool get_auto_http_intercept() const { return auto_http_intercept_; }
  const std::string& get_platform_name() const { return platform_name_; }
  const std::vector<FileConfig>& get_configured_files() const { return configured_files_; }
  
  // Méthodes pour statistiques
  uint32_t get_cache_hits() const { return cache_hits_; }
  uint32_t get_cache_misses() const { return cache_misses_; }
  uint32_t get_direct_reads() const { return direct_reads_; }
  
 protected:
  // Configuration
  std::string platform_name_{"sd_direct"};
  esphome::sd_mmc_card::SDMMCCard *sd_card_{nullptr};
  bool enable_global_bypass_{false};
  size_t cache_size_{32768};
  bool auto_http_intercept_{false};
  
  // Fichiers configurés
  std::vector<FileConfig> configured_files_;
  
  // Cache système
  std::map<std::string, CacheEntry> file_cache_;
  size_t current_cache_size_{0};
  
  // Statistiques
  uint32_t cache_hits_{0};
  uint32_t cache_misses_{0};
  uint32_t direct_reads_{0};
  
  // Méthodes internes
  void setup_sd_access_();
  void setup_cache_system_();
  void setup_http_interception_();
  
  // Gestion du cache
  bool is_cached(const std::string &path) const;
  void add_to_cache(const std::string &path, const std::vector<uint8_t> &data);
  std::vector<uint8_t> get_from_cache(const std::string &path);
  void cleanup_cache_();
  
  // Lecture SD interne
  std::vector<uint8_t> read_file_from_sd_(const std::string &path);
  bool check_file_exists_sd_(const std::string &path);
  size_t get_file_size_sd_(const std::string &path);
  
  // Utilitaires
  std::string normalize_path_(const std::string &path);
  bool is_valid_path_(const std::string &path);
};

}  // namespace storage
}  // namespace esphome



















