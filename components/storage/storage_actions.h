#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "storage.h"
#include "../sd_mmc_card/sd_mmc_card.h"

namespace esphome {
namespace storage {

// Action pour streaming direct de fichiers (classe de base existante)
template<typename... Ts> 
class StorageStreamFileAction : public Action<Ts...> {
 public:
  StorageStreamFileAction(StorageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  TEMPLATABLE_VALUE(size_t, chunk_size)
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    auto chunk_size = this->chunk_size_.value(x...);
    
    ESP_LOGD("storage_action", "Streaming file %s with chunk size %zu", path.c_str(), chunk_size);
    
    // Stream direct depuis SD sans buffer en RAM/Flash 
    this->parent_->stream_file_direct(path, [this, chunk_size](const uint8_t* data, size_t len) {
      // Callback de traitement du chunk
      this->on_chunk_received(data, len);
    });
  }
  
  // Callback virtuel pour traitement personnalisé des chunks
  virtual void on_chunk_received(const uint8_t* data, size_t len) {
    // Par défaut: log du chunk reçu
    ESP_LOGD("storage_action", "Received chunk: %zu bytes", len);
  }
  
  void set_parent(StorageComponent *parent) { parent_ = parent; }

 protected:
  StorageComponent *parent_;
};

// Action pour streaming HTTP simulé depuis SD (déclaration complète)
template<typename... Ts>
class StorageHTTPStreamAction : public StorageStreamFileAction<Ts...> {
 public:
  StorageHTTPStreamAction(StorageComponent *parent) : StorageStreamFileAction<Ts...>(parent) {}
  
  TEMPLATABLE_VALUE(std::string, endpoint_url)
  TEMPLATABLE_VALUE(uint16_t, port)
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    
    // Si endpoint_url n'est pas défini, on en génère un automatiquement
    std::string endpoint = "/stream/" + this->extract_filename(path);
    uint16_t port = 80;
    
    if (this->endpoint_url_.has_value()) {
      endpoint = this->endpoint_url_.value(x...);
    }
    if (this->port_.has_value()) {
      port = this->port_.value(x...);
    }
    
    ESP_LOGD("storage_http", "Starting HTTP stream for %s on %s:%d", 
             path.c_str(), endpoint.c_str(), port);
    
    // Simuler un streaming HTTP
    this->stream_with_http_headers(path, endpoint);
  }
  
  void on_chunk_received(const uint8_t* data, size_t len) override {
    // Traitement spécifique HTTP
    if (this->http_callback_) {
      this->http_callback_(data, len);
    } else {
      // Fallback vers comportement parent
      StorageStreamFileAction<Ts...>::on_chunk_received(data, len);
    }
  }
  
  void set_http_callback(std::function<void(const uint8_t*, size_t)> callback) {
    http_callback_ = callback;
  }
  
  void set_web_server(void *server) {
    web_server_base_ = server;
  }
  
 protected:
  void stream_with_http_headers(const std::string& file_path, const std::string& endpoint) {
    // Générer headers HTTP
    std::string http_headers = this->generate_http_headers(file_path);
    
    ESP_LOGD("storage_http", "HTTP endpoint ready: %s", endpoint.c_str());
    ESP_LOGD("storage_http", "Headers: %s", http_headers.c_str());
    
    // Simuler l'envoi des headers
    if (this->http_callback_) {
      this->http_callback_(reinterpret_cast<const uint8_t*>(http_headers.c_str()), 
                          http_headers.length());
    }
    
    // Stream le fichier depuis SD
    StorageStreamFileAction<Ts...>::play();
  }
  
  std::string generate_http_headers(const std::string& file_path) {
    std::string content_type = this->get_mime_type(file_path);
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Transfer-Encoding: chunked\r\n"
           "\r\n";
  }
  
  std::string get_mime_type(const std::string& file_path) {
    if (file_path.find(".jpg") != std::string::npos || 
        file_path.find(".jpeg") != std::string::npos) return "image/jpeg";
    if (file_path.find(".png") != std::string::npos) return "image/png";
    if (file_path.find(".gif") != std::string::npos) return "image/gif";
    if (file_path.find(".bmp") != std::string::npos) return "image/bmp";
    if (file_path.find(".webp") != std::string::npos) return "image/webp";
    return "application/octet-stream";
  }
  
  std::string extract_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
  }

 private:
  std::function<void(const uint8_t*, size_t)> http_callback_;
  void *web_server_base_{nullptr};
};

// Action pour lecture complète de fichier
template<typename... Ts>
class StorageReadFileAction : public Action<Ts...> {
 public:
  StorageReadFileAction(StorageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  TEMPLATABLE_VALUE(size_t, max_size)
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    auto max_size = this->max_size_.value(x...);
    
    ESP_LOGD("storage_action", "Reading file %s (max size: %zu)", path.c_str(), max_size);
    
    // Lecture directe depuis SD
    auto file_data = this->parent_->read_file_direct(path);
    
    // Limiter la taille si spécifiée
    if (max_size > 0 && file_data.size() > max_size) {
      file_data.resize(max_size);
    }
    
    if (!file_data.empty()) {
      this->on_file_read(file_data);
    } else {
      ESP_LOGW("storage_action", "Failed to read file %s", path.c_str());
    }
  }
  
  // Callback virtuel pour traitement du fichier lu
  virtual void on_file_read(const std::vector<uint8_t>& data) {
    ESP_LOGD("storage_action", "File read successfully: %zu bytes", data.size());
  }
  
  void set_parent(StorageComponent *parent) { parent_ = parent; }

 protected:
  StorageComponent *parent_;
};

// Action spécialisée pour streaming audio
template<typename... Ts>
class StorageStreamAudioAction : public StorageStreamFileAction<Ts...> {
 public:
  StorageStreamAudioAction(StorageComponent *parent) : StorageStreamFileAction<Ts...>(parent) {}
  
  void on_chunk_received(const uint8_t* data, size_t len) override {
    // Streaming optimisé pour audio - envoi direct vers I2S
    if (this->audio_callback_) {
      this->audio_callback_(data, len);
    }
  }
  
  void set_audio_callback(std::function<void(const uint8_t*, size_t)> callback) {
    audio_callback_ = callback;
  }

 private:
  std::function<void(const uint8_t*, size_t)> audio_callback_;
};

// Action pour gestion d'images depuis SD avec simulation HTTP
template<typename... Ts>
class StorageStreamImageAction : public StorageHTTPStreamAction<Ts...> {
 public:
  StorageStreamImageAction(StorageComponent *parent) : StorageHTTPStreamAction<Ts...>(parent) {}
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    
    ESP_LOGD("storage_http_image", "Streaming image %s via simulated HTTP", path.c_str());
    
    // Générer endpoint automatique pour l'image
    std::string image_endpoint = "/image/stream/" + this->generate_image_id(path);
    
    // Configurer l'endpoint
    if (!this->endpoint_url_.has_value()) {
      this->endpoint_url_ = image_endpoint;
    }
    
    // Appeler la méthode parent avec simulation HTTP
    StorageHTTPStreamAction<Ts...>::play(x...);
  }
  
  void on_chunk_received(const uint8_t* data, size_t len) override {
    // Traitement optimisé pour images avec headers HTTP
    if (this->image_callback_) {
      this->image_callback_(data, len);
    } else {
      // Fallback vers traitement HTTP standard
      StorageHTTPStreamAction<Ts...>::on_chunk_received(data, len);
    }
  }
  
  void set_image_callback(std::function<void(const uint8_t*, size_t)> callback) {
    image_callback_ = callback;
  }

 private:
  std::function<void(const uint8_t*, size_t)> image_callback_;
  
  std::string generate_image_id(const std::string& path) {
    // Générer un ID unique et lisible pour l'image
    std::string filename = this->extract_filename(path);
    
    // Retirer l'extension
    size_t dot_pos = filename.find_last_of(".");
    if (dot_pos != std::string::npos) {
      filename = filename.substr(0, dot_pos);
    }
    
    // Remplacer les caractères spéciaux
    for (char& c : filename) {
      if (!std::isalnum(c)) c = '_';
    }
    
    return filename;
  }
};

// Action pour vérification d'existence de fichier
template<typename... Ts>
class StorageFileExistsAction : public Action<Ts...> {
 public:
  StorageFileExistsAction(StorageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    
    bool exists = this->parent_->file_exists_direct(path);
    ESP_LOGD("storage_action", "File %s exists: %s", path.c_str(), exists ? "YES" : "NO");
    
    if (this->exists_callback_) {
      this->exists_callback_(exists);
    }
  }
  
  void set_exists_callback(std::function<void(bool)> callback) {
    exists_callback_ = callback;
  }
  
  void set_parent(StorageComponent *parent) { parent_ = parent; }

 protected:
  StorageComponent *parent_;
  std::function<void(bool)> exists_callback_;
};

// Factory pour créer les actions
class StorageActionFactory {
 public:
  static std::unique_ptr<StorageStreamFileAction<>> create_stream_action(StorageComponent *parent) {
    return std::make_unique<StorageStreamFileAction<>>(parent);
  }
  
  static std::unique_ptr<StorageReadFileAction<>> create_read_action(StorageComponent *parent) {
    return std::make_unique<StorageReadFileAction<>>(parent);
  }
  
  static std::unique_ptr<StorageStreamAudioAction<>> create_audio_action(StorageComponent *parent) {
    return std::make_unique<StorageStreamAudioAction<>>(parent);
  }
  
  static std::unique_ptr<StorageStreamImageAction<>> create_image_action(StorageComponent *parent) {
    return std::make_unique<StorageStreamImageAction<>>(parent);
  }
  
  static std::unique_ptr<StorageHTTPStreamAction<>> create_http_stream_action(StorageComponent *parent) {
    return std::make_unique<StorageHTTPStreamAction<>>(parent);
  }
};

}  // namespace storage
}  // namespace esphome




