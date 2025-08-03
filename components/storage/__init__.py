import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sd_mmc_card
from esphome.const import CONF_ID, CONF_FILES, CONF_PATH
from esphome import automation

DEPENDENCIES = ["sd_mmc_card"]
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_PLATFORM = "platform"
CONF_SD_COMPONENT = "sd_component"
CONF_ENABLE_GLOBAL_BYPASS = "enable_global_bypass"
CONF_CACHE_SIZE = "cache_size"
CONF_AUTO_HTTP_INTERCEPT = "auto_http_intercept"
CONF_CHUNK_SIZE = "chunk_size"

# Namespace et classes
storage_ns = cg.esphome_ns.namespace("storage")
StorageComponent = storage_ns.class_("StorageComponent", cg.Component)

# Actions
StorageStreamFileAction = storage_ns.class_("StorageStreamFileAction", automation.Action)
StorageReadFileAction = storage_ns.class_("StorageReadFileAction", automation.Action)
StorageStreamAudioAction = storage_ns.class_("StorageStreamAudioAction", automation.Action)
StorageStreamImageAction = storage_ns.class_("StorageStreamImageAction", automation.Action)
StorageFileExistsAction = storage_ns.class_("StorageFileExistsAction", automation.Action)

# Configuration schema
CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(StorageComponent),
        cv.Required(CONF_PLATFORM): cv.one_of("sd_direct", lower=True),
        cv.Optional(CONF_SD_COMPONENT): cv.use_id(sd_mmc_card.SDMMCCard),
        cv.Optional(CONF_ENABLE_GLOBAL_BYPASS, default=False): cv.boolean,
        cv.Optional(CONF_CACHE_SIZE, default=32768): cv.positive_int,
        cv.Optional(CONF_AUTO_HTTP_INTERCEPT, default=False): cv.boolean,
        cv.Optional(CONF_FILES, default=[]): cv.All(
            cv.ensure_list,
            [cv.Schema({
                cv.Required(CONF_ID): cv.declare_id(cg.std_string),
                cv.Required(CONF_PATH): cv.string,
                cv.Optional(CONF_CHUNK_SIZE, default=1024): cv.positive_int,
            })]
        )
    }),
    cv.has_at_least_one_key(CONF_SD_COMPONENT)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration de la plateforme
    platform = config[CONF_PLATFORM]
    cg.add(var.set_platform(platform))
    
    # Configuration du composant SD
    if CONF_SD_COMPONENT in config:
        sd_card = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_card))
    
    # Configuration du bypass global
    if config.get(CONF_ENABLE_GLOBAL_BYPASS, False):
        cg.add(var.set_enable_global_bypass(True))
    
    # Configuration de la taille du cache
    cg.add(var.set_cache_size(config[CONF_CACHE_SIZE]))
    
    # Configuration de l'interception HTTP
    if config.get(CONF_AUTO_HTTP_INTERCEPT, False):
        cg.add(var.set_auto_http_intercept(True))
        # S'assurer que le composant web_server est disponible
        cg.add_define("USE_WEB_SERVER")
        cg.add_library("ESP Async WebServer", "1.2.3")
    
    # Configuration des fichiers
    for file_config in config.get(CONF_FILES, []):
        file_id = file_config[CONF_ID]
        file_path = file_config[CONF_PATH]
        chunk_size = file_config.get(CONF_CHUNK_SIZE, 1024)
        
        cg.add(var.add_file(file_path, chunk_size))

# Actions pour l'automatisation
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
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    template_ = await cg.templatable(config["chunk_size"], args, cg.size_t)
    cg.add(var.set_chunk_size(template_))
    
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
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    template_ = await cg.templatable(config["max_size"], args, cg.size_t)
    cg.add(var.set_max_size(template_))
    
    return var

@automation.register_action(
    "storage.stream_audio",
    StorageStreamAudioAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("chunk_size", default=4096): cv.templatable(cv.positive_int),
    })
)
async def storage_stream_audio_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    template_ = await cg.templatable(config["chunk_size"], args, cg.size_t)
    cg.add(var.set_chunk_size(template_))
    
    return var

@automation.register_action(
    "storage.stream_image",
    StorageStreamImageAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(StorageComponent),
        cv.Required("file_path"): cv.templatable(cv.string),
        cv.Optional("chunk_size", default=2048): cv.templatable(cv.positive_int),
    })
)
async def storage_stream_image_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    template_ = await cg.templatable(config["chunk_size"], args, cg.size_t)
    cg.add(var.set_chunk_size(template_))
    
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
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    template_ = await cg.templatable(config["file_path"], args, cg.std_string)
    cg.add(var.set_file_path(template_))
    
    return var





