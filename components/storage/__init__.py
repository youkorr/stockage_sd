import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PLATFORM, CONF_WEB_SERVER
from esphome.components import web_server_base, audio
from esphome import automation

DEPENDENCIES = ['media_player', 'web_server_base', 'audio', 'sd_mmc_card'] #Add sd_mmc_card dependency
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_STORAGE = "storage"
CONF_FILES = "files"
CONF_PATH = "path"
CONF_MEDIA_FILE = "media_file"
CONF_CHUNK_SIZE = "chunk_size"

# NOUVEAU: Constants pour bypass SD direct
CONF_SD_COMPONENT = "sd_component"
CONF_ENABLE_GLOBAL_BYPASS = "enable_global_bypass"
CONF_CACHE_SIZE = "cache_size"
CONF_AUTO_MOUNT = "auto_mount"

storage_ns = cg.esphome_ns.namespace('storage')
StorageComponent = storage_ns.class_('StorageComponent', cg.Component)
StorageFile = storage_ns.class_('StorageFile', audio.AudioFile, cg.Component)

# NOUVEAU: Classes pour les actions bypass
StorageStreamFileAction = storage_ns.class_('StorageStreamFileAction', automation.Action)
StorageReadFileAction = storage_ns.class_('StorageReadFileAction', automation.Action)

FILE_SCHEMA = cv.Schema({
    cv.Required(CONF_PATH): cv.string,
    cv.Required(CONF_ID): cv.declare_id(StorageFile),
    cv.Optional(CONF_CHUNK_SIZE, default=512): cv.positive_int, # Default chunk size
})

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_card", "flash", "inline", "sd_direct", lower=True),  # Ajout sd_direct
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_FILES): cv.ensure_list(FILE_SCHEMA),
    cv.Optional(CONF_WEB_SERVER): cv.use_id(web_server_base.WebServerBase),
    
    # NOUVEAU: Options pour bypass SD direct
    cv.Optional(CONF_SD_COMPONENT): cv.use_id(cg.Component),  # Référence vers sd_mmc_card
    cv.Optional(CONF_ENABLE_GLOBAL_BYPASS, default=False): cv.boolean,
    cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,  # Cache size en bytes
    cv.Optional(CONF_AUTO_MOUNT, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    
    # NOUVEAU: Configuration pour bypass SD direct
    if config[CONF_PLATFORM] in ["sd_direct", "sd_card"]:
        if CONF_SD_COMPONENT in config:
            sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
            cg.add(var.set_sd_component(sd_component))
        else:
            # Auto-detect du composant SD MMC si disponible
            cg.add_define("STORAGE_AUTO_DETECT_SD")
            
        if config[CONF_ENABLE_GLOBAL_BYPASS]:
            cg.add(var.enable_global_bypass(True))
            cg.add_define("STORAGE_GLOBAL_BYPASS_ENABLED")
            
        if config[CONF_CACHE_SIZE] > 0:
            cg.add(var.set_cache_size(config[CONF_CACHE_SIZE]))
    
    # Configuration des fichiers
    for file in config[CONF_FILES]:
        file_var = cg.new_Pvariable(file[CONF_ID])
        cg.add(file_var.set_path(file[CONF_PATH]))
        cg.add(file_var.set_platform(config[CONF_PLATFORM]))
        cg.add(file_var.set_chunk_size(file[CONF_CHUNK_SIZE]))
        await cg.register_component(file_var, file)
        cg.add(var.add_file(file_var))
    
    if CONF_WEB_SERVER in config:
        web_server = await cg.get_variable(config[CONF_WEB_SERVER])
        cg.add(var.set_web_server(web_server))
    
    # NOUVEAU: Setup global hooks pour bypass
    if config.get(CONF_ENABLE_GLOBAL_BYPASS, False):
        cg.add_library("ESPAsyncWebServer", "1.2.3")  # Pour les hooks web si nécessaire
        cg.add_define("STORAGE_HOOKS_ENABLED")

# NOUVEAU: Actions pour automatisation
@automation.register_action(
    "storage.stream_file",
    StorageStreamFileAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("chunk_size", default=1024): cv.templatable(cv.positive_int),
    })
)
async def storage_stream_file_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    if "chunk_size" in config:
        template_ = await cg.templatable(config["chunk_size"], args, cg.size_t)
        cg.add(var.set_chunk_size(template_))
    
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var

@automation.register_action(
    "storage.read_file",
    StorageReadFileAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("max_size", default=0): cv.templatable(cv.positive_int),
    })
)
async def storage_read_file_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    if "max_size" in config:
        template_ = await cg.templatable(config["max_size"], args, cg.size_t)
        cg.add(var.set_max_size(template_))
    
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var

# NOUVEAU: Validation des configurations
def validate_sd_direct_config(config):
    """Valide la configuration pour sd_direct platform"""
    if config[CONF_PLATFORM] == "sd_direct":
        if CONF_SD_COMPONENT not in config:
            # Essayer de détecter automatiquement le composant SD
            pass  # L'auto-détection sera gérée dans le code C++
    return config

# Appliquer la validation
CONFIG_SCHEMA = CONFIG_SCHEMA.extend({}, extra=cv.ALLOW_EXTRA)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_sd_direct_config)

# NOUVEAU: Fonctions helper pour validation des chemins
def validate_file_path(path):
    """Valide que le chemin de fichier est correct pour la plateforme"""
    if not path.startswith("/"):
        raise cv.Invalid("File path must be absolute (start with '/')")
    return path

# Mettre à jour le FILE_SCHEMA avec validation
FILE_SCHEMA = cv.Schema({
    cv.Required(CONF_PATH): cv.All(cv.string, validate_file_path),
    cv.Required(CONF_ID): cv.declare_id(StorageFile),
    cv.Optional(CONF_CHUNK_SIZE, default=512): cv.positive_int,
})






