import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PLATFORM, CONF_WIDTH, CONF_HEIGHT, CONF_FORMAT
from esphome import automation
from esphome.components import image

DEPENDENCIES = ['sd_mmc_card', 'image']
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_STORAGE = "storage"
CONF_PATH = "path"
CONF_CHUNK_SIZE = "chunk_size"

# Constants pour SD direct
CONF_SD_COMPONENT = "sd_component"
CONF_CACHE_SIZE = "cache_size"

# Constants pour les images SD
CONF_SD_IMAGES = "sd_images"

CONF_FILE_PATH = "file_path"
CONF_BYTE_ORDER = "byte_order"
CONF_CACHE_ENABLED = "cache_enabled"
CONF_PRELOAD = "preload"
CONF_AUTO_RESIZE = "auto_resize"

storage_ns = cg.esphome_ns.namespace('storage')
StorageComponent = storage_ns.class_('StorageComponent', cg.Component)
SdImageComponent = storage_ns.class_('SdImageComponent', cg.Component, image.Image_)

SdImageLoadAction = storage_ns.class_('SdImageLoadAction', automation.Action)
SdImageUnloadAction = storage_ns.class_('SdImageUnloadAction', automation.Action)

# Formats d'image en minuscules (pour YAML) -> valeurs pour C++
IMAGE_FORMAT = {
    "rgb565": "RGB565",
    "rgb888": "RGB888", 
    "rgba": "RGBA",
    "grayscale": "GRAYSCALE",
    "binary": "BINARY",
}

# Ordre des bytes en minuscules (pour YAML) -> valeurs pour C++
BYTE_ORDER = {
    "little_endian": "LITTLE_ENDIAN",
    "big_endian": "BIG_ENDIAN",
}

# Extensions de fichiers supportées
SUPPORTED_IMAGE_EXTENSIONS = ['.jpg', '.jpeg', '.png', '.rgb', '.rgb565', '.rgb888', '.rgba', '.gray', '.bin']

def validate_image_file_extension(value):
    """Valide l'extension du fichier image"""
    import os
    ext = os.path.splitext(value.lower())[1]
    if ext not in SUPPORTED_IMAGE_EXTENSIONS:
        raise cv.Invalid(f"Unsupported image format. Supported: {', '.join(SUPPORTED_IMAGE_EXTENSIONS)}")
    return value

def is_compressed_format(file_path):
    """Détermine si le fichier est dans un format compressé"""
    import os
    ext = os.path.splitext(file_path.lower())[1]
    return ext in ['.jpg', '.jpeg', '.png']

SD_IMAGE_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(SdImageComponent),
    cv.Required(CONF_FILE_PATH): cv.All(cv.string, validate_image_file_extension),
    cv.Optional(CONF_WIDTH): cv.positive_int,  # Optionnel pour images compressées
    cv.Optional(CONF_HEIGHT): cv.positive_int, # Optionnel pour images compressées
    cv.Optional(CONF_FORMAT, default="rgb888"): cv.enum(IMAGE_FORMAT, lower=True),
    cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDER, lower=True),
    cv.Optional(CONF_CACHE_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
    cv.Optional(CONF_AUTO_RESIZE, default=True): cv.boolean,  # Nouveau: auto-dimensionnement
    # Ajouter le type pour la compatibilité LVGL
    cv.Optional("type"): cv.enum(IMAGE_FORMAT, upper=True),
}).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_direct", lower=True),
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_SD_COMPONENT): cv.use_id(cg.Component),
    cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,
    cv.Optional(CONF_SD_IMAGES, default=[]): cv.ensure_list(SD_IMAGE_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)

def validate_image_config(img_config):
    """Valide la configuration d'une image"""
    file_path = img_config[CONF_FILE_PATH]
    
    # Vérifier le chemin absolu
    if not file_path.startswith("/"):
        raise cv.Invalid("Image file path must be absolute (start with '/')")
    
    # Pour les formats compressés, les dimensions sont optionnelles (auto-détection)
    if is_compressed_format(file_path):
        # Si les dimensions ne sont pas spécifiées, elles seront auto-détectées
        if CONF_WIDTH not in img_config:
            img_config[CONF_WIDTH] = 0  # 0 = auto-détection
        if CONF_HEIGHT not in img_config:
            img_config[CONF_HEIGHT] = 0  # 0 = auto-détection
            
        # Avertir si les dimensions sont spécifiées pour un format compressé
        if img_config[CONF_WIDTH] > 0 or img_config[CONF_HEIGHT] > 0:
            if not img_config.get(CONF_AUTO_RESIZE, True):
                print(f"WARNING: Dimensions specified for compressed format {file_path}. "
                      "Consider enabling auto_resize or omitting dimensions.")
    else:
        # Pour les formats bruts, les dimensions sont obligatoires
        if CONF_WIDTH not in img_config or CONF_HEIGHT not in img_config:
            raise cv.Invalid("Width and height are required for raw image formats")
        
        if img_config[CONF_WIDTH] <= 0 or img_config[CONF_HEIGHT] <= 0:
            raise cv.Invalid("Width and height must be positive for raw image formats")
        
        # Vérifier la taille maximale pour les formats bruts
        if img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] > 2048 * 2048:
            raise cv.Invalid("Image dimensions too large (max 2048x2048)")
    
    # Auto-définir le type basé sur le format si pas défini
    if "type" not in img_config:
        img_config["type"] = IMAGE_FORMAT[img_config[CONF_FORMAT]]
    
    return img_config

def validate_storage_config(config):
    """Valide la configuration du storage"""
    if CONF_SD_IMAGES in config:
        for img_config in config[CONF_SD_IMAGES]:
            validate_image_config(img_config)
    return config

# Application des validations
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_storage_config)
SD_IMAGE_SCHEMA = cv.All(SD_IMAGE_SCHEMA, validate_image_config)

async def to_code(config):
    # Création du composant principal
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration du composant
    cg.add(var.set_platform(config[CONF_PLATFORM]))

    # Récupération du composant SD
    sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
    cg.add(var.set_sd_component(sd_component))

    # Configuration du cache
    if config[CONF_CACHE_SIZE] > 0:
        cg.add(var.set_cache_size(config[CONF_CACHE_SIZE]))

    # Traitement des images SD
    if CONF_SD_IMAGES in config and config[CONF_SD_IMAGES]:
        for img_config in config[CONF_SD_IMAGES]:
            await process_sd_image_config(img_config, var)
        
        cg.add_define("USE_SD_IMAGE")
        
        # Activer les décodeurs selon les besoins
        has_jpeg = any(is_compressed_format(img[CONF_FILE_PATH]) and 
                      img[CONF_FILE_PATH].lower().endswith(('.jpg', '.jpeg'))
                      for img in config[CONF_SD_IMAGES])
        
        has_png = any(is_compressed_format(img[CONF_FILE_PATH]) and 
                     img[CONF_FILE_PATH].lower().endswith('.png')
                     for img in config[CONF_SD_IMAGES])
        
        if has_jpeg:
            cg.add_build_flag("-DUSE_JPEG_DECODER")
            print("INFO: JPEG decoder will be enabled")
            
        if has_png:
            cg.add_build_flag("-DUSE_PNG_DECODER")
            cg.add_library("lodepng", None)  # Ajouter la bibliothèque PNG
            print("INFO: PNG decoder will be enabled (requires lodepng library)")

async def process_sd_image_config(img_config, storage_component):
    # Création ET enregistrement du composant image
    img_var = cg.new_Pvariable(img_config[CONF_ID])
    await cg.register_component(img_var, img_config)

    # Configuration de base
    cg.add(img_var.set_storage_component(storage_component))
    cg.add(img_var.set_file_path(img_config[CONF_FILE_PATH]))
    
    # Dimensions (peuvent être 0 pour auto-détection)
    cg.add(img_var.set_width(img_config[CONF_WIDTH]))
    cg.add(img_var.set_height(img_config[CONF_HEIGHT]))

    # Configuration du format - conversion minuscule YAML -> majuscule C++
    format_str = IMAGE_FORMAT[img_config[CONF_FORMAT]]
    cg.add(img_var.set_format_string(format_str))

    # Configuration de l'ordre des bytes - conversion minuscule YAML -> majuscule C++
    byte_order_str = BYTE_ORDER[img_config[CONF_BYTE_ORDER]]
    cg.add(img_var.set_byte_order_string(byte_order_str))

    # Configuration des options
    cg.add(img_var.set_cache_enabled(img_config[CONF_CACHE_ENABLED]))
    cg.add(img_var.set_preload(img_config[CONF_PRELOAD]))
    cg.add(img_var.set_auto_resize(img_config[CONF_AUTO_RESIZE]))

    # Calcul de la taille des données seulement pour les formats bruts
    if not is_compressed_format(img_config[CONF_FILE_PATH]):
        format_sizes = {
            "rgb565": 2,
            "rgb888": 3,
            "rgba": 4,
            "grayscale": 1,
            "binary": 1,
        }

        format_key = img_config[CONF_FORMAT]
        width = img_config[CONF_WIDTH]
        height = img_config[CONF_HEIGHT]
        
        if format_key == "binary":
            data_size = (width * height + 7) // 8
        else:
            data_size = width * height * format_sizes[format_key]

        cg.add(img_var.set_expected_data_size(data_size))
    else:
        # Pour les formats compressés, la taille sera déterminée dynamiquement
        cg.add(img_var.set_expected_data_size(0))
    
    # Enregistrer comme image pour ESPHome
    cg.add_global(cg.RawStatement(f"// Register {img_config[CONF_ID].id} as compressed/raw image"))
    
    return img_var

@automation.register_action(
    "sd_image.load",
    SdImageLoadAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SdImageComponent),
        cv.Optional("file_path"): cv.templatable(cv.string),
    })
)
async def sd_image_load_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    if "file_path" in config:
        template_ = await cg.templatable(config["file_path"], args, cg.std_string)
        cg.add(var.set_file_path(template_))
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var

@automation.register_action(
    "sd_image.unload",
    SdImageUnloadAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SdImageComponent),
    })
)
async def sd_image_unload_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var



