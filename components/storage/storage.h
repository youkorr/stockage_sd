#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>  // Pour std::transform
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/image/image.h"
#include "../sd_mmc_card/sd_mmc_card.h"

// Inclusions pour les décodeurs d'images
#ifdef USE_ESP32

#endif

// Optionnel: Support PNG avec LodePNG (plus lourd)
// #define USE_PNG_DECODER
#ifdef USE_PNG_DECODER
#include "lodepng.h"
#endif

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Utiliser l'enum ImageType de ESPHome
using ImageType = image::ImageType;

// Énumérations pour les formats d'image
enum class ImageFormat {
  rgb565,
  rgb888,
  rgba,
  grayscale,
  binary
};

enum class ByteOrder {
  little_endian,
  big_endian
};

// Structure pour les informations d'image décodée
struct DecodedImageInfo {
  int width{0};
  int height{0};
  int channels{3};  // RGB = 3, RGBA = 4
  std::vector<uint8_t> data;
  bool valid{false};
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

// Classe pour les images SD - HÉRITE DE BaseImage pour compatibilité display
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
    this->width_override_ = width;
  }
  void set_height(int height) { 
    this->height_ = height; 
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
  void set_auto_resize(bool enabled) { this->auto_resize_ = enabled; }
  
  // Getters
  const std::string &get_file_path() const { return this->file_path_; }
  int get_width() const override { return this->actual_width_ > 0 ? this->actual_width_ : this->width_; }
  int get_height() const override { return this->actual_height_ > 0 ? this->actual_height_ : this->height_; }
  ImageFormat get_format() const { return this->format_; }
  ByteOrder get_byte_order() const { return this->byte_order_; }
  bool is_loaded() const { return this->is_loaded_; }
  bool is_cache_enabled() const { return this->cache_enabled_; }
  size_t get_expected_data_size() const { return this->expected_data_size_; }
  
  // Méthodes héritées de BaseImage
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  
  // Méthodes pour accéder aux données d'image
  const uint8_t *get_data_start() const { return this->image_data_.data(); }
  ImageType get_image_type() const;
  
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
  
  // Informations sur le fichier image
  bool is_compressed_format() const;
  std::string get_file_extension() const;
  
 private:
  // Configuration
  std::string file_path_;
  int width_{0};
  int height_{0};
  int actual_width_{0};   // Dimensions réelles après décodage
  int actual_height_{0};
  ImageFormat format_{ImageFormat::rgb888};  // Format par défaut pour les images décodées
  ByteOrder byte_order_{ByteOrder::little_endian};
  size_t expected_data_size_{0};
  bool cache_enabled_{true};
  bool preload_{false};
  bool streaming_mode_{false};
  bool auto_resize_{true};  // Ajuster automatiquement les dimensions
  
  // État
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_;
  StorageComponent *storage_component_{nullptr};
  
  // Variables pour la compatibilité avec Image
  int width_override_{0};
  int height_override_{0};
  
  // Méthodes privées pour le décodage
  bool is_jpeg_file(const std::string &path) const;
  bool is_png_file(const std::string &path) const;
  bool is_raw_file(const std::string &path) const;
  
  // Décodeurs
  bool decode_jpeg_data(const std::vector<uint8_t> &jpeg_data, DecodedImageInfo &info);
  bool decode_png_data(const std::vector<uint8_t> &png_data, DecodedImageInfo &info);
  bool process_raw_data(const std::vector<uint8_t> &raw_data, DecodedImageInfo &info);
  bool decode_image_file(const std::vector<uint8_t> &file_data, DecodedImageInfo &info);
  
  // Méthodes utilitaires existantes
  bool read_image_from_storage();
  void convert_byte_order(std::vector<uint8_t> &data);
  void convert_pixel_format(int x, int y, const uint8_t *pixel_data, 
                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  
  // Conversion de format
  void convert_rgb_to_format(const std::vector<uint8_t> &rgb_data, std::vector<uint8_t> &output_data);
  
  // Validation
  bool validate_dimensions() const;
  bool validate_file_path() const;
};

// Classes pour les actions d'automatisation (inchangées)
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














