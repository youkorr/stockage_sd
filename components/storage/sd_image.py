import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from . import storage_ns, SdImageComponent, StorageComponent, STANDALONE_SD_IMAGE_SCHEMA, standalone_sd_image_to_code

CONFIG_SCHEMA = STANDALONE_SD_IMAGE_SCHEMA

async def to_code(config):
    await standalone_sd_image_to_code(config)
