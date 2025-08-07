#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.sd_image";

// ======== StorageComponent Implementation ========

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not set!");
    this->mark_failed();
    return;
  }
  
  ESP_LOGD(TAG, "Platform: %s", platform_.c_str());
  if (cache_size_ > 0) {
    ESP_LOGD(TAG, "Cache size: %zu bytes", cache_size_);
  }
  
  ESP_LOGCONFIG(TAG, "Storage Component setup complete");
}

void StorageComponent::loop() {
  // Rien pour le moment
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Cache Size: %zu bytes", cache_size_);
  ESP_LOGCONFIG(TAG, "  SD Component: %s", sd_component_ ? "Connected" : "Not Connected");
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  return sd_component_->file_size(path) > 0;
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return {};
  }
  
  return sd_component_->read_file(path);
}

bool StorageComponent::write_file_direct(const std::string &path, const std::vector<uint8_t> &data) {
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  return sd_component_->write_file(path, data);
}

size_t StorageComponent::get_file_size(const std::string &path) {
  if (!sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return 0;
  }
  
  return sd_component_->file_size(path);
}

// ======== SdImageComponent Implementation ========

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  
  if (!storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not set!");
    this->mark_failed();
    return;
  }
  
  if (!validate_dimensions()) {
    ESP_LOGE(TAG_IMAGE, "Invalid image dimensions: %dx%d", width_, height_);
    this->mark_failed();
    return;
  }
  
  if (!validate_file_path()) {
    ESP_LOGE(TAG_IMAGE, "Invalid file path: %s", file_path_.c_str());
    this->mark_failed();
    return;
  }
  
  // Vérifier si le fichier existe sur la SD
  if (!storage_component_->file_exists_direct(file_path_)) {
    ESP_LOGW(TAG_IMAGE, "Image file does not exist: %s", file_path_.c_str());
    // Ne pas marquer comme failed, le fichier pourrait être ajouté plus tard
  } else {
    ESP_LOGI(TAG_IMAGE, "Image file found: %s", file_path_.c_str());
  }
  
  // Précharger l'image si demandé
  if (preload_) {
    if (load_image()) {
      ESP_LOGI(TAG_IMAGE, "Image preloaded successfully");
    } else {
      ESP_LOGW(TAG_IMAGE, "Failed to preload image");
    }
  }
  
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component setup complete");
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File Path: %s", file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", width_, height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", get_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Byte Order: %s", 
                byte_order_ == ByteOrder::LITTLE_ENDIAN ? "Little Endian" : "Big Endian");
  ESP_LOGCONFIG(TAG_IMAGE, "  Expected Size: %zu bytes", expected_data_size_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Cache Enabled: %s", cache_enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Preload: %s", preload_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Currently Loaded: %s", is_loaded_ ? "YES" : "NO");
  
  if (is_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Memory Usage: %zu bytes", get_memory_usage());
  }
}

void SdImageComponent::set_format_string(const std::string &format) {
  if (format == "RGB565") format_ = ImageFormat::RGB565;
  else if (format == "RGB888") format_ = ImageFormat::RGB888;
  else if (format == "RGBA") format_ = ImageFormat::RGBA;
  else if (format == "GRAYSCALE") format_ = ImageFormat::GRAYSCALE;
  else if (format == "BINARY") format_ = ImageFormat::BINARY;
  else format_ = ImageFormat::RGB565; // default
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  if (byte_order == "BIG_ENDIAN") byte_order_ = ByteOrder::BIG_ENDIAN;
  else byte_order_ = ByteOrder::LITTLE_ENDIAN; // default
}

bool SdImageComponent::load_image() {
  return load_image_from_path(file_path_);
}

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGD(TAG_IMAGE, "Loading image from: %s", path.c_str());
  
  if (!storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not available");
    return false;
  }
  
  // Libérer l'image précédente si chargée
  if (is_loaded_) {
    unload_image();
  }
  
  // Vérifier si le fichier existe
  if (!storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "Image file not found: %s", path.c_str());
    return false;
  }
  
  // Lire les données depuis la SD
  std::vector<uint8_t> data = storage_component_->read_file_direct(path);
  
  if (data.empty()) {
    ESP_LOGE(TAG_IMAGE, "Failed to read image file: %s", path.c_str());
    return false;
  }
  
  // Vérifier la taille des données
  if (expected_data_size_ > 0 && data.size() != expected_data_size_) {
    ESP_LOGW(TAG_IMAGE, "Image size mismatch. Expected: %zu, Got: %zu", 
             expected_data_size_, data.size());
    // Continuer quand même, mais avec avertissement
  }
  
  // Conversion de l'ordre des bytes si nécessaire
  if (byte_order_ == ByteOrder::BIG_ENDIAN && get_pixel_size() > 1) {
    convert_byte_order(data);
  }
  
  // Stocker les données
  if (cache_enabled_) {
    image_data_ = std::move(data);
    is_loaded_ = true;
    ESP_LOGD(TAG_IMAGE, "Image loaded and cached: %zu bytes", image_data_.size());
  } else {
    // Mode streaming - pas de cache
    streaming_mode_ = true;
    is_loaded_ = true;
    ESP_LOGD(TAG_IMAGE, "Image loaded in streaming mode");
  }
  
  // Mettre à jour le chemin actuel
  file_path_ = path;
  
  return true;
}

void SdImageComponent::unload_image() {
  ESP_LOGD(TAG_IMAGE, "Unloading image");
  
  if (cache_enabled_) {
    image_data_.clear();
    image_data_.shrink_to_fit();
  }
  
  is_loaded_ = false;
  streaming_mode_ = false;
  
  ESP_LOGD(TAG_IMAGE, "Image unloaded");
}

bool SdImageComponent::reload_image() {
  ESP_LOGD(TAG_IMAGE, "Reloading image");
  return load_image_from_path(file_path_);
}

void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  // Vérification des bornes
  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    red = green = blue = alpha = 0;
    return;
  }
  
  if (streaming_mode_) {
    get_pixel_streamed(x, y, red, green, blue, alpha);
    return;
  }
  
  if (!is_loaded_ || image_data_.empty()) {
    red = green = blue = alpha = 0;
    return;
  }
  
  size_t offset = get_pixel_offset(x, y);
  if (offset + get_pixel_size() > image_data_.size()) {
    ESP_LOGE(TAG_IMAGE, "Pixel offset out of bounds: %zu", offset);
    red = green = blue = alpha = 0;
    return;
  }
  
  const uint8_t *pixel_data = &image_data_[offset];
  convert_pixel_format(x, y, pixel_data, red, green, blue, alpha);
}

void SdImageComponent::get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  // Pour le mode streaming, lire directement depuis la SD
  if (!storage_component_) {
    red = green = blue = alpha = 0;
    return;
  }
  
  size_t offset = get_pixel_offset(x, y);
  size_t pixel_size = get_pixel_size();
  
  // Lire seulement les bytes nécessaires pour ce pixel
  std::vector<uint8_t> data = storage_component_->read_file_direct(file_path_);
  
  if (data.size() <= offset + pixel_size) {
    red = green = blue = alpha = 0;
    return;
  }
  
  const uint8_t *pixel_data = &data[offset];
  convert_pixel_format(x, y, pixel_data, red, green, blue, alpha);
}

void SdImageComponent::convert_pixel_format(int x, int y, const uint8_t *pixel_data,
                                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  switch (format_) {
    case ImageFormat::RGB565: {
      uint16_t pixel = (pixel_data[1] << 8) | pixel_data[0];
      red = ((pixel >> 11) & 0x1F) << 3;
      green = ((pixel >> 5) & 0x3F) << 2;
      blue = (pixel & 0x1F) << 3;
      alpha = 255;
      break;
    }
    case ImageFormat::RGB888:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = 255;
      break;
    case ImageFormat::RGBA:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = pixel_data[3];
      break;
    case ImageFormat::GRAYSCALE:
      red = green = blue = pixel_data[0];
      alpha = 255;
      break;
    case ImageFormat::BINARY: {
      int byte_index = (y * width_ + x) / 8;
      int bit_index = (y * width_ + x) % 8;
      bool pixel_on = (pixel_data[byte_index] >> (7 - bit_index)) & 1;
      red = green = blue = pixel_on ? 255 : 0;
      alpha = 255;
      break;
    }
  }
}

size_t SdImageComponent::get_pixel_size() const {
  switch (format_) {
    case ImageFormat::RGB565:
      return 2;
    case ImageFormat::RGB888:
      return 3;
    case ImageFormat::RGBA:
      return 4;
    case ImageFormat::GRAYSCALE:
      return 1;
    case ImageFormat::BINARY:
      return 1; // Géré spécialement
    default:
      return 2;
  }
}

size_t SdImageComponent::get_pixel_offset(int x, int y) const {
  if (format_ == ImageFormat::BINARY) {
    return (y * width_ + x) / 8;
  }
  return (y * width_ + x) * get_pixel_size();
}

void SdImageComponent::convert_byte_order(std::vector<uint8_t> &data) {
  size_t pixel_size = get_pixel_size();
  
  if (pixel_size <= 1) return;
  
  for (size_t i = 0; i < data.size(); i += pixel_size) {
    if (pixel_size == 2) {
      std::swap(data[i], data[i + 1]);
    } else if (pixel_size == 4) {
      std::swap(data[i], data[i + 3]);
      std::swap(data[i + 1], data[i + 2]);
    }
  }
}

bool SdImageComponent::validate_dimensions() const {
  return width_ > 0 && height_ > 0 && width_ <= 1024 && height_ <= 768;
}

bool SdImageComponent::validate_file_path() const {
  return !file_path_.empty() && file_path_[0] == '/';
}

size_t SdImageComponent::calculate_expected_size() const {
  if (format_ == ImageFormat::BINARY) {
    return (width_ * height_ + 7) / 8;
  }
  return width_ * height_ * get_pixel_size();
}

std::string SdImageComponent::get_format_string() const {
  switch (format_) {
    case ImageFormat::RGB565: return "RGB565";
    case ImageFormat::RGB888: return "RGB888";
    case ImageFormat::RGBA: return "RGBA";
    case ImageFormat::GRAYSCALE: return "Grayscale";
    case ImageFormat::BINARY: return "Binary";
    default: return "Unknown";
  }
}

bool SdImageComponent::validate_image_data() const {
  if (!is_loaded_) return false;
  
  size_t expected_size = calculate_expected_size();
  if (cache_enabled_) {
    return image_data_.size() == expected_size;
  }
  
  // Pour le mode streaming, vérifier que le fichier existe et a la bonne taille
  std::vector<uint8_t> data = storage_component_->read_file_direct(file_path_);
  return data.size() == expected_size;
}

void SdImageComponent::free_cache() {
  if (cache_enabled_) {
    image_data_.clear();
    image_data_.shrink_to_fit();
  }
}

bool SdImageComponent::read_image_from_storage() {
  if (!storage_component_) {
    return false;
  }
  
  std::vector<uint8_t> data = storage_component_->read_file_direct(file_path_);
  if (data.empty()) {
    return false;
  }
  
  image_data_ = std::move(data);
  return true;
}

}  // namespace storage
}  // namespace esphome








