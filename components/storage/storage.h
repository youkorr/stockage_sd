#pragma once
#include <string>
#include <vector>
#include <memory>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "../sd_mmc_card/sd_mmc_card.h"

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Énumérations pour les formats d'image
enum class ImageFormat {
  RGB565,
  RGB888,
  RGBA,
  GRAYSCALE,
  BINARY
};

enum class ByteOrder {
  little_endian,
  big_endian
};

// Classe principale Storage
class StorageComponent : public Component {
 public:
  StorageComponent() = default;
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration
  void set_platform(const std::string &platform) { platform_ = platform; }
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { sd_component_ = sd_component; }
  void set_cache_size(size_t cache_size) { cache_size_ = cache_size; }
  
  // Méthodes de fichier
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
  // Getters
  const std::string &get_platform() const { return platform_; }
  sd_mmc_card::SdMmc *get_sd_component() const { return sd_component_; }
  
 private:
  std::string platform_;
  sd_mmc_card::SdMmc *sd_component_{nullptr};
  size_t cache_size_{0};
};

// Classe pour les images SD
class SdImageComponent : public Component {
 public:
  SdImageComponent() = default;
  
  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration de base
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_width(int width) { width_ = width; }
  void set_height(int height) { height_ = height; }
  void set_format(ImageFormat format) { format_ = format; }
  void set_format_string(const std::string &format);
  void set_byte_order(ByteOrder byte_order) { byte_order_ = byte_order; }
  void set_byte_order_string(const std::string &byte_order);
  void set_expected_data_size(size_t size) { expected_data_size_ = size; }
  
  // Configuration avancée
  void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
  void set_preload(bool preload) { preload_ = preload; }
  void set_storage_component(StorageComponent *storage) { storage_component_ = storage; }
  
  // Getters
  const std::string &get_file_path() const { return file_path_; }
  int get_width() const { return width_; }
  int get_height() const { return height_; }
  ImageFormat get_format() const { return format_; }
  ByteOrder get_byte_order() const { return byte_order_; }
  bool is_loaded() const { return is_loaded_; }
  bool is_cache_enabled() const { return cache_enabled_; }
  
  // Méthodes principales
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Méthodes d'accès aux pixels
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha = nullptr) const;
  const uint8_t *get_data() const { return image_data_.data(); }
  size_t get_data_size() const { return image_data_.size(); }
  
  // Méthodes utilitaires
  bool validate_image_data() const;
  size_t calculate_expected_size() const;
  std::string get_format_string() const;
  
  // Gestion mémoire
  void free_cache();
  size_t get_memory_usage() const { return image_data_.size(); }
  
  // Méthodes pour streaming (images très grandes)
  void get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  bool is_streaming_mode() const { return streaming_mode_; }
  void set_streaming_mode(bool enabled) { streaming_mode_ = enabled; }
  
  // Méthodes de compatibilité pour les displays
  void get_image_dimensions(int *width, int *height) {
    *width = width_;
    *height = height_;
  }
  
  const uint8_t* get_image_data() const { return get_data(); }

 private:
  // Configuration
  std::string file_path_;
  int width_{0};
  int height_{0};
  ImageFormat format_{ImageFormat::RGB565};
  ByteOrder byte_order_{ByteOrder::LITTLE_ENDIAN};
  size_t expected_data_size_{0};
  bool cache_enabled_{true};
  bool preload_{false};
  bool streaming_mode_{false};
  
  // État
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_;
  StorageComponent *storage_component_{nullptr};
  
  // Méthodes privées
  bool read_image_from_storage();
  void convert_byte_order(std::vector<uint8_t> &data);
  void convert_pixel_format(int x, int y, const uint8_t *pixel_data, 
                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  
  // Validation
  bool validate_dimensions() const;
  bool validate_file_path() const;
};

// Classes pour les actions d'automatisation
template<typename... Ts> class SdImageLoadAction : public Action<Ts...> {
 public:
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_file_path(std::function<std::string(Ts...)> file_path) { file_path_ = file_path; }
  void set_parent(SdImageComponent *parent) { parent_ = parent; }
  
  void play(Ts... x) override {
    std::string path = file_path_.has_value() ? file_path_.value()(x...) : "";
    if (path.empty()) {
      parent_->load_image();
    } else {
      parent_->load_image_from_path(path);
    }
  }

 private:
  SdImageComponent *parent_;
  optional<std::function<std::string(Ts...)>> file_path_;
};

template<typename... Ts> class SdImageUnloadAction : public Action<Ts...> {
 public:
  explicit SdImageUnloadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_parent(SdImageComponent *parent) { parent_ = parent; }
  
  void play(Ts... x) override {
    parent_->unload_image();
  }

 private:
  SdImageComponent *parent_;
};

}  // namespace storage
}  // namespace esphome

















