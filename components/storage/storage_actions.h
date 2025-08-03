#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "storage.h"
#include "../sd_mmc_card/sd_mmc_card.h"

namespace esphome {
namespace storage {

// Action pour streaming HTTP simulé depuis SD
template<typename... Ts>
class StorageHTTPStreamAction : public StorageStreamFileAction<Ts...> {
 public:
  StorageHTTPStreamAction(StorageComponent *parent) : StorageStreamFileAction<Ts...>(parent) {}
  
  TEMPLATABLE_VALUE(std::string, endpoint_url)
  TEMPLATABLE_VALUE(uint16_t, port)
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    auto endpoint = this->endpoint_url_.value(x...);
    auto port = this->port_.value(x...);
    
    ESP_LOGD("storage_http", "Starting HTTP stream for %s on %s:%d", 
             path.c_str(), endpoint.c_str(), port);
    
    // Créer un pseudo-serveur HTTP pour le streaming
    this->setup_http_endpoint(endpoint, path);
    
    // Stream avec headers HTTP
    this->stream_with_http_headers(path);
  }
  
 protected:
  void setup_http_endpoint(const std::string& endpoint, const std::string& file_path) {
    // Simuler un endpoint HTTP
    ESP_LOGD("storage_http", "Endpoint available at: http://device_ip%s", endpoint.c_str());
    
    // Vous pouvez intégrer ça avec web_server_base d'ESPHome
    if (web_server_base_) {
      web_server_base_->add_handler(endpoint, [this, file_path](web_server_base::WebServerRequest &req) {
        this->handle_http_request(req, file_path);
      });
    }
  }
  
  void stream_with_http_headers(const std::string& file_path) {
    // Ajouter headers HTTP au stream
    std::string http_headers = this->generate_http_headers(file_path);
    
    // Stream headers puis fichier
    if (this->http_callback_) {
      this->http_callback_(reinterpret_cast<const uint8_t*>(http_headers.c_str()), 
                          http_headers.length());
    }
    
    // Stream le fichier depuis SD avec chunks
    this->parent_->stream_file_direct(file_path, [this](const uint8_t* data, size_t len) {
      if (this->http_callback_) {
        this->http_callback_(data, len);
      }
    });
  }
  
  std::string generate_http_headers(const std::string& file_path) {
    std::string content_type = "application/octet-stream";
    
    // Détecter le type de fichier
    if (file_path.find(".jpg") != std::string::npos || 
        file_path.find(".jpeg") != std::string::npos) {
      content_type = "image/jpeg";
    } else if (file_path.find(".png") != std::string::npos) {
      content_type = "image/png";
    } else if (file_path.find(".gif") != std::string::npos) {
      content_type = "image/gif";
    }
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "\r\n";
  }
  
  void handle_http_request(web_server_base::WebServerRequest &req, const std::string& file_path) {
    ESP_LOGD("storage_http", "HTTP request received for %s", file_path.c_str());
    
    // Servir le fichier directement depuis SD
    auto file_data = this->parent_->read_file_direct(file_path);
    
    if (!file_data.empty()) {
      std::string content_type = this->get_mime_type(file_path);
      req.send_response(200, content_type.c_str(), file_data);
    } else {
      req.send_response(404, "text/plain", "File not found");
    }
  }
  
  std::string get_mime_type(const std::string& file_path) {
    if (file_path.find(".jpg") != std::string::npos || 
        file_path.find(".jpeg") != std::string::npos) return "image/jpeg";
    if (file_path.find(".png") != std::string::npos) return "image/png";
    if (file_path.find(".gif") != std::string::npos) return "image/gif";
    return "application/octet-stream";
  }
  
 public:
  void set_http_callback(std::function<void(const uint8_t*, size_t)> callback) {
    http_callback_ = callback;
  }
  
  void set_web_server(web_server_base::WebServerBase *server) {
    web_server_base_ = server;
  }

 private:
  std::function<void(const uint8_t*, size_t)> http_callback_;
  web_server_base::WebServerBase *web_server_base_{nullptr};
};

// Action spécialisée pour images avec streaming HTTP
template<typename... Ts>
class StorageHTTPImageAction : public StorageHTTPStreamAction<Ts...> {
 public:
  StorageHTTPImageAction(StorageComponent *parent) : StorageHTTPStreamAction<Ts...>(parent) {}
  
  void play(Ts... x) override {
    auto path = this->file_path_.value(x...);
    
    ESP_LOGD("storage_http_image", "Streaming image %s via HTTP", path.c_str());
    
    // Configuration spécifique pour images
    this->endpoint_url_ = "/stream/image/" + std::to_string(this->get_image_id(path));
    this->port_ = 80; // Port HTTP standard
    
    StorageHTTPStreamAction<Ts...>::play(x...);
  }
  
 private:
  uint32_t get_image_id(const std::string& path) {
    // Générer un ID unique pour l'image basé sur son chemin
    uint32_t hash = 0;
    for (char c : path) {
      hash = hash * 31 + c;
    }
    return hash;
  }
};

// Factory étendu
class StorageActionFactory {
 public:
  static std::unique_ptr<StorageStreamFileAction<>> create_stream_action(StorageComponent *parent) {
    return std::make_unique<StorageStreamFileAction<>>(parent);
  }
  
  static std::unique_ptr<StorageHTTPStreamAction<>> create_http_stream_action(StorageComponent *parent) {
    return std::make_unique<StorageHTTPStreamAction<>>(parent);
  }
  
  static std::unique_ptr<StorageHTTPImageAction<>> create_http_image_action(StorageComponent *parent) {
    return std::make_unique<StorageHTTPImageAction<>>(parent);
  }
  
  static std::unique_ptr<StorageStreamImageAction<>> create_image_action(StorageComponent *parent) {
    return std::make_unique<StorageStreamImageAction<>>(parent);
  }
};

}  // namespace storage
}  // namespace esphome



