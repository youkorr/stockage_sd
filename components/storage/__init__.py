import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_WIDTH, CONF_HEIGHT, CONF_FORMAT
from esphome.components import image
from esphome import automation

# Dépendances requises
DEPENDENCIES = ['storage']
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_STORAGE_ID = "storage_id"
CONF_SD_COMPONENT = "sd_component"  # Option pour référence directe au composant SD
CONF_FILE_PATH = "file_path"
CONF_BYTE_ORDER = "byte_order"
CONF_CACHE_ENABLED = "cache_enabled"
CONF_PRELOAD = "preload"

# Namespace pour le composant
sd_image_ns = cg.esphome_ns.namespace('sd_image')
SdImageComponent = sd_image_ns.class_('SdImageComponent', image.Image, cg.Component)

# Actions pour automatisation
SdImageLoadAction = sd_image_ns.class_('SdImageLoadAction', automation.Action)
SdImageUnloadAction = sd_image_ns.class_('SdImageUnloadAction', automation.Action)

# Formats d'image supportés
IMAGE_FORMAT = {
    "rgb565": "RGB565",
    "rgb888": "RGB888", 
    "rgba": "RGBA",
    "grayscale": "GRAYSCALE",
    "binary": "BINARY",
}

BYTE_ORDER = {
    "little_endian": "LITTLE_ENDIAN",
    "big_endian": "BIG_ENDIAN",
}

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(SdImageComponent),
    cv.Exclusive(CONF_STORAGE_ID, "source"): cv.use_id(cg.Component),  # Via composant storage
    cv.Exclusive(CONF_SD_COMPONENT, "source"): cv.use_id(cg.Component),  # Via composant SD direct
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Required(CONF_WIDTH): cv.positive_int,
    cv.Required(CONF_HEIGHT): cv.positive_int,
    cv.Required(CONF_FORMAT): cv.enum(IMAGE_FORMAT, upper=True),
    cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDER, upper=True),
    cv.Optional(CONF_CACHE_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration de base
    cg.add(var.set_file_path(config[CONF_FILE_PATH]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))
    cg.add(var.set_format(getattr(sd_image_ns, f"ImageFormat::{IMAGE_FORMAT[config[CONF_FORMAT]]}")))
    cg.add(var.set_byte_order(getattr(sd_image_ns, f"ByteOrder::{BYTE_ORDER[config[CONF_BYTE_ORDER]]}")))
    
    # Options avancées
    cg.add(var.set_cache_enabled(config[CONF_CACHE_ENABLED]))
    cg.add(var.set_preload(config[CONF_PRELOAD]))
    
    # Référence au composant de stockage (storage ou SD direct)
    if CONF_STORAGE_ID in config:
        storage = await cg.get_variable(config[CONF_STORAGE_ID])
        cg.add(var.set_storage_component(storage))
    elif CONF_SD_COMPONENT in config:
        sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_component))
    else:
        raise cv.Invalid("Either storage_id or sd_component must be specified")
    
    # Calcul automatique de la taille des données
    format_sizes = {
        "rgb565": 2,
        "rgb888": 3,
        "rgba": 4,
        "grayscale": 1,
        "binary": 1,  # Sera calculé en bits
    }
    
    if config[CONF_FORMAT] == "binary":
        # Pour binary, calculer en bits puis convertir en bytes
        data_size = (config[CONF_WIDTH] * config[CONF_HEIGHT] + 7) // 8
    else:
        data_size = config[CONF_WIDTH] * config[CONF_HEIGHT] * format_sizes[config[CONF_FORMAT]]
    
    cg.add(var.set_expected_data_size(data_size))
    
    # Ajouter les defines nécessaires
    cg.add_define("USE_SD_IMAGE")

# Actions pour automatisation
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

# Validation de la configuration
def validate_image_config(config):
    """Valide la configuration de l'image"""
    # Vérifier qu'au moins une source est spécifiée
    if CONF_STORAGE_ID not in config and CONF_SD_COMPONENT not in config:
        raise cv.Invalid("Either storage_id or sd_component must be specified")
    
    # Vérifier que le chemin est absolu
    if not config[CONF_FILE_PATH].startswith("/"):
        raise cv.Invalid("File path must be absolute (start with '/')")
    
    # Vérifier l'extension du fichier
    valid_extensions = ['.rgb565', '.rgb888', '.rgba', '.gray', '.bin']
    file_path = config[CONF_FILE_PATH].lower()
    if not any(file_path.endswith(ext) for ext in valid_extensions):
        raise cv.Invalid(f"File must have one of these extensions: {', '.join(valid_extensions)}")
    
    # Vérifier que les dimensions sont raisonnables
    if config[CONF_WIDTH] * config[CONF_HEIGHT] > 1024 * 768:  # Max 1024x768
        raise cv.Invalid("Image dimensions too large (max 1024x768)")
    
    return config

# Appliquer la validation
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_image_config)





