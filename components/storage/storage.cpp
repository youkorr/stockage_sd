#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <ESPAsyncWebServer.h>

namespace esphome {
namespace storage {

static const char *const TAG = "storage";

// Instance globale pour hooks
StorageComponent* StorageComponent::global_instance_ = nullptr;

// ===========================================
// Implémentation StorageFile avec SdMmc existant
// ===========================================

void StorageFile::stream_direct(std::function<void(const uint8_t*, size_t)> callback) {
  if (!is_sd_direct() || !sd_component_) {
    ESP_LOGE(TAG, "SD direct not available for file %s", path_.c_str());
    return;
  }
  
  ESP_LOGD(TAG, "Streaming file %s directly from SD", path_.c_str());
  
  // Utilise la méthode read_file_stream existante
  sd_component_->read_file_stream(path_.c_str(), 0, chunk_size_, callback);
}

void StorageFile::stream_chunked_direct(std::function<void(const uint8_t*, size_t)> callback) {
  if (!is_sd_direct() || !sd_component_) {
    ESP_LOGE(TAG, "SD direct not available for file %s", path_.c_str());
    return;
  }
  
  // Stream par chunks en utilisant la méthode read_file_chunked existante
  size_t offset = 0;
  size_t file_size = get_file_size_direct();
  
  ESP_LOGD(TAG, "Streaming file %s in chunks of %u bytes", path_.c_str(), chunk_size_);
  
  while (offset < file_size) {
    size_t current_chunk = std::min((size_t)chunk_size_, file_size - offset);
    auto chunk_data = sd_component_->read_file_chunked(path_, offset, current_chunk);
    
    if (chunk_data.empty()) {
      ESP_LOGE(TAG, "Failed to read chunk at offset %zu", offset);
      break;
    }
    
    // Callback direct - pas de stockage en RAM
    callback(chunk_data.data(), chunk_data.size());
    offset += current_chunk;
  }
}

std::vector<uint8_t> StorageFile::read_direct() {
  if (!is_sd_direct() || !sd_component_) {
    return {};
  }
  
  ESP_LOGD(TAG, "Reading file %s directly from SD", path_.c_str());
  return sd_component_->read_file(path_);
}

bool StorageFile::read_audio_chunk(size_t offset, uint8_t* buffer, size_t buffer_size, size_t& bytes_read) {
  if (!is_sd_direct() || !sd_component_) {
    return false;
  }
  
  // Lecture directe chunk par chunk pour audio en utilisant la méthode existante
  auto chunk_data = sd_component_->read_file_chunked(path_, offset, buffer_size);
  bytes_read = chunk_data.size();
  
  if (bytes_read > 0) {
    memcpy(buffer, chunk_data.data(), bytes_read);
    current_position_ = offset + bytes_read;
    return true;
  }
  
  return false;
}

size_t StorageFile::get_file_size_direct() const {
  if (!file_size_cached_) {
    if (is_sd_direct() && sd_component_) {
      cached_file_size_ = sd_component_->file_size(path_);
    } else {
      cached_file_size_ = 0;
    }
    file_size_cached_ = true;
  }
  return cached_file_size_;
}

bool StorageFile::file_exists_direct() const {
  if (!is_sd_direct() || !sd_component_) {
    return false;
  }
  
  return sd_component_->file_size(path_) > 0;
}

std::string StorageFile::get_http_url() const {
  // Extraire le nom du fichier du chemin
  std::string filename = path_;
  size_t pos = path_.find_last_of("/\\");
  if (pos != std::string::npos) {
    filename = path_.substr(pos + 1);
  }
  
  // Générer une URL HTTP
  return "/sd/" + filename;
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
    
    // Enregistrer automatiquement tous les fichiers comme ressources HTTP
    if (web_server_ != nullptr) {
      for (auto *file : files_) {
        std::string path = file->get_path();
        std::string url_path = file->get_http_url();
        register_http_resource(path, url_path);
        
        ESP_LOGD(TAG, "Auto-registered file %s as HTTP resource at %s", path.c_str(), url_path.c_str());
      }
      
      // Configurer les gestionnaires HTTP
      setup_http_handlers(web_server_);
    }
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
  
  // Vérifier si la carte SD est montée
  if (!sd_component_->is_mounted()) {
    ESP_LOGD(TAG, "SD card not mounted, attempting to mount...");
    if (!sd_component_->mount()) {
      ESP_LOGE(TAG, "Failed to mount SD card!");
      return;
    }
  }
  
  ESP_LOGD(TAG, "SD card mounted successfully");
  
  // Configurer tous les fichiers pour SD direct
  for (auto *file : files_) {
    file->set_sd_component(sd_component_);
    file->set_platform("sd_direct");
    ESP_LOGD(TAG, "Configured file %s for SD direct access", file->get_path().c_str());
    
    // Vérifier si le fichier existe
    if (file->file_exists_direct()) {
      ESP_LOGD(TAG, "File exists: %s, size: %d bytes", 
               file->get_path().c_str(), file->get_file_size_direct());
    } else {
      ESP_LOGW(TAG, "File does not exist: %s", file->get_path().c_str());
    }
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

StorageFile* StorageComponent::get_file_by_id(const std::string &id) {
  for (auto *file : files_) {
    if (file->get_id() == id) {
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
  
  // Utilise la méthode read_file_stream existante
  sd_component_->read_file_stream(path.c_str(), 0, 1024, callback);
}

std::string StorageComponent::get_http_url_for_file(const std::string &file_id) const {
  StorageFile *file = nullptr;
  for (auto *f : files_) {
    if (f->get_id() == file_id) {
      file = f;
      break;
    }
  }
  
  if (!file) {
    return "";
  }
  
  // Construire l'URL complète
  return "http://" + get_base_url() + file->get_http_url();
}

void StorageComponent::setup_http_handlers(web_server_base::WebServerBase *web_server) {
  this->web_server_ = web_server;
  
  // Récupérer le serveur AsyncWebServer sous-jacent
  AsyncWebServer *server = web_server->get_server();
  if (!server) {
    ESP_LOGE(TAG, "Failed to get AsyncWebServer instance");
    return;
  }
  
  // Enregistrer un gestionnaire pour toutes les requêtes /sd/*
  server->on("^\\/sd\\/(.+)$", HTTP_GET, [this](AsyncWebServerRequest *request) {
    std::string url_path = request->url().c_str();
    ESP_LOGD(TAG, "HTTP request for %s", url_path.c_str());
    
    // Extraire le nom du fichier de l'URL
    std::string filename = url_path.substr(4); // Enlever "/sd/"
    
    // Essayer différentes variantes de chemins
    std::vector<std::string> path_variants = {
      "/" + filename,
      "/sdcard/" + filename,
      filename
    };
    
    std::string file_path;
    for (const auto &path : path_variants) {
      if (this->file_exists_direct(path)) {
        file_path = path;
        break;
      }
    }
    
    if (file_path.empty()) {
      ESP_LOGW(TAG, "No file found for URL %s", url_path.c_str());
      request->send(404, "text/plain", "File not found");
      return;
    }
    
    ESP_LOGD(TAG, "Serving file %s for URL %s", file_path.c_str(), url_path.c_str());
    
    // Lire le fichier depuis la SD
    auto data = this->read_file_direct(file_path);
    if (data.empty()) {
      ESP_LOGW(TAG, "Failed to read file %s", file_path.c_str());
      request->send(404, "text/plain", "File not found or empty");
      return;
    }
    
    // Déterminer le type MIME
    std::string content_type = "application/octet-stream";
    if (file_path.find(".jpg") != std::string::npos || file_path.find(".jpeg") != std::string::npos) {
      content_type = "image/jpeg";
    } else if (file_path.find(".png") != std::string::npos) {
      content_type = "image/png";
    } else if (file_path.find(".mp3") != std::string::npos) {
      content_type = "audio/mpeg";
    } else if (file_path.find(".wav") != std::string::npos) {
      content_type = "audio/wav";
    } else if (file_path.find(".gif") != std::string::npos) {
      content_type = "image/gif";
    } else if (file_path.find(".bmp") != std::string::npos) {
      content_type = "image/bmp";
    }
    
    // Envoyer la réponse
    AsyncResponseStream *response = request->beginResponseStream(content_type.c_str());
    response->write(data.data(), data.size());
    request->send(response);
  });
  
  ESP_LOGI(TAG, "HTTP handlers for SD card files registered");
}

void StorageComponent::register_http_resource(const std::string &path, const std::string &url_path) {
  this->http_resources_[path] = url_path;
  ESP_LOGD(TAG, "Registered HTTP resource: %s -> %s", path.c_str(), url_path.c_str());
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

}  // namespace storage
}  // namespace esphome









