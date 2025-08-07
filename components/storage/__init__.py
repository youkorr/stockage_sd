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

storage_ns = cg.esphome_ns.namespace('storage')
StorageComponent = storage_ns.class_('StorageComponent', cg.Component)
# IMPORTANT: Utiliser le bon héritage pour image::Image
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

SD_IMAGE_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(SdImageComponent),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Required(CONF_WIDTH): cv.positive_int,
    cv.Required(CONF_HEIGHT): cv.positive_int,
    cv.Required(CONF_FORMAT): cv.enum(IMAGE_FORMAT, lower=True),  # lower=True pour accepter minuscules
    cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDER, lower=True),
    cv.Optional(CONF_CACHE_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA).extend(image.IMAGE_SCHEMA)

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_direct", lower=True),
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_SD_COMPONENT): cv.use_id(cg.Component),
    cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,
    cv.Optional(CONF_SD_IMAGES, default=[]): cv.ensure_list(SD_IMAGE_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)

def validate_image_config(img_config):
    if not img_config[CONF_FILE_PATH].startswith("/"):
        raise cv.Invalid("Image file path must be absolute (start with '/')")
    if img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] > 1024 * 768:
        raise cv.Invalid("Image dimensions too large (max 1024x768)")
    return img_config

def validate_storage_config(config):
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

async def process_sd_image_config(img_config, storage_component):
    # IMPORTANT: Création ET enregistrement du composant image
    img_var = cg.new_Pvariable(img_config[CONF_ID])
    await cg.register_component(img_var, img_config)

    # Configuration de base
    cg.add(img_var.set_storage_component(storage_component))
    cg.add(img_var.set_file_path(img_config[CONF_FILE_PATH]))
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

    # Calcul de la taille des données (utilisation des clés minuscules)
    format_sizes = {
        "rgb565": 2,
        "rgb888": 3,
        "rgba": 4,
        "grayscale": 1,
        "binary": 1,
    }

    format_key = img_config[CONF_FORMAT]  # Déjà en minuscules
    if format_key == "binary":
        data_size = (img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] + 7) // 8
    else:
        data_size = img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] * format_sizes[format_key]

    cg.add(img_var.set_expected_data_size(data_size))
    
    # CRUCIAL: Retourner la variable pour que ESPHome puisse la suivre
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




