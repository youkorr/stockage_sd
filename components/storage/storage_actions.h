#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"

#ifdef USE_WEB_SERVER_BASE
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#include "storage.h"
#include <functional>

namespace esphome {
namespace storage {

// Classe handler corrigée pour ESP-IDF
class StoragePathHandler : public AsyncWebHandler {
 public:
  StoragePathHandler(const std::string& path, http_method_t method, 
                     std::function<void(AsyncWebServerRequest*)> handler)
    : path_(path), method_(method), handler_(handler) {}
  
  bool canHandle(AsyncWebServerRequest *request) override {
    if (request->method() != method_) return false;
    
    std::string url = request->url().c_str();
    
    // Vérifier si c'est un wildcard
    if (path_.ends_with("*")) {
      std::string prefix = path_.substr(0, path_.length() - 1);
      return url.starts_with(prefix);
    }
    
    return url == path_;
  }
  
  void handleRequest(AsyncWebServerRequest *request) override {
    handler_(request);
  }

 private:
  std::string path_;
  http_method_t method_;
  std::function<void(AsyncWebServerRequest*)> handler_;
};

class StorageExtensionHandler : public AsyncWebHandler {
 public:
  StorageExtensionHandler(const std::string& extension, http_method_t method,
                         std::function<void(AsyncWebServerRequest*)> handler)
    : extension_(extension), method_(method), handler_(handler) {}
  
  bool canHandle(AsyncWebServerRequest *request) override {
    if (request->method() != method_) return false;
    
    std::string url = request->url().c_str();
    std::string ext = extension_.substr(1); // Enlever le * de *.jpg
    
    return url.ends_with(ext);
  }
  
  void handleRequest(AsyncWebServerRequest *request) override {
    handler_(request);
  }

 private:
  std::string extension_;
  http_method_t method_;
  std::function<void(AsyncWebServerRequest*)> handler_;
};

// Intercepteur HTTP corrigé
class StorageHTTPInterceptor {
 public:
  static void setup_automatic_interception(StorageComponent *storage) {
    ESP_LOGI("storage_interceptor", "Setting up HTTP interception for ESP-IDF");
    
#ifdef USE_WEB_SERVER_BASE
    auto *web_server = App.get_web_server_base();
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
  static void install_handlers(StorageComponent *storage, web_server_base::WebServerBase *web_server) {
    // Handler pour images
    auto handler1 = new StoragePathHandler("/img/*", HTTP_GET, 
      [storage](AsyncWebServerRequest *request) {
        handle_image_request(storage, request, "/scard/img");
      });
    web_server->add_handler(handler1);
    
    // Handler pour extensions
    auto handler2 = new StorageExtensionHandler("*.jpg", HTTP_GET,
      [storage](AsyncWebServerRequest *request) {
        handle_any_image_request(storage, request);
      });
    web_server->add_handler(handler2);
  }
  
  static void serve_file_from_sd(StorageComponent *storage, AsyncWebServerRequest *request, 
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
    
    // Créer réponse avec les données binaires
    AsyncWebServerResponse *response = request->beginResponse_P(
      200, 
      content_type.c_str(), 
      file_data.data(), 
      file_data.size()
    );
    
    response->addHeader("Cache-Control", "public, max-age=3600");
    response->addHeader("Content-Length", std::to_string(file_data.size()).c_str());
    
    request->send(response);
  }
  
  static std::string get_mime_type(const std::string &path) {
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".gif")) return "image/gif";
    if (path.ends_with(".bmp")) return "image/bmp";
    return "application/octet-stream";
  }
};

}  // namespace storage
}  // namespace esphome

