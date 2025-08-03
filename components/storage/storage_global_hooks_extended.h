// storage_global_hooks_extended.h
#pragma once
#include "storage.h"
#include "esphome/core/log.h"
#include <map>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>

// Forward declarations for system hooks
extern "C" {
  size_t esphome_read_file(const char* path, uint8_t* buffer, size_t max_size);
  bool esphome_file_exists(const char* path);
  void* esphome_open_file(const char* path, const char* mode);
}

namespace esphome {
namespace storage {

// ===========================================
// EXTENSION DE LA CLASSE StorageGlobalHooks EXISTANTE
// ===========================================

// Ajouter les méthodes manquantes à la classe existante via des fonctions statiques
class StorageGlobalHooksExtensions {
 public:
  // ===========================================
  // MÉTHODES D'INSTALLATION (manquantes dans l'original)
  // ===========================================
  
  static void install_hooks() {
    ESP_LOGI("storage_hooks", "🔧 Installing global storage hooks...");
    
    // Installer les hooks système
    hook_system_file_calls();
    
    // Installer hooks spécifiques ESPHome
    hook_esphome_calls();
    
    // Installer hooks LVGL
    hook_lvgl_calls();
    
    ESP_LOGI("storage_hooks", "✅ All storage hooks installed");
    hooks_installed_ = true;
  }
  
  static void uninstall_hooks() {
    if (!hooks_installed_) return;
    
    ESP_LOGW("storage_hooks", "⚠️ Uninstalling storage hooks");
    restore_original_functions();
    hooks_installed_ = false;
  }
  
  static bool are_hooks_installed() { return hooks_installed_; }

  // ===========================================
  // HOOKS LVGL SPÉCIALISÉS
  // ===========================================
  
  static const uint8_t* intercept_lvgl_image_data(const std::string &path, size_t* size_out) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage || !storage->is_global_bypass_enabled()) {
      return nullptr;
    }
    
    ESP_LOGD("storage_hooks", "🖼️ LVGL requesting image: %s", path.c_str());
    
    // Cache statique pour images LVGL (LVGL garde les pointeurs)
    static std::map<std::string, std::vector<uint8_t>> lvgl_image_cache;
    
    auto it = lvgl_image_cache.find(path);
    if (it == lvgl_image_cache.end()) {
      // Charger depuis SD via la classe existante
      auto data = storage->read_file_direct(path);
      if (data.empty()) {
        ESP_LOGE("storage_hooks", "Failed to load LVGL image: %s", path.c_str());
        return nullptr;
      }
      
      // Mettre en cache
      lvgl_image_cache[path] = std::move(data);
      it = lvgl_image_cache.find(path);
      ESP_LOGI("storage_hooks", "✅ LVGL image cached: %s (%zu bytes)", 
              path.c_str(), it->second.size());
    }
    
    if (size_out) *size_out = it->second.size();
    return it->second.data();
  }
  
  // Hook pour audio streaming optimisé
  static bool intercept_audio_stream(const std::string &path, size_t offset, 
                                   uint8_t* buffer, size_t buffer_size, size_t* bytes_read) {
    auto *storage = StorageComponent::get_global_instance();
    if (!storage || !storage->is_global_bypass_enabled()) {
      return false;
    }
    
    ESP_LOGD("storage_hooks", "🎵 Audio stream: %s (offset: %zu, size: %zu)", 
             path.c_str(), offset, buffer_size);
    
    // Utiliser StorageFile pour lecture optimisée
    auto* file = storage->get_file_by_path(path);
    if (file && file->is_sd_direct()) {
      return file->read_audio_chunk(offset, buffer, buffer_size, *bytes_read);
    }
    
    return false;
  }

  // ===========================================
  // HOOKS SYSTÈME AVANCÉS
  // ===========================================
  
  // Hook FILE* fopen pour intercepter toutes les ouvertures de fichiers
  static FILE* hooked_fopen(const char* path, const char* mode) {
    if (!hooks_installed_ || !path) {
      return original_fopen ? original_fopen(path, mode) : nullptr;
    }
    
    std::string spath(path);
    ESP_LOGD("storage_hooks", "📂 fopen intercepted: %s (mode: %s)", path, mode);
    
    // Vérifier si c'est un fichier géré par notre storage
    if (is_managed_file(spath)) {
      auto *storage = StorageComponent::get_global_instance();
      if (storage && storage->file_exists_direct(spath)) {
        ESP_LOGD("storage_hooks", "🔄 Redirecting to SD: %s", path);
        
        // Créer un FILE* virtuel ou rediriger vers le bon chemin SD
        return redirect_to_sd_file(spath, mode);
      }
    }
    
    // Fallback vers fopen original
    return original_fopen ? original_fopen(path, mode) : nullptr;
  }
  
  // Hook fread pour données depuis SD
  static size_t hooked_fread(void* ptr, size_t size, size_t count, FILE* stream) {
    if (!hooks_installed_ || !stream) {
      return original_fread ? original_fread(ptr, size, count, stream) : 0;
    }
    
    // Vérifier si c'est un de nos streams redirigés
    if (is_our_file_stream(stream)) {
      return handle_sd_fread(ptr, size, count, stream);
    }
    
    // Fallback vers fread original
    return original_fread ? original_fread(ptr, size, count, stream) : 0;
  }

 private:
  static bool hooks_installed_;
  
  // Pointeurs vers fonctions originales
  static FILE* (*original_fopen)(const char*, const char*);
  static size_t (*original_fread)(void*, size_t, size_t, FILE*);
  static int (*original_fclose)(FILE*);
  
  // ===========================================
  // HELPERS PRIVÉS
  // ===========================================
  
  static bool is_managed_file(const std::string &path) {
    return path.find("/sd/") == 0 || 
           path.find("/scard/") == 0 ||
           path.find("sd:") == 0;
  }
  
  static FILE* redirect_to_sd_file(const std::string &path, const char* mode) {
    // Pour l'instant, ouvrir le fichier SD normalement
    // TODO: Implémenter un FILE* virtuel qui utilise StorageFile
    
    auto *storage = StorageComponent::get_global_instance();
    if (!storage) return nullptr;
    
    // Essayer d'ouvrir via le système de fichiers SD monté
    std::string sd_path = "/sdcard" + path.substr(3); // Remplacer /sd/ par /sdcard/
    return original_fopen ? original_fopen(sd_path.c_str(), mode) : nullptr;
  }
  
  static bool is_our_file_stream(FILE* stream) {
    // TODO: Maintenir une liste des FILE* que nous gérons
    return false; // Pour l'instant
  }
  
  static size_t handle_sd_fread(void* ptr, size_t size, size_t count, FILE* stream) {
    // TODO: Implémenter lecture via StorageFile
    return original_fread ? original_fread(ptr, size, count, stream) : 0;
  }
  
  static void hook_system_file_calls() {
    ESP_LOGD("storage_hooks", "Installing system file hooks");
    
    // Sur ESP32, on ne peut pas facilement remplacer les fonctions système
    // On utilise plutôt une approche de wrapper/interception au niveau application
    
    // Sauvegarder les fonctions originales pour référence
    original_fopen = fopen;
    original_fread = fread;
    original_fclose = fclose;
    
    // Note: L'interception se fait au niveau des composants ESPHome
    // qui utilisent nos macros STORAGE_INTERCEPT_*
    
    ESP_LOGD("storage_hooks", "System file hooks ready (wrapper mode)");
  }
  
  static void hook_esphome_calls() {
    ESP_LOGD("storage_hooks", "Installing ESPHome-specific hooks");
    
    // Sur ESP32/ESPHome, on intercepte au niveau des composants
    // plutôt qu'au niveau système. Les composants utilisent nos macros.
    
    ESP_LOGD("storage_hooks", "ESPHome hooks ready (component-level interception)");
  }
  
  static void hook_lvgl_calls() {
    ESP_LOGD("storage_hooks", "Installing LVGL hooks");
    
    // Pour LVGL sur ESP32, l'interception se fait via les fonctions de callback
    // ou en remplaçant les drivers d'image lors de l'initialisation LVGL
    
    ESP_LOGD("storage_hooks", "LVGL hooks ready (callback-based interception)");
  }
  
  static void restore_original_functions() {
    ESP_LOGD("storage_hooks", "Restoring original functions");
    
    // Sur ESP32, on ne modifie pas vraiment les fonctions système
    // donc la "restauration" consiste juste à nettoyer nos états
    
    original_fopen = nullptr;
    original_fread = nullptr;
    original_fclose = nullptr;
    
    ESP_LOGD("storage_hooks", "Function restoration completed");
  }
};

// Variables statiques
bool StorageGlobalHooksExtensions::hooks_installed_ = false;
FILE* (*StorageGlobalHooksExtensions::original_fopen)(const char*, const char*) = nullptr;
size_t (*StorageGlobalHooksExtensions::original_fread)(void*, size_t, size_t, FILE*) = nullptr;
int (*StorageGlobalHooksExtensions::original_fclose)(FILE*) = nullptr;

// ===========================================
// EXTENSION DE StorageGlobalHooks via fonctions friend
// ===========================================

// Ajouter les méthodes manquantes à StorageGlobalHooks
namespace StorageGlobalHooksImpl {
  void install_hooks() {
    StorageGlobalHooksExtensions::install_hooks();
  }
  
  void uninstall_hooks() {
    StorageGlobalHooksExtensions::uninstall_hooks();
  }
  
  const uint8_t* get_lvgl_image_data(const std::string &path, size_t* size_out) {
    return StorageGlobalHooksExtensions::intercept_lvgl_image_data(path, size_out);
  }
  
  bool stream_audio_chunk(const std::string &path, size_t offset, 
                         uint8_t* buffer, size_t buffer_size, size_t* bytes_read) {
    return StorageGlobalHooksExtensions::intercept_audio_stream(path, offset, 
                                                              buffer, buffer_size, bytes_read);
  }
}

// ===========================================
// AUTO-INSTALLER COMPATIBLE  
// ===========================================

class StorageHooksAutoInstaller {
 public:
  StorageHooksAutoInstaller() {
    ESP_LOGI("storage_hooks", "🚀 Auto-installing storage hooks on startup");
    StorageGlobalHooksExtensions::install_hooks();
  }
  
  ~StorageHooksAutoInstaller() {
    StorageGlobalHooksExtensions::uninstall_hooks();
  }
};

// Instance globale pour auto-installation
static StorageHooksAutoInstaller hooks_auto_installer;

// ===========================================
// MACROS PRATIQUES ESP32-OPTIMISÉES
// ===========================================

// Macro pour interception intelligente avec fallback
#define STORAGE_INTERCEPT_READ(path) \
  do { \
    auto data = esp32_storage_read(path); \
    if (!data.empty()) return data; \
  } while(0)

// Macro pour existence avec fallback
#define STORAGE_INTERCEPT_EXISTS(path) \
  do { \
    if (StorageGlobalHooks::intercept_file_exists(path)) return true; \
    FILE* f = esp32_storage_fopen(path, "r"); \
    if (f) { fclose(f); return true; } \
  } while(0)

// Macro pour LVGL avec cache
#define STORAGE_INTERCEPT_LVGL_IMAGE(path, size_ptr) \
  do { \
    auto* img_data = StorageGlobalHooksExtensions::intercept_lvgl_image_data(path, size_ptr); \
    if (img_data) return img_data; \
  } while(0)

// Macro pour ouvrir fichier avec redirection SD
#define STORAGE_FOPEN(path, mode) \
  (esp32_storage_fopen(path, mode) ?: fopen(path, mode))

// Macro pour streaming audio
#define STORAGE_INTERCEPT_AUDIO(path, offset, buffer, size, bytes_read) \
  do { \
    if (StorageGlobalHooksExtensions::intercept_audio_stream(path, offset, buffer, size, bytes_read)) \
      return true; \
  } while(0)

// ===========================================
// API C POUR INTEROPÉRABILITÉ
// ===========================================

extern "C" {
  // Fonctions C pour autres composants ESPHome
  const uint8_t* storage_hooks_get_lvgl_image(const char* path, size_t* size_out) {
    return StorageGlobalHooksExtensions::intercept_lvgl_image_data(std::string(path), size_out);
  }
  
  bool storage_hooks_stream_audio(const char* path, size_t offset, 
                                 uint8_t* buffer, size_t buffer_size, size_t* bytes_read) {
    return StorageGlobalHooksExtensions::intercept_audio_stream(std::string(path), offset, 
                                                              buffer, buffer_size, bytes_read);
  }
  
  void storage_hooks_install() {
    StorageGlobalHooksExtensions::install_hooks();
  }
  
  void storage_hooks_uninstall() {
    StorageGlobalHooksExtensions::uninstall_hooks();
  }
  
  bool storage_hooks_are_installed() {
    return StorageGlobalHooksExtensions::are_hooks_installed();
  }
}

}  // namespace storage
}  // namespace esphome
