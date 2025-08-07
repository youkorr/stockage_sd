import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PLATFORM, CONF_WIDTH, CONF_HEIGHT, CONF_FORMAT
from esphome import automation

DEPENDENCIES = ['sd_mmc_card']
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

# Classe pour les images SD dans le namespace storage
SdImageComponent = storage_ns.class_('SdImageComponent', cg.Component)

# Classes pour les actions images
SdImageLoadAction = storage_ns.class_('SdImageLoadAction', automation.Action)
SdImageUnloadAction = storage_ns.class_('SdImageUnloadAction', automation.Action)

# Formats d'image supportés
IMAGE_FORMAT = {
    "rgb565": "rgb565",
    "rgb888": "rgb888", 
    "rgba": "rgba",
    "grayscale": "graycale",
    "binary": "binary",
}

BYTE_ORDER = {
    "little_endian": "little_endian",
    "big_endian": "big_endian",
}

# Schéma pour les images SD
SD_IMAGE_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(SdImageComponent),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Required(CONF_WIDTH): cv.positive_int,
    cv.Required(CONF_HEIGHT): cv.positive_int,
    cv.Required(CONF_FORMAT): cv.enum(IMAGE_FORMAT),
    cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDER),
    cv.Optional(CONF_CACHE_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
})


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.Required(CONF_PLATFORM): cv.one_of("sd_direct", lower=True),
        cv.Required(CONF_ID): cv.declare_id(StorageComponent),
        
        # Options pour SD direct
        cv.Required(CONF_SD_COMPONENT): cv.use_id(cg.Component),  # Référence vers sd_mmc_card
        cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,  # Cache size en bytes
        
        # Support des images SD
        cv.Optional(CONF_SD_IMAGES): cv.ensure_list(SD_IMAGE_SCHEMA),
    }).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_SD_COMPONENT)
)

# Schéma global pour supporter à la fois storage et sd_image au niveau racine
def get_combined_schema():
    return cv.Any(
        CONFIG_SCHEMA,  # Configuration storage
        SD_IMAGE_SCHEMA  # Configuration sd_image
    )

async def to_code(config):
    # Si c'est une configuration storage
    if CONF_PLATFORM in config:
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        cg.add(var.set_platform(config[CONF_PLATFORM]))
        
        # Configuration SD direct
        sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_component))
        
        if config[CONF_CACHE_SIZE] > 0:
            cg.add(var.set_cache_size(config[CONF_CACHE_SIZE]))
        
        # Configuration des images SD si présentes
        if CONF_SD_IMAGES in config:
            for img_config in config[CONF_SD_IMAGES]:
                await process_sd_image_config(img_config)
                
        # Ajouter les defines nécessaires pour les images
        if CONF_SD_IMAGES in config:
            cg.add_define("USE_SD_IMAGE")


async def process_sd_image_config(img_config):
    """Traite la configuration d'une image SD"""
    img_var = cg.new_Pvariable(img_config[CONF_ID])
    await cg.register_component(img_var, img_config)
    
    # Référence vers le composant storage
    
    cg.add(img_var.set_storage_component(storage_component))
    
    # Configuration de base de l'image
    cg.add(img_var.set_file_path(img_config[CONF_FILE_PATH]))
    cg.add(img_var.set_width(img_config[CONF_WIDTH]))
    cg.add(img_var.set_height(img_config[CONF_HEIGHT]))
    
    # Configuration du format et byte order
    try:
        format_enum = getattr(storage_ns, "ImageFormat")
        format_value = getattr(format_enum, IMAGE_FORMAT[img_config[CONF_FORMAT]])
        cg.add(img_var.set_format(format_value))
        
        byte_order_enum = getattr(storage_ns, "ByteOrder")
        byte_order_value = getattr(byte_order_enum, BYTE_ORDER[img_config[CONF_BYTE_ORDER]])
        cg.add(img_var.set_byte_order(byte_order_value))
    except AttributeError:
        # Fallback si les enums ne sont pas encore définis
        cg.add(img_var.set_format_string(IMAGE_FORMAT[img_config[CONF_FORMAT]]))
        cg.add(img_var.set_byte_order_string(BYTE_ORDER[img_config[CONF_BYTE_ORDER]]))
    
    # Options avancées
    cg.add(img_var.set_cache_enabled(img_config[CONF_CACHE_ENABLED]))
    cg.add(img_var.set_preload(img_config[CONF_PRELOAD]))
    
    # Calcul automatique de la taille des données
    format_sizes = {
        "rgb565": 2,
        "rgb888": 3,
        "rgba": 4,
        "grayscale": 1,
        "binary": 1,
    }
    
    if img_config[CONF_FORMAT] == "binary":
        data_size = (img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] + 7) // 8
    else:
        data_size = img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] * format_sizes[img_config[CONF_FORMAT]]
    
    cg.add(img_var.set_expected_data_size(data_size))
    
    # Ajouter les defines nécessaires
    cg.add_define("USE_SD_IMAGE")

# Actions pour les images
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

# Validation des configurations
def validate_image_config(img_config):
    """Valide la configuration d'une image"""
    # Vérifier que le chemin est absolu
    if not img_config[CONF_FILE_PATH].startswith("/"):
        raise cv.Invalid("Image file path must be absolute (start with '/')")
    
    # Vérifier que les dimensions sont raisonnables
    if img_config[CONF_WIDTH] * img_config[CONF_HEIGHT] > 1024 * 768:
        raise cv.Invalid("Image dimensions too large (max 1024x768)")
    
    return img_config

def validate_storage_config(config):
    """Valide la configuration storage"""
    if CONF_SD_IMAGES in config:
        for img_config in config[CONF_SD_IMAGES]:
            validate_image_config(img_config)
    
    return config

# Appliquer les validations
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_storage_config)
SD_IMAGE_SCHEMA = cv.All(SD_IMAGE_SCHEMA, validate_image_config)

# Export du schéma combiné
FINAL_VALIDATE_SCHEMA = get_combined_schema()





