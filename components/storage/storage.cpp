#include "storage.h"
#include "storage_actions.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"

#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>

namespace esphome {
namespace storage {

static const char *const TAG = "storage";

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  
  // Vérifier la configuration de base
  if (!this->sd_card_) {
    ESP_LOGE(TAG, "No SD card component configured!");
    this->mark_failed();
    return;
  }
  
  // Attendre que la SD soit prête
  if (!this->sd_card_->is_mounted()) {
    ESP_LOGW(TAG, "SD card not mounted yet, retrying...");
    this->set_timeout(1000, [this]() { this->setup(); });
    return;
  }
  
  // Setup des différents systèmes
  this->setup_sd_access_();
  this->setup_cache_system_();
  
  // Setup de l'interception HTTP si demandée
  if (this->auto_http_intercept_) {
    ESP_LOGI(TAG, "HTTP interception enabled - setting up web server handlers...");
    this->set_timeout(2000, [this]() {
      this->setup_http_interception_();
    });
  }
  
  ESP_LOGCONFIG(TAG, "Storage Component setup complete. Platform: %s, Files: %d, HTTP Intercept: %s", 
                this->platform_name_.c_str(), 
                this->configured_files_.size(),
                this->auto_http_intercept_ ? "ENABLED" : "DISABLED");
}

void StorageComponent::loop() {
  // Nettoyage périodique du cache
  static uint32_t last_cleanup = 0;
  if (millis() - last_cleanup > 60000) { // Toutes les minutes
    this->cleanup_cache_();
    last_cleanup = millis();
  }
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Global Bypass: %s", YESNO(this->enable_global_bypass_));
  ESP_LOGCONFIG(TAG, "  Cache Size: %zu bytes", this->cache_size_);
  ESP_LOGCONFIG(TAG, "  HTTP Interception: %s", YESNO(this->auto_http_intercept_));
  ESP_LOGCONFIG(TAG, "  Configured Files: %d", this->configured_files_.size());
  
  for (const auto& file : this->configured_files_) {
    ESP_LOGCONFIG(TAG, "    - ID: %s, Path: %s, Chunk: %zu", 
                  file.id.c_str(), file.path.c_str(), file.chunk_size);
  }
  
  ESP_LOGCONFIG(TAG, "  Statistics:");
  ESP_LOGCONFIG(TAG, "    Cache Hits: %u", this->cache_hits_);
  ESP_LOGCONFIG(TAG, "    Cache Misses: %u", this->cache_misses_);
  ESP_LOGCONFIG(TAG, "    Direct Reads: %u", this->direct_reads_);
  ESP_LOGCONFIG(TAG, "    Cache Usage: %zu/%zu bytes", this->current_cache_size_, this->cache_size_);
}

void StorageComponent::add_file(const std::string &path, size_t chunk_size) {
  std::string id = "file_" + std::to_string(this->configured_files_.size());
  this->add_file_with_id(id, path, chunk_size);
}

void StorageComponent::add_file_with_id(const std::string &id, const std::string &path, size_t chunk_size) {
  FileConfig config(id, this->normalize_path_(path), chunk_size);
  this->configured_files_.push_back(config);
  ESP_LOGD(TAG, "Added file config: ID=%s, Path=%s, Chunk=%zu", id.c_str(), path.c_str(), chunk_size);
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  std::string normalized_path = this->normalize_path_(path);
  
  // Vérifier le cache d'abord
  if (!this->enable_global_bypass_ && this->is_cached(normalized_path)) {
    this->cache_hits_++;
    ESP_LOGD(TAG, "Cache hit for: %s", normalized_path.c_str());
    return this->get_from_cache(normalized_path);
  }
  
  // Lecture directe depuis SD
  this->cache_misses_++;
  this->direct_reads_++;
  auto data = this->read_file_from_sd_(normalized_path);
  
  // Ajouter au cache si pas en mode bypass
  if (!this->enable_global_bypass_ && !data.empty()) {
    this->add_to_cache(normalized_path, data);
  }
  
  ESP_LOGD(TAG, "Read file direct: %s (%zu bytes)", normalized_path.c_str(), data.size());
  return data;
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  std::string normalized_path = this->normalize_path_(path);
  return this->check_file_exists_sd_(normalized_path);
}

size_t StorageComponent::get_file_size_direct(const std::string &path) {
  std::string normalized_path = this->normalize_path_(path);
  return this->get_file_size_sd_(normalized_path);
}

void StorageComponent::stream_file_direct(const std::string &path, std::function<void(const uint8_t*, size_t)> callback) {
  this->stream_file_chunked(path, 1024, callback);
}

void StorageComponent::stream_file_chunked(const std::string &path, size_t chunk_size, std::function<void(const uint8_t*, size_t)> callback) {
  std::string normalized_path = this->normalize_path_(path);
  
  ESP_LOGD(TAG, "Streaming file: %s (chunk size: %zu)", normalized_path.c_str(), chunk_size);
  
  FILE* file = fopen(normalized_path.c_str(), "rb");
  if (!file) {
    ESP_LOGW(TAG, "Failed to open file for streaming: %s", normalized_path.c_str());
    return;
  }
  
  std::vector<uint8_t> buffer(chunk_size);
  size_t total_read = 0;
  
  while (true) {
    size_t bytes_read = fread(buffer.data(), 1, chunk_size, file);
    if (bytes_read == 0) break;
    
    callback(buffer.data(), bytes_read);
    total_read += bytes_read;
    
    if (bytes_read < chunk_size) break; // EOF
  }
  
  fclose(file);
  ESP_LOGD(TAG, "Streaming complete: %s (%zu bytes total)", normalized_path.c_str(), total_read);
}

void StorageComponent::clear_cache() {
  this->file_cache_.clear();
  this->current_cache_size_ = 0;
  ESP_LOGI(TAG, "Cache cleared");
}

void StorageComponent::remove_from_cache(const std::string &path) {
  std::string normalized_path = this->normalize_path_(path);
  auto it = this->file_cache_.find(normalized_path);
  if (it != this->file_cache_.end()) {
    this->current_cache_size_ -= it->second.size;
    this->file_cache_.erase(it);
    ESP_LOGD(TAG, "Removed from cache: %s", normalized_path.c_str());
  }
}

size_t StorageComponent::get_cache_usage() const {
  return this->current_cache_size_;
}

// Méthodes protégées

void StorageComponent::setup_sd_access_() {
  ESP_LOGCONFIG(TAG, "Configuring SD direct access...");
  
  for (const auto& file_config : this->configured_files_) {
    if (!this->file_exists_direct(file_config.path)) {
      ESP_LOGW(TAG, "Configured file not found: %s", file_config.path.c_str());
    } else {
      ESP_LOGD(TAG, "Configured file %s for SD direct access", file_config.path.c_str());
    }
  }
  
  if (this->enable_global_bypass_) {
    ESP_LOGI(TAG, "SD direct access enabled - files read directly from SD without flash usage");
  }
}

void StorageComponent::setup_cache_system_() {
  ESP_LOGD(TAG, "Setting up cache system (size: %zu bytes)", this->cache_size_);
  this->file_cache_.clear();
  this->current_cache_size_ = 0;
}

void StorageComponent::setup_http_interception_() {
  ESP_LOGI(TAG, "Initializing HTTP interception for SD files...");
  
  try {
    // Utiliser la factory pour setup l'interception
    StorageActionFactory::setup_http_interception(this);
    ESP_LOGI(TAG, "HTTP interception setup successful!");
  } catch (const std::exception& e) {
    ESP_LOGW(TAG, "Failed to setup HTTP interception: %s", e.what());
  } catch (...) {
    ESP_LOGW(TAG, "Failed to setup HTTP interception: unknown error");
  }
}

bool StorageComponent::is_cached(const std::string &path) const {
  return this->file_cache_.find(path) != this->file_cache_.end();
}

void StorageComponent::add_to_cache(const std::string &path, const std::vector<uint8_t> &data) {
  if (data.size() > this->cache_size_) {
    ESP_LOGD(TAG, "File too large for cache: %s (%zu bytes)", path.c_str(), data.size());
    return;
  }
  
  // Nettoyer le cache si nécessaire
  while (this->current_cache_size_ + data.size() > this->cache_size_ && !this->file_cache_.empty()) {
    this->cleanup_cache_();
  }
  
  this->file_cache_[path] = CacheEntry(data);
  this->current_cache_size_ += data.size();
  ESP_LOGD(TAG, "Added to cache: %s (%zu bytes)", path.c_str(), data.size());
}

std::vector<uint8_t> StorageComponent::get_from_cache(const std::string &path) {
  auto it = this->file_cache_.find(path);
  if (it != this->file_cache_.end()) {
    it->second.last_access = millis();
    return it->second.data;
  }
  return {};
}

void StorageComponent::cleanup_cache_() {
  if (this->file_cache_.empty()) return;
  
  // Trouver l'entrée la plus ancienne
  auto oldest = this->file_cache_.begin();
  for (auto it = this->file_cache_.begin(); it != this->file_cache_.end(); ++it) {
    if (it->second.last_access < oldest->second.last_access) {
      oldest = it;
    }
  }
  
  ESP_LOGD(TAG, "Removing oldest cache entry: %s", oldest->first.c_str());
  this->current_cache_size_ -= oldest->second.size;
  this->file_cache_.erase(oldest);
}

std::vector<uint8_t> StorageComponent::read_file_from_sd_(const std::string &path) {
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    ESP_LOGW(TAG, "Failed to open file: %s", path.c_str());
    return {};
  }
  
  // Obtenir la taille du fichier
  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  if (file_size == 0) {
    fclose(file);
    ESP_LOGW(TAG, "Empty file: %s", path.c_str());
    return {};
  }
  
  // Lire le fichier
  std::vector<uint8_t> data(file_size);
  size_t bytes_read = fread(data.data(), 1, file_size, file);
  fclose(file);
  
  if (bytes_read != file_size) {
    ESP_LOGW(TAG, "Failed to read complete file: %s (%zu/%zu bytes)", path.c_str(), bytes_read, file_size);
    return {};
  }
  
  return data;
}

bool StorageComponent::check_file_exists_sd_(const std::string &path) {
  struct stat st;
  return (stat(path.c_str(), &st) == 0);
}

size_t StorageComponent::get_file_size_sd_(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    return st.st_size;
  }
  return 0;
}

std::string StorageComponent::normalize_path_(const std::string &path) {
  std::string normalized = path;
  
  // S'assurer que le chemin commence par /
  if (!normalized.empty() && normalized[0] != '/') {
    normalized = "/" + normalized;
  }
  
  // Remplacer les doubles slashes
  size_t pos = 0;
  while ((pos = normalized.find("//", pos)) != std::string::npos) {
    normalized.replace(pos, 2, "/");
  }
  
  return normalized;
}

bool StorageComponent::is_valid_path_(const std::string &path) {
  if (path.empty()) return false;
  if (path.find("..") != std::string::npos) return false; // Sécurité
  return true;
}

}  // namespace storage
}  // namespace esphome









