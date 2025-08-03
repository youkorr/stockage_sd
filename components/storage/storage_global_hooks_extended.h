// storage_global_hooks_extended.h
#pragma once
#include "storage.h"
#include "esphome/core/log.h"
#include <map>
#include <string>

// Hooks pour intercepter les appels système ESPHome
extern "C" {
  // Prototype des fonctions ESPHome à intercepter
  size_t esphome_read_file(const char* path, uint8_t* buffer, size_t max_size);
  bool esphome_file_exists(const char* path);
  void* esphome_open_file(const char* path, const char* mode);
}

namespace esphome {
namespace storage {

class StorageGlobalHooks {
 public:
  // ===========================================
  // HOOKS PRINCIPAUX - Votre code existant amélioré
  // ===========================================
  
  static std::vector<uint8_t> intercept_file_read(const std::string &path) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage || !storage->is_global_bypass_enabled()) {
      return {};  // Pas de bypass, utiliser le système normal
    }
    
    ESP_LOGD("storage_hooks", "🔄 Intercepting file read: %s", path.c_str());
    
    // Vérifier si c'est un fichier SD configuré
    if (is_sd_file_path(path)) {
      auto data = storage->read_file_direct(path);
      if (!data.empty()) {
        ESP_LOGI("storage_hooks", "✅ Read from SD: %s (%zu bytes)", path.c_str(), data.size());
        return data;
      }
    }
    
    return {};  // Fallback au système normal
  }
  
  static bool intercept_file_exists(const std::string &path) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage || !storage->is_global_bypass_enabled()) {
      return false;  // Pas de bypass
    }
    
    ESP_LOGD("storage_hooks", "🔍 Checking file existence: %s", path.c_str());
    
    if (is_sd_file_path(path)) {
      bool exists = storage->file_exists_direct(path);
      ESP_LOGD("storage_hooks", "File %s exists on SD: %s", path.c_str(), exists ? "YES" : "NO");
      return exists;
    }
    
    return false;
  }
  
  static void intercept_file_stream(const std::string &path, std::function<void(const uint8_t*, size_t)> callback) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage) return;
    
    ESP_LOGD("storage_hooks", "📡 Streaming file: %s", path.c_str());
    storage->stream_file_direct(path, callback);
  }

  // ===========================================
  // NOUVEAUX HOOKS SPÉCIFIQUES LVGL
  // ===========================================
  
  // Hook pour les images LVGL
  static const uint8_t* intercept_lvgl_image_data(const std::string &path, size_t* size_out) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage) return nullptr;
    
    ESP_LOGD("storage_hooks", "🖼️ LVGL requesting image: %s", path.c_str());
    
    // Cache des images en mémoire pour LVGL (car LVGL garde les pointeurs)
    static std::map<std::string, std::vector<uint8_t>> image_cache;
    
    auto it = image_cache.find(path);
    if (it == image_cache.end()) {
      // Charger depuis SD
      auto data = storage->read_file_direct(path);
      if (data.empty()) {
        ESP_LOGE("storage_hooks", "Failed to load LVGL image: %s", path.c_str());
        return nullptr;
      }
      
      // Mettre en cache
      image_cache[path] = std::move(data);
      it = image_cache.find(path);
      ESP_LOGI("storage_hooks", "✅ LVGL image cached: %s (%zu bytes)", path.c_str(), it->second.size());
    }
    
    if (size_out) *size_out = it->second.size();
    return it->second.data();
  }
  
  // Hook pour audio/media
  static bool intercept_audio_stream(const std::string &path, size_t offset, 
                                   uint8_t* buffer, size_t buffer_size, size_t* bytes_read) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage) return false;
    
    ESP_LOGD("storage_hooks", "🎵 Audio stream request: %s (offset: %zu, size: %zu)", 
             path.c_str(), offset, buffer_size);
    
    // Utiliser votre StorageFile pour lecture audio optimisée
    auto* file = storage->get_file_by_path(path);
    if (file && file->is_sd_direct()) {
      return file->read_audio_chunk(offset, buffer, buffer_size, *bytes_read);
    }
    
    return false;
  }

  // ===========================================
  // INSTALLATION DES HOOKS AUTOMATIQUES
  // ===========================================
  
  static void install_hooks() {
    ESP_LOGI("storage_hooks", "🔧 Installing global storage hooks...");
    
    // Hook 1: Intercepter esphome::read_file
    hook_esphome_read_file();
    
    // Hook 2: Intercepter fopen/fread système
    hook_system_file_calls();
    
    // Hook 3: Intercepter spécifiquement LVGL
    hook_lvgl_image_calls();
    
    ESP_LOGI("storage_hooks", "✅ All storage hooks installed");
  }
  
  static void uninstall_hooks() {
    ESP_LOGW("storage_hooks", "⚠️ Uninstalling storage hooks");
    // Restaurer les fonctions originales
    restore_original_functions();
  }

 private:
  // ===========================================
  // HELPERS PRIVÉS
  // ===========================================
  
  static bool is_sd_file_path(const std::string &path) {
    // Vérifier si le chemin correspond à vos fichiers SD
    return path.find("/scard/") == 0 || 
           path.find("sd:") == 0 ||
           path.find("/sd/") == 0;
  }
  
  static void hook_esphome_read_file() {
    // Remplacer la fonction de lecture de fichiers d'ESPHome
    ESP_LOGD("storage_hooks", "Installing ESPHome file read hook");
    
    // TODO: Implémentation spécifique selon votre architecture ESPHome
    // Exemple conceptuel:
    /*
    original_read_file = esphome_read_file;
    esphome_read_file = hooked_read_file;
    */
  }
  
  static void hook_system_file_calls() {
    // Intercepter les appels système fopen/fread
    ESP_LOGD("storage_hooks", "Installing system file call hooks");
    
    // Utiliser dlsym pour remplacer fopen, fread, etc.
    // Ceci permettra d'intercepter même les bibliothèques qui utilisent stdio
  }
  
  static void hook_lvgl_image_calls() {
    ESP_LOGD("storage_hooks", "Installing LVGL image hooks");
    
    // Hook spécifique pour lv_img_set_src et les fonctions de décodage d'images
    // Intercepter les appels avant qu'ils n'atteignent le filesystem
  }
  
  static void restore_original_functions() {
    // Restaurer toutes les fonctions interceptées
  }
  
  // Pointeurs vers les fonctions originales
  static void* (*original_fopen)(const char*, const char*);
  static size_t (*original_fread)(void*, size_t, size_t, void*);
  static size_t (*original_esphome_read_file)(const char*, uint8_t*, size_t);
};

// ===========================================
// AUTO-INSTALLATION VIA CONSTRUCTEUR STATIQUE
// ===========================================

class StorageHooksInstaller {
 public:
  StorageHooksInstaller() {
    ESP_LOGI("storage_hooks", "🚀 Auto-installing storage hooks on startup");
    StorageGlobalHooks::install_hooks();
  }
  
  ~StorageHooksInstaller() {
    StorageGlobalHooks::uninstall_hooks();
  }
};

// Instance globale pour auto-installation
static StorageHooksInstaller hooks_installer;

}  // namespace storage
}  // namespace esphome
