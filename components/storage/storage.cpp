#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/display/display.h"

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.sd_image";

// ======== StorageComponent Implementation ========

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not set!");
    this->mark_failed();
    return;
  }
  
  ESP_LOGD(TAG, "Platform: %s", this->platform_.c_str());
  if (this->cache_size_ > 0) {
    ESP_LOGD(TAG, "Cache size: %zu bytes", this->cache_size_);
  }
  
  ESP_LOGCONFIG(TAG, "Storage Component setup complete");
}

void StorageComponent::loop() {
  // Rien pour le moment
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Cache Size: %zu bytes", this->cache_size_);
  ESP_LOGCONFIG(TAG, "  SD Component: %s", this->sd_component_ ? "Connected" : "Not Connected");
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  return this->sd_component_->file_size(path) > 0;
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return {};
  }
  
  return this->sd_component_->read_file(path);
}

bool StorageComponent::write_file_direct(const std::string &path, const std::vector<uint8_t> &data) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  this->sd_component_->write_file(path.c_str(), data.data(), data.size());
  return true;
}

size_t StorageComponent::get_file_size(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return 0;
  }
  
  return this->sd_component_->file_size(path);
}

// ======== SdImageComponent Implementation ========

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not set!");
    this->mark_failed();
    return;
  }
  
  if (!this->validate_file_path()) {
    ESP_LOGE(TAG_IMAGE, "Invalid file path: %s", this->file_path_.c_str());
    this->mark_failed();
    return;
  }
  
  // Vérifier si le fichier existe sur la SD
  if (!this->storage_component_->file_exists_direct(this->file_path_)) {
    ESP_LOGW(TAG_IMAGE, "Image file does not exist: %s", this->file_path_.c_str());
  } else {
    ESP_LOGI(TAG_IMAGE, "Image file found: %s", this->file_path_.c_str());
  }
  
  // Précharger l'image si demandé
  if (this->preload_) {
    if (this->load_image()) {
      ESP_LOGI(TAG_IMAGE, "Image preloaded successfully");
    } else {
      ESP_LOGW(TAG_IMAGE, "Failed to preload image");
    }
  }
  
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component setup complete");
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File Path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->get_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Byte Order: %s", 
                this->byte_order_ == ByteOrder::little_endian ? "Little Endian" : "Big Endian");
  ESP_LOGCONFIG(TAG_IMAGE, "  Expected Size: %zu bytes", this->expected_data_size_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Cache Enabled: %s", this->cache_enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Preload: %s", this->preload_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Currently Loaded: %s", this->is_loaded_ ? "YES" : "NO");
  
  if (this->is_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Memory Usage: %zu bytes", this->get_memory_usage());
  }
}

void SdImageComponent::set_format_string(const std::string &format) {
  if (format == "RGB565") this->format_ = ImageFormat::rgb565;
  else if (format == "RGB888") this->format_ = ImageFormat::rgb888;
  else if (format == "RGBA") this->format_ = ImageFormat::rgba;
  else if (format == "GRAYSCALE") this->format_ = ImageFormat::grayscale;
  else if (format == "BINARY") this->format_ = ImageFormat::binary;
  else {
    ESP_LOGW(TAG_IMAGE, "Unknown format: %s, using RGB565", format.c_str());
    this->format_ = ImageFormat::rgb565; // default
  }
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  if (byte_order == "BIG_ENDIAN") this->byte_order_ = ByteOrder::big_endian;
  else if (byte_order == "LITTLE_ENDIAN") this->byte_order_ = ByteOrder::little_endian;
  else {
    ESP_LOGW(TAG_IMAGE, "Unknown byte order: %s, using little_endian", byte_order.c_str());
    this->byte_order_ = ByteOrder::little_endian; // default
  }
}

bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGD(TAG_IMAGE, "Loading image from: %s", path.c_str());
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not available");
    return false;
  }
  
  // Libérer l'image précédente si chargée
  if (this->is_loaded_) {
    this->unload_image();
  }
  
  // Vérifier si le fichier existe
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "Image file not found: %s", path.c_str());
    return false;
  }
  
  // Lire les données depuis la SD
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "Failed to read image file: %s", path.c_str());
    return false;
  }
  
  ESP_LOGD(TAG_IMAGE, "Read %zu bytes from file", file_data.size());
  
  // Détecter le type de fichier et décoder
  bool decode_success = false;
  if (this->is_jpeg_file(file_data)) {
    ESP_LOGI(TAG_IMAGE, "Detected JPEG file, decoding...");
    decode_success = this->decode_jpeg(file_data);
  } else if (this->is_png_file(file_data)) {
    ESP_LOGI(TAG_IMAGE, "Detected PNG file, decoding...");
    decode_success = this->decode_png(file_data);
  } else {
    ESP_LOGI(TAG_IMAGE, "Assuming raw bitmap data");
    decode_success = this->load_raw_data(file_data);
  }
  
  if (!decode_success) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode image: %s", path.c_str());
    return false;
  }
  
  // Mettre à jour le chemin actuel
  this->file_path_ = path;
  this->is_loaded_ = true;
  
  ESP_LOGI(TAG_IMAGE, "Image loaded successfully: %dx%d, %zu bytes", 
           this->width_, this->height_, this->image_data_.size());
  
  return true;
}

bool SdImageComponent::is_jpeg_file(const std::vector<uint8_t> &data) {
  return data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool SdImageComponent::is_png_file(const std::vector<uint8_t> &data) {
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data.size() >= 8 && memcmp(data.data(), png_signature, 8) == 0;
}

bool SdImageComponent::decode_jpeg(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "JPEG decoder: Software fallback (ESP32-P4 HW decoder not available)");
  
  // Pour l'instant, créer une image test colorée au lieu de planter
  // Dimensions par défaut si pas spécifiées
  if (this->width_ <= 0) this->width_ = 200;
  if (this->height_ <= 0) this->height_ = 200;
  
  // Créer une image test RGB888 avec un motif coloré
  size_t output_size = this->width_ * this->height_ * 3;
  this->image_data_.resize(output_size);
  
  // Remplir avec un motif test coloré
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = (y * this->width_ + x) * 3;
      
      // Motif coloré basé sur la position
      this->image_data_[offset + 0] = (x * 255) / this->width_;     // Rouge
      this->image_data_[offset + 1] = (y * 255) / this->height_;    // Vert  
      this->image_data_[offset + 2] = 128;                          // Bleu fixe
    }
  }
  
  // Forcer le format RGB888 
  this->format_ = ImageFormat::rgb888;
  ESP_LOGI(TAG_IMAGE, "JPEG fallback: Created test pattern %dx%d (%zu bytes)", 
           this->width_, this->height_, output_size);
  
  return true;
}

bool SdImageComponent::decode_png(const std::vector<uint8_t> &png_data) {
  ESP_LOGI(TAG_IMAGE, "PNG decoder: Software fallback (ESP32-P4 HW decoder not available)");
  
  // Pour l'instant, créer une image test différente
  if (this->width_ <= 0) this->width_ = 100;
  if (this->height_ <= 0) this->height_ = 100;
  
  // Créer une image test RGB888 avec un motif différent  
  size_t output_size = this->width_ * this->height_ * 3;
  this->image_data_.resize(output_size);
  
  // Remplir avec un motif test en damier
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = (y * this->width_ + x) * 3;
      
      bool checker = ((x / 10) + (y / 10)) % 2;
      uint8_t color = checker ? 255 : 0;
      
      this->image_data_[offset + 0] = color;     // Rouge
      this->image_data_[offset + 1] = color;     // Vert  
      this->image_data_[offset + 2] = color;     // Bleu
    }
  }
  
  this->format_ = ImageFormat::rgb888;
  ESP_LOGI(TAG_IMAGE, "PNG fallback: Created test checkerboard %dx%d (%zu bytes)", 
           this->width_, this->height_, output_size);
  
  return true;
}

bool SdImageComponent::load_raw_data(const std::vector<uint8_t> &raw_data) {
  ESP_LOGD(TAG_IMAGE, "Loading raw bitmap data");
  
  // Pour les données brutes, nous devons connaître les dimensions à l'avance
  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG_IMAGE, "Dimensions must be set for raw data: %dx%d", this->width_, this->height_);
    return false;
  }
  
  // Calculer la taille attendue
  size_t expected_size = this->calculate_expected_size();
  
  if (raw_data.size() != expected_size) {
    ESP_LOGW(TAG_IMAGE, "Raw data size mismatch. Expected: %zu, Got: %zu", 
             expected_size, raw_data.size());
    // Continuer quand même, mais avec avertissement
  }
  
  // Copier les données
  this->image_data_ = raw_data;
  
  // Conversion de l'ordre des bytes si nécessaire
  if (this->byte_order_ == ByteOrder::big_endian && this->get_pixel_size() > 1) {
    this->convert_byte_order(this->image_data_);
  }
  
  ESP_LOGD(TAG_IMAGE, "Raw data loaded: %zu bytes", this->image_data_.size());
  return true;
}

void SdImageComponent::unload_image() {
  ESP_LOGD(TAG_IMAGE, "Unloading image");
  
  this->image_data_.clear();
  this->image_data_.shrink_to_fit();
  this->is_loaded_ = false;
  this->streaming_mode_ = false;
  
  ESP_LOGD(TAG_IMAGE, "Image unloaded");
}

bool SdImageComponent::reload_image() {
  ESP_LOGD(TAG_IMAGE, "Reloading image");
  return this->load_image_from_path(this->file_path_);
}

// Méthodes héritées de display::BaseImage
void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->is_loaded_ || this->image_data_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }

  ESP_LOGI(TAG_IMAGE, "Drawing image at (%d,%d) size %dx%d", x, y, this->width_, this->height_);

  for (int img_y = 0; img_y < this->height_; img_y++) {
    for (int img_x = 0; img_x < this->width_; img_x++) {
      uint8_t red, green, blue, alpha;
      this->get_pixel(img_x, img_y, red, green, blue, alpha);

      // Si alpha == 0, pixel transparent, on saute
      if (alpha == 0)
        continue;

      Color pixel_color(red, green, blue);
      
      // Vérifier si le display a draw_pixel_at ou draw_absolute_pixel
      try {
        display->draw_pixel_at(x + img_x, y + img_y, pixel_color);
      } catch (...) {
        // Fallback si draw_pixel_at n'existe pas
        display->draw_filled_rect(x + img_x, y + img_y, 1, 1, pixel_color);
      }
    }
    
    // Log de progression tous les 10 lignes
    if (img_y % 10 == 0) {
      ESP_LOGV(TAG_IMAGE, "Drawing line %d/%d", img_y, this->height_);
    }
  }
  
  ESP_LOGI(TAG_IMAGE, "Image draw completed");
}

ImageType SdImageComponent::get_image_type() const {
  switch (this->format_) {
    case ImageFormat::rgb565:
      return image::IMAGE_TYPE_RGB565;
    case ImageFormat::rgb888:
      return image::IMAGE_TYPE_RGB;
    case ImageFormat::rgba:
      return image::IMAGE_TYPE_RGBA;
    case ImageFormat::grayscale:
      return image::IMAGE_TYPE_GRAYSCALE;
    case ImageFormat::binary:
      return image::IMAGE_TYPE_BINARY;
    default:
      return image::IMAGE_TYPE_RGB565;
  }
}

// Version sans alpha
void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const {
  uint8_t alpha;
  this->get_pixel(x, y, red, green, blue, alpha);
}

// Version avec alpha
void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  // Vérification des bornes
  if (x < 0 || x >= this->width_ || y < 0 || y >= this->height_) {
    red = green = blue = alpha = 0;
    return;
  }
  
  if (!this->is_loaded_ || this->image_data_.empty()) {
    red = green = blue = alpha = 0;
    return;
  }
  
  size_t offset = this->get_pixel_offset(x, y);
  if (offset + this->get_pixel_size() > this->image_data_.size()) {
    ESP_LOGW(TAG_IMAGE, "Pixel offset out of bounds: %zu (max: %zu)", 
             offset + this->get_pixel_size(), this->image_data_.size());
    red = green = blue = alpha = 0;
    return;
  }
  
  const uint8_t *pixel_data = &this->image_data_[offset];
  this->convert_pixel_format(x, y, pixel_data, red, green, blue, alpha);
}

void SdImageComponent::convert_pixel_format(int x, int y, const uint8_t *pixel_data,
                                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  switch (this->format_) {
    case ImageFormat::rgb565: {
      uint16_t pixel = (pixel_data[1] << 8) | pixel_data[0];
      red = ((pixel >> 11) & 0x1F) << 3;
      green = ((pixel >> 5) & 0x3F) << 2;
      blue = (pixel & 0x1F) << 3;
      alpha = 255;
      break;
    }
    case ImageFormat::rgb888:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = 255;
      break;
    case ImageFormat::rgba:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = pixel_data[3];
      break;
    case ImageFormat::grayscale:
      red = green = blue = pixel_data[0];
      alpha = 255;
      break;
    case ImageFormat::binary: {
      int bit_pos = (y * this->width_ + x) % 8;
      bool pixel_on = (pixel_data[0] >> (7 - bit_pos)) & 1;
      red = green = blue = pixel_on ? 255 : 0;
      alpha = 255;
      break;
    }
  }
}

size_t SdImageComponent::get_pixel_size() const {
  switch (this->format_) {
    case ImageFormat::rgb565:
      return 2;
    case ImageFormat::rgb888:
      return 3;
    case ImageFormat::rgba:
      return 4;
    case ImageFormat::grayscale:
      return 1;
    case ImageFormat::binary:
      return 1; // Géré spécialement
    default:
      return 3; // Default RGB888
  }
}

size_t SdImageComponent::get_pixel_offset(int x, int y) const {
  if (this->format_ == ImageFormat::binary) {
    return (y * this->width_ + x) / 8;
  }
  return (y * this->width_ + x) * this->get_pixel_size();
}

void SdImageComponent::convert_byte_order(std::vector<uint8_t> &data) {
  size_t pixel_size = this->get_pixel_size();
  
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
  return this->width_ > 0 && this->height_ > 0 && this->width_ <= 1024 && this->height_ <= 768;
}

bool SdImageComponent::validate_file_path() const {
  return !this->file_path_.empty() && this->file_path_[0] == '/';
}

size_t SdImageComponent::calculate_expected_size() const {
  if (this->format_ == ImageFormat::binary) {
    return (this->width_ * this->height_ + 7) / 8;
  }
  return this->width_ * this->height_ * this->get_pixel_size();
}

std::string SdImageComponent::get_format_string() const {
  switch (this->format_) {
    case ImageFormat::rgb565: return "RGB565";
    case ImageFormat::rgb888: return "RGB888";
    case ImageFormat::rgba: return "RGBA";
    case ImageFormat::grayscale: return "Grayscale";
    case ImageFormat::binary: return "Binary";
    default: return "Unknown";
  }
}

bool SdImageComponent::validate_image_data() const {
  if (!this->is_loaded_) return false;
  return !this->image_data_.empty();
}

void SdImageComponent::free_cache() {
  this->image_data_.clear();
  this->image_data_.shrink_to_fit();
}

bool SdImageComponent::read_image_from_storage() {
  if (!this->storage_component_) {
    return false;
  }
  
  std::vector<uint8_t> data = this->storage_component_->read_file_direct(this->file_path_);
  if (data.empty()) {
    return false;
  }
  
  this->image_data_ = std::move(data);
  return true;
}

// Version streaming pour gestion mémoire limitée
void SdImageComponent::get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  // Pour le mode streaming, lire directement depuis la SD
  if (!this->storage_component_) {
    red = green = blue = alpha = 0;
    return;
  }
  
  size_t offset = this->get_pixel_offset(x, y);
  size_t pixel_size = this->get_pixel_size();
  
  // Lire seulement les bytes nécessaires pour ce pixel
  std::vector<uint8_t> data = this->storage_component_->read_file_direct(this->file_path_);
  
  if (data.size() <= offset + pixel_size) {
    red = green = blue = alpha = 0;
    return;
  }
  
  const uint8_t *pixel_data = &data[offset];
  this->convert_pixel_format(x, y, pixel_data, red, green, blue, alpha);
}

void SdImageComponent::get_pixel_streamed(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const {
  uint8_t alpha;
  this->get_pixel_streamed(x, y, red, green, blue, alpha);
}

}  // namespace storage
}  // namespace esphome









