import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PLATFORM, CONF_WEB_SERVER
from esphome.components import web_server_base, audio
from esphome import automation

DEPENDENCIES = ['media_player', 'web_server_base', 'audio', 'sd_mmc_card']
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_STORAGE = "storage"
CONF_FILES = "files"
CONF_PATH = "path"
CONF_MEDIA_FILE = "media_file"
CONF_CHUNK_SIZE = "chunk_size"

# Constants pour bypass SD direct
CONF_SD_COMPONENT = "sd_component"
CONF_ENABLE_GLOBAL_BYPASS = "enable_global_bypass"
CONF_CACHE_SIZE = "cache_size"
CONF_AUTO_MOUNT = "auto_mount"

# NOUVEAU: Constants pour interception HTTP automatique
CONF_ENABLE_HTTP_INTERCEPTION = "enable_http_interception"
CONF_AUTO_SETUP_WEB_SERVER = "auto_setup_web_server"
CONF_INTERCEPT_LOCALHOST = "intercept_localhost"

storage_ns = cg.esphome_ns.namespace('storage')
StorageComponent = storage_ns.class_('StorageComponent', cg.Component)
StorageFile = storage_ns.class_('StorageFile', audio.AudioFile, cg.Component)

# Classes pour les actions
StorageStreamFileAction = storage_ns.class_('StorageStreamFileAction', automation.Action)
StorageReadFileAction = storage_ns.class_('StorageReadFileAction', automation.Action)
StorageStreamAudioAction = storage_ns.class_('StorageStreamAudioAction', automation.Action)
StorageStreamImageAction = storage_ns.class_('StorageStreamImageAction', automation.Action)
StorageFileExistsAction = storage_ns.class_('StorageFileExistsAction', automation.Action)

# NOUVEAU: Classe pour l'intercepteur HTTP
StorageHTTPInterceptor = storage_ns.class_('StorageHTTPInterceptor')
StorageActionFactory = storage_ns.class_('StorageActionFactory')

FILE_SCHEMA = cv.Schema({
    cv.Required(CONF_PATH): cv.All(cv.string, lambda path: path if path.startswith("/") else cv.Invalid("Path must be absolute")),
    cv.Required(CONF_ID): cv.declare_id(StorageFile),
    cv.Optional(CONF_CHUNK_SIZE, default=512): cv.positive_int,
})

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_card", "flash", "inline", "sd_direct", lower=True),
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_FILES): cv.ensure_list(FILE_SCHEMA),
    cv.Optional(CONF_WEB_SERVER): cv.use_id(web_server_base.WebServerBase),
    
    # Options existantes
    cv.Optional(CONF_SD_COMPONENT): cv.use_id(cg.Component),
    cv.Optional(CONF_ENABLE_GLOBAL_BYPASS, default=False): cv.boolean,
    cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,
    cv.Optional(CONF_AUTO_MOUNT, default=True): cv.boolean,
    
    # NOUVEAU: Options pour interception HTTP automatique
    cv.Optional(CONF_ENABLE_HTTP_INTERCEPTION, default=True): cv.boolean,
    cv.Optional(CONF_AUTO_SETUP_WEB_SERVER, default=True): cv.boolean,
    cv.Optional(CONF_INTERCEPT_LOCALHOST, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    
    # Configuration SD direct
    if config[CONF_PLATFORM] in ["sd_direct", "sd_card"]:
        if CONF_SD_COMPONENT in config:
            sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
            cg.add(var.set_sd_component(sd_component))
        else:
            cg.add_define("STORAGE_AUTO_DETECT_SD")
            
        if config[CONF_ENABLE_GLOBAL_BYPASS]:
            cg.add(var.enable_global_bypass(True))
            cg.add_define("STORAGE_GLOBAL_BYPASS_ENABLED")
            
        if config[CONF_CACHE_SIZE] > 0:
            cg.add(var.set_cache_size(config[CONF_CACHE_SIZE]))
        
        # NOUVEAU: Configuration de l'interception HTTP automatique
        if config.get(CONF_ENABLE_HTTP_INTERCEPTION, True):
            cg.add_define("STORAGE_HTTP_INTERCEPTION_ENABLED")
            
            # Forcer l'activation du serveur web si nécessaire
            if config.get(CONF_AUTO_SETUP_WEB_SERVER, True):
                cg.add_define("STORAGE_AUTO_WEB_SERVER")
                
            if config.get(CONF_INTERCEPT_LOCALHOST, True):
                cg.add_define("STORAGE_INTERCEPT_LOCALHOST")
            
            # Ajouter les dépendances nécessaires
            cg.add_library("ESPAsyncWebServer", "1.2.7")
            
            # Code d'initialisation automatique
            cg.add(cg.RawStatement(f"""
// Auto-setup HTTP interception in setup()
App.register_component(new CallbackComponent([this]() {{
    // Attendre que tous les composants soient prêts
    App.scheduler.set_timeout(this, "setup_http_interception", 2000, [this]() {{
        storage::StorageActionFactory::setup_http_interception({var});
        ESP_LOGI("storage", "HTTP interception setup complete - ESPHome images will be served from SD");
    }});
}}));
"""))
    
    # Configuration des fichiers
    for file in config[CONF_FILES]:
        file_var = cg.new_Pvariable(file[CONF_ID])
        cg.add(file_var.set_path(file[CONF_PATH]))
        cg.add(file_var.set_platform(config[CONF_PLATFORM]))
        cg.add(file_var.set_chunk_size(file[CONF_CHUNK_SIZE]))
        await cg.register_component(file_var, file)
        cg.add(var.add_file(file_var))
    
    # Configuration du serveur web si spécifié
    if CONF_WEB_SERVER in config:
        web_server = await cg.get_variable(config[CONF_WEB_SERVER])
        cg.add(var.set_web_server(web_server))
    elif config.get(CONF_AUTO_SETUP_WEB_SERVER, True) and config[CONF_PLATFORM] in ["sd_direct", "sd_card"]:
        # Auto-créer un serveur web si nécessaire pour l'interception
        cg.add_define("STORAGE_NEEDS_WEB_SERVER")
    
    # Setup global hooks
    if config.get(CONF_ENABLE_GLOBAL_BYPASS, False):
        cg.add_define("STORAGE_HOOKS_ENABLED")

# Actions automation existantes
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

@automation.register_action(
    "storage.stream_audio",
    StorageStreamAudioAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("chunk_size", default=1024): cv.templatable(cv.positive_int),
    })
)
async def storage_stream_audio_to_code(config, action_id, template_arg, args):
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
    "storage.stream_image",
    StorageStreamImageAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("chunk_size", default=1024): cv.templatable(cv.positive_int),
    })
)
async def storage_stream_image_to_code(config, action_id, template_arg, args):
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
    "storage.file_exists",
    StorageFileExistsAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
    })
)
async def storage_file_exists_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var

# Validation de configuration
def validate_sd_direct_config(config):
    """Valide la configuration pour sd_direct avec interception HTTP"""
    if config[CONF_PLATFORM] == "sd_direct":
        if config.get(CONF_ENABLE_HTTP_INTERCEPTION, True):
            # S'assurer qu'on a une dépendance web server
            if CONF_WEB_SERVER not in config and not config.get(CONF_AUTO_SETUP_WEB_SERVER, True):
                raise cv.Invalid("HTTP interception requires web_server_base or auto_setup_web_server")
    return config

CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_sd_direct_config)






