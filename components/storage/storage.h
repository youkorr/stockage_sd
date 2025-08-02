#pragma once
#include <string>
#include <vector>
#include <functional>
#include "esphome/core/component.h"
#include "esphome/components/audio/audio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "esp_vfs_fat.h"
#include "../sd_mmc_card/sd_mmc_card.h"

namespace esphome {
namespace storage {

class StorageFile : public audio::AudioFile, public Component {
 public:
  StorageFile() : path_(""), id_(""), platform_(""), chunk_size_(512) {}
  StorageFile(const std::string &path, const std::string &id) 
    : path_(path), id_(id), platform_(""), chunk_size_(512) {}
  
  void setup() override {}
  
  // Getters/Setters existants
  void set_component_source(const std::string &source) { component_source_ = source; }
  const std::string &get_component_source() const { return component_source_; }
  const std::string &get_path() const { return path_; }
  const std::string &get_id() const { return id_; }
  const std::string &get_platform() const { return platform_; }
  void set_path(const std::string &path) { path_ = path; }
  void set_id(const std::string &id) { id_ = id; }
  void set_platform(const std::string &platform) { platform_ = platform; }
  void set_chunk_size(uint32_t chunk_size) { chunk_size_ = chunk_size; }
  uint32_t get_chunk_size() const { return chunk_size_; }
  std::string get_filename() const { return path_; }
  bool is_valid() const { return !path_.empty(); }

  // NOUVEAU: Méthodes pour bypass direct SD
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { sd_component_ = sd_component; }
  bool is_sd_direct() const { return platform_ == "sd_direct"; }
  
  // NOUVEAU: Stream direct depuis SD sans buffer RAM - utilise vos méthodes existantes
  void stream_direct(std::function<void(const uint8_t*, size_t)> callback);
  void stream_chunked_direct(std::function<void(const uint8_t*, size_t)> callback);
  
  // NOUVEAU: Lecture directe compatible avec votre SdMmc
  std::vector<uint8_t> read_direct();
  bool read_audio_chunk(size_t offset, uint8_t* buffer, size_t buffer_size, size_t& bytes_read);
  
  // NOUVEAU: Helpers pour info fichier
  size_t get_file_size_direct() const;  // Pas override - nouvelle méthode
  bool file_exists_direct() const;

 private:
  std::string path_;
  std::string id_;
  std::string platform_;
  std::string component_source_;
  uint32_t chunk_size_;
  
  // NOUVEAU: Pour le bypass SD
  sd_mmc_card::SdMmc *sd_component_{nullptr};
  mutable size_t current_position_{0};
  mutable size_t cached_file_size_{0};
  mutable bool file_size_cached_{false};
};

class StorageComponent : public Component {
 public:
  void setup() override;
  std::string get_file_path(const std::string &file_id) const;
  void add_file(StorageFile *file) { files_.push_back(file); }
  void set_platform(const std::string &platform) { platform_ = platform; }
  
  // NOUVEAU: Configuration SD direct
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { 
    sd_component_ = sd_component; 
    // Configurer automatiquement tous les fichiers pour SD direct
    for (auto *file : files_) {
      file->set_sd_component(sd_component);
      if (platform_ == "sd_direct") {
        file->set_platform("sd_direct");
      }
    }
  }
  
  // NOUVEAU: Bypass global pour ESPHome
  void enable_global_bypass(bool enable) { global_bypass_enabled_ = enable; }
  bool is_global_bypass_enabled() const { return global_bypass_enabled_; }
  void set_cache_size(size_t cache_size) { cache_size_ = cache_size; }
  
  // NOUVEAU: Méthodes de bypass - utilise vos méthodes SdMmc existantes
  StorageFile* get_file_by_path(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool file_exists_direct(const std::string &path);
  void stream_file_direct(const std::string &path, std::function<void(const uint8_t*, size_t)> callback);
  
  // NOUVEAU: Interface pour autres composants ESPHome
  static StorageComponent* global_instance_;
  static void set_global_instance(StorageComponent* instance) { global_instance_ = instance; }
  static StorageComponent* get_global_instance() { return global_instance_; }

 private:
  void setup_sd_card();
  void setup_flash();
  void setup_inline();
  void setup_sd_direct(); // NOUVEAU
  
  std::vector<StorageFile*> files_;
  std::string platform_;
  
  // NOUVEAU: Pour le bypass
  sd_mmc_card::SdMmc *sd_component_{nullptr};
  bool global_bypass_enabled_{false};
  size_t cache_size_{0};
};

// NOUVEAU: Classe pour hooks globaux ESPHome
class StorageGlobalHooks {
 public:
  // Intercepter les appels de lecture de fichiers ESPHome
  static std::vector<uint8_t> intercept_file_read(const std::string &path);
  static bool intercept_file_exists(const std::string &path);
  static void intercept_file_stream(const std::string &path, std::function<void(const uint8_t*, size_t)> callback);
};

}  // namespace storage
}  // namespace esphome



















