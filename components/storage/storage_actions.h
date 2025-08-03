#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"

#ifdef USE_WEB_SERVER_IDF
#include "esphome/components/web_server_idf/web_server_idf.h"
#endif

#include "storage.h"
#include <functional>

namespace esphome {
namespace storage {

// Classe handler corrigée pour web_server_idf
class StoragePathHandler : public web_server_idf::AsyncWebHandler {
 public:
  StoragePathHandler(const std::string& path, int method, 
                     std::function<void(web_server_idf::AsyncWebServerRequest*)> handler)
    : path_(path), method_(method), handler_(handler) {}
  
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) override {
    if (request->method() != method_) return false;
    
    std::string url = request->url();
    
    // Vérifier si c'est un wildcard
    if (path_.ends_with("*")) {
      std::string prefix = path_.substr(0, path_.length() - 1);
      return url.starts_with(prefix);
    }
    
    return url == path_;
  }
  
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override {
    handler_(request);
  }

 private:
  std::string path_;
  int method_;  // Utiliser int au lieu de http_method_t
  std::function<void(web_server_idf::AsyncWebServerRequest*)> handler_;
};

class StorageExtensionHandler : public web_server_idf::AsyncWebHandler {
 public:
  StorageExtensionHandler(const std::string& extension, int method,
                         std::function<void(web_server_idf::AsyncWebServerRequest*)> handler)
    : extension_(extension), method_(method), handler_(handler) {}
  
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) override {
    if (request->method() != method_) return false;
    
    std::string url = request->url();
    std::string ext = extension_.substr(1); // Enlever le * de *.jpg
    
    return url.ends_with(ext);
  }
  
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override {
    handler_(request);
  }

 private:
  std::string extension_;
  int method_;
  std::function<void(web_server_idf::AsyncWebServerRequest*)> handler_;
};

// Intercepteur HTTP corrigé
class StorageHTTPInterceptor {
 public:
  static void setup_automatic_interception(StorageComponent *storage) {
    ESP_LOGI("storage_interceptor", "Setting up HTTP interception for web_server_idf");
    
#ifdef USE_WEB_SERVER_IDF
    auto *web_server = App.get_component<web_server_idf::WebServerIdf>();
    if (!web_server) {
      ESP_LOGW("storage_interceptor", "No web server found");
      return;
    }
    
    install_handlers(storage, web_server);
#else
    ESP_LOGW("storage_interceptor", "Web server not available");
#endif
  }
  
 private:
  static void install_handlers(StorageComponent *storage, web_server_idf::WebServerIdf *web_server) {
    // Handler pour images avec constantes HTTP appropriées
    auto handler1 = new StoragePathHandler("/img/*", 1, // HTTP_GET = 1
      [storage](web_server_idf::AsyncWebServerRequest *request) {
        handle_image_request(storage, request, "/scard/img");
      });
    web_server->add_handler(handler1);
    
    // Handler pour extensions
    auto handler2 = new StorageExtensionHandler("*.jpg", 1, // HTTP_GET = 1
      [storage](web_server_idf::AsyncWebServerRequest *request) {
        handle_any_image_request(storage, request);
      });
    web_server->add_handler(handler2);
    
    // Ajouter d'autres extensions
    auto handler3 = new StorageExtensionHandler("*.png", 1,
      [storage](web_server_idf::AsyncWebServerRequest *request) {
        handle_any_image_request(storage, request);
      });
    web_server->add_handler(handler3);
  }
  
  // Déclarer les méthodes statiques manquantes
  static void handle_image_request(StorageComponent *storage, 
                                  web_server_idf::AsyncWebServerRequest *request, 
                                  const std::string &base_path) {
    std::string url_path = request->url();
    std::string full_path = base_path + url_path;
    
    ESP_LOGD("storage_interceptor", "Image request: %s -> %s", 
             url_path.c_str(), full_path.c_str());
    
    serve_file_from_sd(storage, request, full_path);
  }
  
  static void handle_any_image_request(StorageComponent *storage, 
                                      web_server_idf::AsyncWebServerRequest *request) {
    std::string url_path = request->url();
    
    // Essayer plusieurs emplacements
    std::vector<std::string> possible_paths = {
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
  
  static void serve_file_from_sd(StorageComponent *storage, 
                                web_server_idf::AsyncWebServerRequest *request, 
                                const std::string &file_path) {
    if (!storage->file_exists_direct(file_path.c_str())) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    
    auto file_data = storage->read_file_direct(file_path.c_str());
    if (file_data.empty()) {
      request->send(500, "text/plain", "Failed to read file");
      return;
    }
    
    std::string content_type = get_mime_type(file_path);
    
    // Utiliser beginResponse au lieu de beginResponse_P
    web_server_idf::AsyncWebServerResponse *response = request->beginResponse(
      200, 
      content_type.c_str(), 
      reinterpret_cast<const char*>(file_data.data()),
      file_data.size()
    );
    
    response->addHeader("Cache-Control", "public, max-age=3600");
    response->addHeader("Content-Length", std::to_string(file_data.size()).c_str());
    response->addHeader("Access-Control-Allow-Origin", "*");
    
    request->send(response);
    
    ESP_LOGD("storage_interceptor", "Served: %s (%zu bytes, %s)", 
             file_path.c_str(), file_data.size(), content_type.c_str());
  }
  
  static std::string get_mime_type(const std::string &path) {
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".gif")) return "image/gif";
    if (path.ends_with(".bmp")) return "image/bmp";
    if (path.ends_with(".webp")) return "image/webp";
    if (path.ends_with(".svg")) return "image/svg+xml";
    return "application/octet-stream";
  }
};

// Factory pour créer les actions (actions template conservées mais simplifiées)
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
    
    // Implementation simplifiée
    if (this->parent_->file_exists_direct(path)) {
      auto file_data = this->parent_->read_file_direct(path);
      this->on_file_streamed(file_data);
    }
  }
  
  virtual void on_file_streamed(const std::vector<uint8_t>& data) {
    ESP_LOGD("storage_action", "File streamed: %zu bytes", data.size());
  }
  
 protected:
  StorageComponent *parent_;
};

// Factory d'actions
class StorageActionFactory {
 public:
  static void setup_http_interception(StorageComponent *parent) {
    StorageHTTPInterceptor::setup_automatic_interception(parent);
  }
};

}  // namespace storage
}  // namespace esphome


