#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/image/image.h"  // Image ESPHome
#include "../sd_mmc_card/sd_mmc_card.h"

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
  grayscale,
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

// Classe pour les images SD - HÉRITE DE display::BaseImage (Image ESPHome)
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
  
  // Getters
  const std::string &get_file_path() const { return this->file_path_; }
  int get_width() const override { return this->width_; }
  int get_height() const override { return this->height_; }
  ImageFormat get_format() const { return this->format_; }
  ByteOrder get_byte_order() const { return this->byte_order_; }
  bool is_loaded() const { return this->is_loaded_; }
  bool is_cache_enabled() const { return this->cache_enabled_; }
  size_t get_expected_data_size() const { return this->expected_data_size_; }
  
  // Méthodes héritées de display::BaseImage
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  
  // Accès aux données image
  const uint8_t *get_data_start() const { return this->image_data_.data(); }
  ImageType get_image_type() const;
  
  // Chargement/déchargement d'image
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Accès aux pixels (exemples)
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
  
  // Streaming (pour images très grandes)
  void get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  void get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  bool is_streaming_mode() const { return this->streaming_mode_; }
  void set_streaming_mode(bool enabled) { this->streaming_mode_ = enabled; }
  
  // Compatibilité affichage
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
  
  // Variables override largeur/hauteur
  int width_override_{0};
  int height_override_{0};
  
  // Méthodes privées (à implémenter dans le .cpp)
  bool read_image_from_storage();
  void convert_byte_order(std::vector<uint8_t> &data);
  void convert_pixel_format(int x, int y, const uint8_t *pixel_data, 
                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  
  bool validate_dimensions() const;
  bool validate_file_path() const;
};

// Actions pour l'automatisation
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

// ---------- Implémentation de load_image_from_path (dans storage.cpp) ----------
inline bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGD("SdImageComponent", "Loading image from: %s", path.c_str());

  image::Image decoded_img;
  if (!decoded_img.load_file(path.c_str())) {
    ESP_LOGE("SdImageComponent", "Failed to load image: %s", path.c_str());
    return false;
  }

  this->width_ = decoded_img.get_width();
  this->height_ = decoded_img.get_height();

  // Copie les données RGB (ou autres formats)
  const auto &data = decoded_img.get_data();
  this->image_data_.assign(data.begin(), data.end());

  this->is_loaded_ = true;
  ESP_LOGI("SdImageComponent", "Image loaded %dx%d (%zu bytes)", this->width_, this->height_, this->image_data_.size());

  return true;
}

}  // namespace storage
}  // namespace esphome
















