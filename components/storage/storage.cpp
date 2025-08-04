# Ajoutez cette option à CONFIG_SCHEMA
CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_card", "flash", "inline", "sd_direct", lower=True),
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_FILES): cv.ensure_list(FILE_SCHEMA),
    cv.Optional(CONF_WEB_SERVER): cv.use_id(web_server_base.WebServerBase),
    cv.Optional(CONF_AUTO_HTTP_INTERCEPT, default=True): cv.boolean,  # Nouvelle option
    
    # Options pour bypass SD direct
    cv.Optional(CONF_SD_COMPONENT): cv.use_id(cg.Component),
    cv.Optional(CONF_ENABLE_GLOBAL_BYPASS, default=False): cv.boolean,
    cv.Optional(CONF_CACHE_SIZE, default=0): cv.positive_int,
}).extend(cv.COMPONENT_SCHEMA)

# Modifiez la fonction to_code pour configurer le serveur web
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    
    # Configuration pour bypass SD direct
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
    
    # Configuration des fichiers
    for file in config[CONF_FILES]:
        file_var = cg.new_Pvariable(file[CONF_ID])
        cg.add(file_var.set_path(file[CONF_PATH]))
        cg.add(file_var.set_platform(config[CONF_PLATFORM]))
        cg.add(file_var.set_chunk_size(file[CONF_CHUNK_SIZE]))
        await cg.register_component(file_var, file)
        cg.add(var.add_file(file_var))
    
    # Configuration du serveur web
    if CONF_WEB_SERVER in config:
        web_server = await cg.get_variable(config[CONF_WEB_SERVER])
        cg.add(var.set_web_server(web_server))
        
        # Si auto_http_intercept est activé, configurer les gestionnaires HTTP
        if config[CONF_AUTO_HTTP_INTERCEPT]:
            cg.add(var.setup_http_handlers(web_server))









