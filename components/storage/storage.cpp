#include "storage.h"
#include "esphome/core/log.h"

namespace esphome {
namespace storage {

static const char *const TAG = "storage";

// Instance globale pour hooks
StorageComponent* StorageComponent::global_instance_ = nullptr;

// ===========================================
// Implémentation StorageFile avec bypass SD
// ===========================================

void StorageFile::stream_direct(std::function<void(const uint8_t*, size_t)> callback) {
  if (!is_sd_direct() || !sd_component_) {
    ESP_LOGE(TAG, "SD direct not available for file %s", path_.c_str());
    return;
  }
  
  ESP_LOGD(TAG, "Streaming file %s directly from SD", path_.c_str());
  sd_component_->stream_file_direct_chunked(path_, chunk_size_, callback);
}

void StorageFile::stream_chunked_direct(std::function<void(const uint8_t*, size_t)> callback) {
  if (!is_sd_direct() || !sd_component_) {
    ESP_LOGE(TAG, "SD direct not available for file %s", path_.c_str());
    return;
  }
  
  // Stream avec la taille de chunk configurée
  sd_component_->read_file_stream(path_.c_str(), 0, chunk_size_, callback);
}

bool StorageFile::read_audio_chunk(size_t offset, uint8_t* buffer, size_t buffer_size, size_t& bytes_read) {
  if (!is_sd_direct() || !sd_component_) {
    return false;
  }
  
  // Lecture directe chunk par chunk pour audio
  auto chunk_data = sd_component_->read_file_chunked(path_, offset, buffer_size);
  bytes_read = chunk_data.size();
  
  if (bytes_read > 0) {
    memcpy(buffer, chunk_data.data(), bytes_read);
    current_position_ = offset + bytes_read;
    return true;
  }
  
  return false;
}

// Override des méthodes AudioFile pour bypass complet
size_t StorageFile::get_file_size() const {
  if (!file_size_cached_) {
    if (is_sd_direct() && sd_component_) {
      file_size_ = sd_component_->file_size(path_);
    } else {
      file_size_ = 0;
    }
    file_size_cached_ = true;
  }
  return file_size_;
}

bool StorageFile::seek(size_t position) {
  if (position <= get_file_size()) {
    current_position_ = position;
    return true;
  }
  return false;
}

size_t StorageFile::read(uint8_t* buffer, size_t length) {
  if (!is_sd_direct() || !sd_component_) {
    return 0;
  }
  
  size_t bytes_read = 0;
  if (read_audio_chunk(current_position_, buffer, length, bytes_read)) {
    return bytes_read;
  }
  return 0;
}

bool StorageFile::is_eof() const {
  return current_position_ >= get_file_size();
}

// ===========================================
// Implémentation StorageComponent
// ===========================================

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  
  // Configurer l'instance globale pour hooks
  StorageComponent::set_global_instance(this);
  
  if (platform_ == "sd_card" || platform_ == "sd_direct") {
    setup_sd_direct();
  } else if (platform_ == "flash") {
    setup_flash();
  } else if (platform_ == "inline") {
    setup_inline();
  }
  
  ESP_LOGCONFIG(TAG, "Storage Component setup complete. Platform: %s, Files: %zu", 
                platform_.c_str(), files_.size());
}

void StorageComponent::setup_sd_direct() {
  ESP_LOGCONFIG(TAG, "Configuring SD direct access...");
  
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not set for SD direct platform!");
    return;
  }
  
  // Configurer tous les fichiers pour SD direct
  for (auto *file : files_) {
    file->set_sd_component(sd_component_);
    file->set_platform("sd_direct");
    ESP_LOGD(TAG, "Configured file %s for SD direct access", file->get_path().c_str());
  }
  
  platform_ = "sd_direct";
  ESP_LOGI(TAG, "SD direct access enabled - files read directly from SD without flash usage");
}

void StorageComponent::setup_sd_card() {
  // Ancienne méthode - on redirige vers sd_direct
  ESP_LOGW(TAG, "sd_card platform deprecated, using sd_direct instead");
  setup_sd_direct();
}

void StorageComponent::setup_flash() {
  ESP_LOGCONFIG(TAG, "Using flash storage (embedded files)");
  // Implementation existante pour flash
}

void StorageComponent::setup_inline() {
  ESP_LOGCONFIG(TAG, "Using inline storage");
  // Implementation existante pour inline
}

std::string StorageComponent::get_file_path(const std::string &file_id) const {
  for (const auto *file : files_) {
    if (file->get_id() == file_id) {
      return file->get_path();
    }
  }
  return "";
}

StorageFile* StorageComponent::get_file_by_path(const std::string &path) {
  for (auto *file : files_) {
    if (file->get_path() == path) {
      return file;
    }
  }
  return nullptr;
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  if (!sd_component_) {
    return {};
  }
  
  ESP_LOGD(TAG, "Reading file %s directly from SD", path.c_str());
  return sd_component_->read_file(path);
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  if (!sd_component_) {
    return false;
  }
  
  return sd_component_->file_size(path) > 0;
}

void StorageComponent::stream_file_direct(const std::string &path, std::function<void(const uint8_t*, size_t)> callback) {
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not available for streaming");
    return;
  }
  
  ESP_LOGD(TAG, "Streaming file %s directly from SD", path.c_str());
  sd_component_->stream_file_direct(path, callback);
}

// ===========================================
// Hooks globaux pour bypass ESPHome
// ===========================================

std::vector<uint8_t> StorageGlobalHooks::intercept_file_read(const std::string &path) {
  auto *storage = StorageComponent::get_global_instance();
  if (!storage) return {};
  
  ESP_LOGD(TAG, "Intercepting file read: %s", path.c_str());
  return storage->read_file_direct(path);
}

bool StorageGlobalHooks::intercept_file_exists(const std::string &path) {
  auto *storage = StorageComponent::get_global_instance();
  if (!storage) return false;
  
  return storage->file_exists_direct(path);
}

void StorageGlobalHooks::intercept_file_stream(const std::string &path, std::function<void(const uint8_t*, size_t)> callback) {
  auto *storage = StorageComponent::get_global_instance();
  if (!storage) return;
  
  storage->stream_file_direct(path, callback);
}

bool StorageGlobalHooks::hook_media_player_file(const std::string &media_url, std::function<void(const uint8_t*, size_t)> callback) {
  auto *storage = StorageComponent::get_global_instance();
  if (!storage || !storage->is_global_bypass_enabled()) {
    return false;
  }
  
  // Extraire le chemin du fichier de l'URL
  std::string file_path = media_url;
  if (media_url.find("file://") == 0) {
    file_path = media_url.substr(7); // Enlever "file://"
  }
  
  ESP_LOGI(TAG, "Hooking media player file: %s", file_path.c_str());
  storage->stream_file_direct(file_path, callback);
  return true;
}

bool StorageGlobalHooks::hook_image_file(const std::string &image_path, std::function<void(const uint8_t*, size_t)> callback) {
  auto *storage = StorageComponent::get_global_instance();
  if (!storage || !storage->is_global_bypass_enabled()) {
    return false;
  }
  
  ESP_LOGI(TAG, "Hooking image file: %s", image_path.c_str());
  storage->stream_file_direct(image_path, callback);
  return true;
}

}  // namespace storage
}  // namespace esphome









