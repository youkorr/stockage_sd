#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/image/image.h"  // Ajout important
#include "../sd_mmc_card/sd_mmc_card.h"
#include "../image.h/image.h"

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Utiliser l'enum ImageType de ESPHome ou le définir si nécessaire
using ImageType = image::ImageType;

// Énumérations pour les formats d'image
enum class ImageFormat {
  rgb565,
  rgb888,
  rgba,
  grayscale,  // Fixed: was "graycale"
  binary
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
  void set_platform(const std::string &platform) { this->platform_ = platform; }
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { this->sd_component_ = sd_component; }
  void set_cache_size(size_t cache_size) { this->cache_size_ = cache_size; }
  
  // Méthodes de fichier
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
  // Getters
  const std::string &get_platform() const { return this->platform_; }
  sd_mmc_card::SdMmc *get_sd_component() const { return this->sd_component_; }
  size_t get_cache_size() const { return this->cache_size_; }
  
 private:
  std::string platform_;
  sd_mmc_card::SdMmc *sd_component_{nullptr};
  size_t cache_size_{0};
};

// Classe pour les images SD - HÉRITE MAINTENANT DE image::Image
class SdImageComponent : public Component, public display::BaseImage {
 public:
  SdImageComponent() = default;

  
  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration de base
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_width(int width) { 
    this->width_ = width; 
    // Mettre à jour aussi la classe parent Image
    this->width_override_ = width;
  }
  void set_height(int height) { 
    this->height_ = height; 
    // Mettre à jour aussi la classe parent Image
    this->height_override_ = height;
  }
  void set_format(ImageFormat format) { this->format_ = format; }
  void set_format_string(const std::string &format);
  void set_byte_order(ByteOrder byte_order) { this->byte_order_ = byte_order; }
  void set_byte_order_string(const std::string &byte_order);
  void set_expected_data_size(size_t size) { this->expected_data_size_ = size; }
  
  // Configuration avancée
  void set_cache_enabled(bool enabled) { this->cache_enabled_ = enabled; }
  void set_preload(bool preload) { this->preload_ = preload; }
  void set_storage_component(StorageComponent *storage) { this->storage_component_ = storage; }
  
  // Getters
  const std::string &get_file_path() const { return this->file_path_; }
  int get_width() const override { return this->width_; }  // Override de Image
  int get_height() const override { return this->height_; } // Override de Image
  ImageFormat get_format() const { return this->format_; }
  ByteOrder get_byte_order() const { return this->byte_order_; }
  bool is_loaded() const { return this->is_loaded_; }
  bool is_cache_enabled() const { return this->cache_enabled_; }
  size_t get_expected_data_size() const { return this->expected_data_size_; }
  
  // Méthodes héritées de image::Image - OBLIGATOIRES
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  
  // REMOVED: These methods don't exist in the base Image class or have different signatures
  // const uint8_t *get_data_start() const override { return this->image_data_.data(); }
  // ImageType get_type() const override;
  
  // Alternative methods for accessing image data
  const uint8_t *get_data_start() const { return this->image_data_.data(); }
  ImageType get_image_type() const;  // Renamed to avoid override conflict
  
  // Méthodes principales
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Méthodes d'accès aux pixels
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const; 
  const uint8_t *get_data() const { return this->image_data_.data(); }
  size_t get_data_size() const { return this->image_data_.size(); }
  
  // Méthodes utilitaires
  bool validate_image_data() const;
  size_t calculate_expected_size() const;
  std::string get_format_string() const;
  
  // Gestion mémoire
  void free_cache();
  size_t get_memory_usage() const { return this->image_data_.size(); }
  
  // Méthodes pour streaming (images très grandes)
  void get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  void get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  bool is_streaming_mode() const { return this->streaming_mode_; }
  void set_streaming_mode(bool enabled) { this->streaming_mode_ = enabled; }
  
  // Méthodes de compatibilité pour les displays
  void get_image_dimensions(int *width, int *height) const {
    if (width) *width = this->width_;
    if (height) *height = this->height_;
  }
  
  const uint8_t* get_image_data() const { return this->get_data(); }

 private:
  // Configuration
  std::string file_path_;
  int width_{0};
  int height_{0};
  ImageFormat format_{ImageFormat::rgb565};
  ByteOrder byte_order_{ByteOrder::little_endian};
  size_t expected_data_size_{0};
  bool cache_enabled_{true};
  bool preload_{false};
  bool streaming_mode_{false};
  
  // État
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_;
  StorageComponent *storage_component_{nullptr};
  
  // Variables pour la compatibilité avec Image
  int width_override_{0};
  int height_override_{0};
  
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
  SdImageLoadAction() = default;
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  
  void set_parent(SdImageComponent *parent) { this->parent_ = parent; }
  
  void play(Ts... x) override {
    if (this->parent_ == nullptr) return;
    
    if (this->file_path_.has_value()) {
      std::string path = this->file_path_.value(x...);
      if (!path.empty()) {
        this->parent_->load_image_from_path(path);
        return;
      }
    }
    this->parent_->load_image();
  }

 private:
  SdImageComponent *parent_{nullptr};
};

template<typename... Ts> class SdImageUnloadAction : public Action<Ts...> {
 public:
  SdImageUnloadAction() = default;
  explicit SdImageUnloadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_parent(SdImageComponent *parent) { this->parent_ = parent; }
  
  void play(Ts... x) override {
    if (this->parent_ != nullptr) {
      this->parent_->unload_image();
    }
  }

 private:
  SdImageComponent *parent_{nullptr};
};

}  // namespace storage
}  // namespace esphome















