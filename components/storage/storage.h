#pragma once
#include <string>
#include <vector>
#include <memory>
#include "esphome/core/component.h"
#include "esphome/components/image/image.h"
#include "../storage/storage.h"
#include "../sd_mmc_card/sd_mmc_card.h"

namespace esphome {
namespace sd_image {

// Énumérations pour les formats d'image
enum class ImageFormat {
  RGB565,
  RGB888,
  RGBA,
  GRAYSCALE,
  BINARY
};

enum class ByteOrder {
  LITTLE_ENDIAN,
  BIG_ENDIAN
};

class SdImageComponent : public image::Image, public Component {
 public:
  SdImageComponent() = default;
  
  void setup() override;
  void loop() override {}
  void dump_config() override;
  
  // Configuration de base
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_width(int width) { width_ = width; }
  void set_height(int height) { height_ = height; }
  void set_format(ImageFormat format) { format_ = format; }
  void set_byte_order(ByteOrder byte_order) { byte_order_ = byte_order; }
  void set_expected_data_size(size_t size) { expected_data_size_ = size; }
  
  // Configuration avancée
  void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
  void set_preload(bool preload) { preload_ = preload; }
  void set_storage_component(storage::StorageComponent *storage) { storage_component_ = storage; }
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { sd_component_ = sd_component; }
  
  // Getters
  const std::string &get_file_path() const { return file_path_; }
  int get_width() const override { return width_; }
  int get_height() const override { return height_; }
  ImageFormat get_format() const { return format_; }
  ByteOrder get_byte_order() const { return byte_order_; }
  bool is_loaded() const { return is_loaded_; }
  bool is_cache_enabled() const { return cache_enabled_; }
  
  // Méthodes principales
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Interface Image ESPHome
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const override;
  const uint8_t *get_data() const override { return image_data_.data(); }
  size_t get_data_size() const override { return image_data_.size(); }
  image::ImageType get_type() const override;
  
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
  storage::StorageComponent *storage_component_{nullptr};
  sd_mmc_card::SdMmc *sd_component_{nullptr};  // Référence directe au composant SD
  
  // Méthodes privées
  bool read_image_from_sd_direct();  // Lecture directe depuis le composant SD
  bool read_image_from_storage();    // Lecture via le composant storage
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
class SdImageLoadAction : public Action<> {
 public:
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_parent(SdImageComponent *parent) { parent_ = parent; }
  
  void play(Ts... x) override {
    if (file_path_.empty()) {
      parent_->load_image();
    } else {
      parent_->load_image_from_path(file_path_);
    }
  }

 private:
  SdImageComponent *parent_;
  std::string file_path_;
};

class SdImageUnloadAction : public Action<> {
 public:
  explicit SdImageUnloadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_parent(SdImageComponent *parent) { parent_ = parent; }
  
  void play(Ts... x) override {
    parent_->unload_image();
  }

 private:
  SdImageComponent *parent_;
};

}  // namespace sd_image
}  // namespace esphome

















