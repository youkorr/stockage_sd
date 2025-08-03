#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
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
};

}  // namespace storage
}  // namespace esphome


