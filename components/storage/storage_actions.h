// storage_actions.h - Actions pour automatisation avec bypass SD et interception HTTP

#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "storage.h"

namespace esphome {
namespace storage {

// Action pour streaming direct de fichiers
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

// Action pour gestion d'images depuis SD
template<typename... Ts>
class StorageStreamImageAction : public StorageStreamFileAction<Ts...> {
 public:
  StorageStreamImageAction(StorageComponent *parent) : StorageStreamFileAction<Ts...>(parent) {}
  
  void on_chunk_received(const uint8_t* data, size_t len) override {
    // Traitement optimisé pour images - decode direct
    if (this->image_callback_) {
      this->image_callback_(data, len);
    }
  }
  
  void set_image_callback(std::function<void(const uint8_t*, size_t)> callback) {
    image_callback_ = callback;
  }

 private:
  std::function<void(const uint8_t*, size_t)> image_callback_;
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

// NOUVEAU: Intercepteur HTTP automatique pour ESPHome
class StorageHTTPInterceptor {
 public:
  static void setup_automatic_interception(StorageComponent *storage) {
    ESP_LOGI("storage_interceptor", "Setting up automatic HTTP interception for ESPHome images");
    
    // Obtenir le serveur web d'ESPHome
    auto *web_server = App.get_web_server();
    if (!web_server) {
      ESP_LOGW("storage_interceptor", "No web server found, creating one");
      // ESPHome créera automatiquement un serveur web si nécessaire
      return;
    }
    
    install_localhost_handlers(storage, web_server);
    install_image_handlers(storage, web_server);
  }
  
 private:
  static void install_localhost_handlers(StorageComponent *storage, web_server_base::WebServerBase *web_server) {
    // Intercepter toutes les requêtes localhost pour images
    web_server->add_handler("/img/*", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_image_request(storage, request, "/scard/img");
    });
    
    web_server->add_handler("/sd/*", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_image_request(storage, request, "/scard");
    });
    
    web_server->add_handler("/scard/*", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_image_request(storage, request, "");
    });
  }
  
  static void install_image_handlers(StorageComponent *storage, web_server_base::WebServerBase *web_server) {
    // Handler générique pour tous les fichiers images
    web_server->add_handler("*.jpg", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_any_image_request(storage, request);
    });
    
    web_server->add_handler("*.jpeg", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_any_image_request(storage, request);
    });
    
    web_server->add_handler("*.png", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_any_image_request(storage, request);
    });
    
    web_server->add_handler("*.bmp", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_any_image_request(storage, request);
    });
    
    web_server->add_handler("*.gif", HTTP_GET, [storage](AsyncWebServerRequest *request) {
      handle_any_image_request(storage, request);
    });
  }
  
  static void handle_image_request(StorageComponent *storage, AsyncWebServerRequest *request, const String &base_path) {
    String url_path = request->url();
    String full_path = base_path + url_path;
    
    ESP_LOGD("storage_interceptor", "Image request: %s -> %s", url_path.c_str(), full_path.c_str());
    
    serve_file_from_sd(storage, request, full_path);
  }
  
  static void handle_any_image_request(StorageComponent *storage, AsyncWebServerRequest *request) {
    String url_path = request->url();
    
    // Essayer plusieurs emplacements possibles
    std::vector<String> possible_paths = {
      "/scard" + url_path,
      "/scard/img" + url_path,
      "/scard/images" + url_path
    };
    
    for (const auto &path : possible_paths) {
      if (storage->file_exists_direct(path.c_str())) {
        ESP_LOGD("storage_interceptor", "Found image at: %s", path.c_str());
        serve_file_from_sd(storage, request, path);
        return;
      }
    }
    
    ESP_LOGW("storage_interceptor", "Image not found: %s", url_path.c_str());
    request->send(404, "text/plain", "Image not found on SD card");
  }
  
  static void serve_file_from_sd(StorageComponent *storage, AsyncWebServerRequest *request, const String &file_path) {
    if (!storage->file_exists_direct(file_path.c_str())) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    
    // Lire le fichier depuis SD
    auto file_data = storage->read_file_direct(file_path.c_str());
    if (file_data.empty()) {
      request->send(500, "text/plain", "Failed to read file from SD");
      return;
    }
    
    // Déterminer le type MIME
    String content_type = get_mime_type(file_path);
    
    // Créer la réponse
    AsyncWebServerResponse *response = request->beginResponse_P(
      200, 
      content_type.c_str(), 
      (const uint8_t*)file_data.data(), 
      file_data.size()
    );
    
    // Headers d'optimisation
    response->addHeader("Cache-Control", "public, max-age=3600");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Content-Length", String(file_data.size()));
    
    request->send(response);
    
    ESP_LOGD("storage_interceptor", "Served: %s (%zu bytes, %s)", 
             file_path.c_str(), file_data.size(), content_type.c_str());
  }
  
  static String get_mime_type(const String &path) {
    // Utiliser une approche compatible avec std::string
    std::string path_str = path.c_str();
    
    // Fonction helper pour vérifier l'extension
    auto has_extension = [&path_str](const std::string& ext) {
      if (path_str.length() < ext.length()) return false;
      return path_str.compare(path_str.length() - ext.length(), ext.length(), ext) == 0;
    };
    
    if (has_extension(".jpg") || has_extension(".jpeg")) return "image/jpeg";
    if (has_extension(".png")) return "image/png";
    if (has_extension(".gif")) return "image/gif";
    if (has_extension(".bmp")) return "image/bmp";
    if (has_extension(".webp")) return "image/webp";
    if (has_extension(".svg")) return "image/svg+xml";
    return "application/octet-stream";
  }
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
  
  // NOUVEAU: Setup automatique de l'interception
  static void setup_http_interception(StorageComponent *parent) {
    StorageHTTPInterceptor::setup_automatic_interception(parent);
  }
};

}  // namespace storage
}  // namespace esphome
