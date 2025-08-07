from __future__ import annotations

import hashlib
import io
import logging
from pathlib import Path
import re

from PIL import Image, UnidentifiedImageError

from esphome import core, external_files
import esphome.codegen as cg
from esphome.components.const import CONF_BYTE_ORDER
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEFAULTS,
    CONF_DITHER,
    CONF_FILE,
    CONF_ICON,
    CONF_ID,
    CONF_PATH,
    CONF_RAW_DATA_ID,
    CONF_RESIZE,
    CONF_SOURCE,
    CONF_TYPE,
    CONF_URL,
)
from esphome.core import CORE, HexInt

_LOGGER = logging.getLogger(__name__)

DOMAIN = "image"
DEPENDENCIES = ["display"]

image_ns = cg.esphome_ns.namespace("image")

ImageType = image_ns.enum("ImageType")
TransparencyType = image_ns.enum("TransparencyType")  # Correction du nom

CONF_OPAQUE = "opaque"
CONF_CHROMA_KEY = "chroma_key"
CONF_ALPHA_CHANNEL = "alpha_channel"
CONF_INVERT_ALPHA = "invert_alpha"
CONF_IMAGES = "images"

TRANSPARENCY_TYPES = (
    CONF_OPAQUE,
    CONF_CHROMA_KEY,
    CONF_ALPHA_CHANNEL,
)


def get_image_type_enum(type):
    return getattr(ImageType, f"IMAGE_TYPE_{type.upper()}")


def get_transparency_enum(transparency):
    return getattr(TransparencyType, f"TRANSPARENCY_{transparency.upper()}")


class ImageEncoder:
    """
    Superclass of image type encoders
    """

    # Control which transparency options are available for a given type
    allow_config = {CONF_ALPHA_CHANNEL, CONF_CHROMA_KEY, CONF_OPAQUE}

    # All imageencoder types are valid
    @staticmethod
    def validate(value):
        return value

    def __init__(self, width, height, transparency, dither, invert_alpha):
        """
        :param width:  The image width in pixels
        :param height:  The image height in pixels
        :param transparency: Transparency type
        :param dither: Dither method
        :param invert_alpha: True if the alpha channel should be inverted; for monochrome formats inverts the colours.
        """
        self.transparency = transparency
        self.width = width
        self.height = height
        self.data = [0 for _ in range(width * height)]
        self.dither = dither
        self.index = 0
        self.invert_alpha = invert_alpha
        self.path = ""

    def convert(self, image, path):
        """
        Convert the image format
        :param image:  Input image
        :param path:  Path to the image file
        :return: converted image
        """
        return image

    def encode(self, pixel):
        """
        Encode a single pixel
        """

    def end_row(self):
        """
        Marks the end of a pixel row
        :return:
        """


def is_alpha_only(image: Image):
    """
    Check if an image (assumed to be RGBA) is only alpha
    """
    # Any alpha data?
    if image.split()[-1].getextrema()[0] == 0xFF:
        return False
    return all(b.getextrema()[1] == 0 for b in image.split()[:-1])


class ImageBinary(ImageEncoder):
    allow_config = {CONF_OPAQUE, CONF_INVERT_ALPHA, CONF_CHROMA_KEY}

    def __init__(self, width, height, transparency, dither, invert_alpha):
        self.width8 = (width + 7) // 8
        super().__init__(self.width8, height, transparency, dither, invert_alpha)
        self.bitno = 0

    def convert(self, image, path):
        if is_alpha_only(image):
            image = image.split()[-1]
        return image.convert("1", dither=self.dither)

    def encode(self, pixel):
        if self.invert_alpha:
            pixel = not pixel
        if pixel:
            self.data[self.index] |= 0x80 >> (self.bitno % 8)
        self.bitno += 1
        if self.bitno == 8:
            self.bitno = 0
            self.index += 1

    def end_row(self):
        """
        Pad rows to a byte boundary
        """
        if self.bitno != 0:
            self.bitno = 0
            self.index += 1


class ImageGrayscale(ImageEncoder):
    allow_config = {CONF_ALPHA_CHANNEL, CONF_CHROMA_KEY, CONF_INVERT_ALPHA, CONF_OPAQUE}

    def convert(self, image, path):
        if is_alpha_only(image):
            if self.transparency != CONF_ALPHA_CHANNEL:
                _LOGGER.warning(
                    "Grayscale image %s is alpha only, but transparency is set to %s",
                    path,
                    self.transparency,
                )
                self.transparency = CONF_ALPHA_CHANNEL
            image = image.split()[-1]
        return image.convert("LA")

    def encode(self, pixel):
        b, a = pixel
        if self.transparency == CONF_CHROMA_KEY:
            if b == 1:
                b = 0
            if a != 0xFF:
                b = 1
        if self.invert_alpha:
            b ^= 0xFF
        if self.transparency == CONF_ALPHA_CHANNEL:
            if a != 0xFF:
                b = a
        self.data[self.index] = b
        self.index += 1


class ImageRGB565(ImageEncoder):
    def __init__(self, width, height, transparency, dither, invert_alpha):
        stride = 3 if transparency == CONF_ALPHA_CHANNEL else 2
        super().__init__(
            width * stride,
            height,
            transparency,
            dither,
            invert_alpha,
        )
        self.big_endian = True

    def set_big_endian(self, big_endian: bool) -> None:
        self.big_endian = big_endian

    def convert(self, image, path):
        return image.convert("RGBA")

    def encode(self, pixel):
        r, g, b, a = pixel
        r = r >> 3
        g = g >> 2
        b = b >> 3
        if self.transparency == CONF_CHROMA_KEY:
            if r == 0 and g == 1 and b == 0:
                g = 0
            elif a < 128:
                r = 0
                g = 1
                b = 0
        rgb = (r << 11) | (g << 5) | b
        if self.big_endian:
            self.data[self.index] = rgb >> 8
            self.index += 1
            self.data[self.index] = rgb & 0xFF
            self.index += 1
        else:
            self.data[self.index] = rgb & 0xFF
            self.index += 1
            self.data[self.index] = rgb >> 8
            self.index += 1
        if self.transparency == CONF_ALPHA_CHANNEL:
            if self.invert_alpha:
                a ^= 0xFF
            self.data[self.index] = a
            self.index += 1


class ImageRGB(ImageEncoder):
    def __init__(self, width, height, transparency, dither, invert_alpha):
        stride = 4 if transparency == CONF_ALPHA_CHANNEL else 3
        super().__init__(
            width * stride,
            height,
            transparency,
            dither,
            invert_alpha,
        )

    def convert(self, image, path):
        return image.convert("RGBA")

    def encode(self, pixel):
        r, g, b, a = pixel
        if self.transparency == CONF_CHROMA_KEY:
            if r == 0 and g == 1 and b == 0:
                g = 0
            elif a < 128:
                r = 0
                g = 1
                b = 0
        self.data[self.index] = r
        self.index += 1
        self.data[self.index] = g
        self.index += 1
        self.data[self.index] = b
        self.index += 1
        if self.transparency == CONF_ALPHA_CHANNEL:
            if self.invert_alpha:
                a ^= 0xFF
            self.data[self.index] = a
            self.index += 1


class ReplaceWith:
    """
    Placeholder class to provide feedback on deprecated features
    """

    allow_config = {CONF_ALPHA_CHANNEL, CONF_CHROMA_KEY, CONF_OPAQUE}

    def __init__(self, replace_with):
        self.replace_with = replace_with

    def validate(self, value):
        raise cv.Invalid(
            f"Image type {value} is removed; replace with {self.replace_with}"
        )


IMAGE_TYPE = {
    "BINARY": ImageBinary,
    "GRAYSCALE": ImageGrayscale,
    "RGB565": ImageRGB565,
    "RGB": ImageRGB,
    "TRANSPARENT_BINARY": ReplaceWith("'type: BINARY' and 'transparency: chroma_key'"),
    "RGB24": ReplaceWith("'type: RGB'"),
    "RGBA": ReplaceWith("'type: RGB' and 'transparency: alpha_channel'"),
}

CONF_TRANSPARENCY = "transparency"

# If the MDI file cannot be downloaded within this time, abort.
IMAGE_DOWNLOAD_TIMEOUT = 30  # seconds

SOURCE_LOCAL = "local"
SOURCE_WEB = "web"
SOURCE_SD_CARD = "sd_card"
SOURCE_MDI = "mdi"
SOURCE_MDIL = "mdil"
SOURCE_MEMORY = "memory"

MDI_SOURCES = {
    SOURCE_MDI: "https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/",
    SOURCE_MDIL: "https://raw.githubusercontent.com/Pictogrammers/MaterialDesignLight/refs/heads/master/svg/",
    SOURCE_MEMORY: "https://raw.githubusercontent.com/Pictogrammers/Memory/refs/heads/main/src/svg/",
}

Image_ = image_ns.class_("Image")
SDCardImage_ = image_ns.class_("SDCardImage", Image_)

INSTANCE_TYPE = Image_


def compute_local_image_path(value) -> Path:
    url = value[CONF_URL] if isinstance(value, dict) else value
    h = hashlib.new("sha256")
    h.update(url.encode())
    key = h.hexdigest()[:8]
    base_dir = external_files.compute_local_file_dir(DOMAIN)
    return base_dir / key


def local_path(value):
    value = value[CONF_PATH] if isinstance(value, dict) else value
    return str(CORE.relative_config_path(value))


def download_file(url, path):
    external_files.download_content(url, path, IMAGE_DOWNLOAD_TIMEOUT)
    return str(path)


def download_gh_svg(value, source):
    mdi_id = value[CONF_ICON] if isinstance(value, dict) else value
    base_dir = external_files.compute_local_file_dir(DOMAIN) / source
    path = base_dir / f"{mdi_id}.svg"

    url = MDI_SOURCES[source] + mdi_id + ".svg"
    return download_file(url, path)


def download_image(value):
    value = value[CONF_URL] if isinstance(value, dict) else value
    return download_file(value, compute_local_image_path(value))


def is_svg_file(file):
    if not file:
        return False
    with open(file, "rb") as f:
        return "<svg" in str(f.read(1024))


def validate_cairosvg_installed():
    try:
        import cairosvg
    except ImportError as err:
        raise cv.Invalid(
            "Please install the cairosvg python package to use this feature. "
            "(pip install cairosvg)"
        ) from err

    major, minor, _ = cairosvg.__version__.split(".")
    if major < "2" or major == "2" and minor < "2":
        raise cv.Invalid(
            "Please update your cairosvg installation to at least 2.2.0. "
            "(pip install -U cairosvg)"
        )


def validate_file_shorthand(value):
    value = cv.string_strict(value)
    parts = value.strip().split(":")
    if len(parts) == 2 and parts[0] in MDI_SOURCES:
        match = re.match(r"^[a-zA-Z0-9\-]+$", parts[1])
        if match is None:
            raise cv.Invalid(f"Could not parse mdi icon name from '{value}'.")
        return download_gh_svg(parts[1], parts[0])

    if value.startswith("http://") or value.startswith("https://"):
        return download_image(value)

    value = cv.file_(value)
    return local_path(value)


LOCAL_SCHEMA = cv.All(
    {
        cv.Required(CONF_PATH): cv.file_,
    },
    local_path,
)


def mdi_schema(source):
    def validate_mdi(value):
        return download_gh_svg(value, source)

    return cv.All(
        cv.Schema(
            {
                cv.Required(CONF_ICON): cv.string,
            }
        ),
        validate_mdi,
    )


WEB_SCHEMA = cv.All(
    {
        cv.Required(CONF_URL): cv.string,
    },
    download_image,
)


SD_CARD_SCHEMA = cv.All(
    {
        cv.Required(CONF_PATH): cv.string,
    },
    lambda value: value[CONF_PATH],
)

TYPED_FILE_SCHEMA = cv.typed_schema(
    {
        SOURCE_LOCAL: LOCAL_SCHEMA,
        SOURCE_WEB: WEB_SCHEMA,
        SOURCE_SD_CARD: SD_CARD_SCHEMA,
    }
    | {source: mdi_schema(source) for source in MDI_SOURCES},
    key=CONF_SOURCE,
)

def validate_transparency(choices=TRANSPARENCY_TYPES):
    def validate(value):
        if isinstance(value, bool):
            value = str(value)
        return cv.one_of(*choices, lower=True)(value)

    return validate


def validate_type(image_types):
    def validate(value):
        value = cv.one_of(*image_types, upper=True)(value)
        return IMAGE_TYPE[value].validate(value)

    return validate


def validate_settings(value):
    """
    Validate the settings for a single image configuration.
    """
    # Skip validation for SD card images as they are processed at runtime
    if value.get(CONF_SOURCE) == SOURCE_SD_CARD:
        return value
        
    conf_type = value[CONF_TYPE]
    type_class = IMAGE_TYPE[conf_type]
    transparency = value[CONF_TRANSPARENCY].lower()
    if transparency not in type_class.allow_config:
        raise cv.Invalid(
            f"Image format '{conf_type}' cannot have transparency: {transparency}"
        )
    invert_alpha = value.get(CONF_INVERT_ALPHA, False)
    if (
        invert_alpha
        and transparency != CONF_ALPHA_CHANNEL
        and CONF_INVERT_ALPHA not in type_class.allow_config
    ):
        raise cv.Invalid("No alpha channel to invert")
    if value.get(CONF_BYTE_ORDER) is not None and not callable(
        getattr(type_class, "set_big_endian", None)
    ):
        raise cv.Invalid(
            f"Image format '{conf_type}' does not support byte order configuration"
        )
    if file := value.get(CONF_FILE):
        file = Path(file)
        if is_svg_file(file):
            validate_cairosvg_installed()
        else:
            try:
                Image.open(file)
            except UnidentifiedImageError as exc:
                raise cv.Invalid(
                    f"File can't be opened as image: {file.absolute()}"
                ) from exc
    return value


IMAGE_ID_SCHEMA = {
    cv.Required(CONF_ID): cv.declare_id(Image_),
    cv.Required(CONF_FILE): cv.Any(validate_file_shorthand, TYPED_FILE_SCHEMA),
    cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
}


OPTIONS_SCHEMA = {
    cv.Optional(CONF_RESIZE): cv.dimensions,
    cv.Optional(CONF_DITHER, default="NONE"): cv.one_of(
        "NONE", "FLOYDSTEINBERG", upper=True
    ),
    cv.Optional(CONF_INVERT_ALPHA, default=False): cv.boolean,
    cv.Optional(CONF_BYTE_ORDER): cv.one_of("BIG_ENDIAN", "LITTLE_ENDIAN", upper=True),
    cv.Optional(CONF_TRANSPARENCY, default=CONF_OPAQUE): validate_transparency(),
    cv.Optional(CONF_TYPE): validate_type(IMAGE_TYPE),
}

OPTIONS = [key.schema for key in OPTIONS_SCHEMA]

# image schema with no defaults, used with `CONF_IMAGES` in the config
IMAGE_SCHEMA_NO_DEFAULTS = {
    **IMAGE_ID_SCHEMA,
    **{cv.Optional(key): OPTIONS_SCHEMA[key] for key in OPTIONS},
}

BASE_SCHEMA = cv.Schema(
    {
        **IMAGE_ID_SCHEMA,
        **OPTIONS_SCHEMA,
    }
).add_extra(validate_settings)

IMAGE_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Required(CONF_TYPE): validate_type(IMAGE_TYPE),
    }
)


def validate_defaults(value):
    """
    Validate the options for images with defaults
    """
    defaults = value[CONF_DEFAULTS]
    result = []
    for index, image in enumerate(value[CONF_IMAGES]):
        type = image.get(CONF_TYPE, defaults.get(CONF_TYPE))
        if type is None:
            raise cv.Invalid(
                "Type is required either in the image config or in the defaults",
                path=[CONF_IMAGES, index],
            )
        type_class = IMAGE_TYPE[type]
        # A default byte order should be simply ignored if the type does not support it
        available_options = [*OPTIONS]
        if (
            not callable(getattr(type_class, "set_big_endian", None))
            and CONF_BYTE_ORDER not in image
        ):
            available_options.remove(CONF_BYTE_ORDER)
        
        # Créer un nouveau dictionnaire au lieu de modifier l'existant
        config = {}
        # Copier les options avec les valeurs par défaut
        for key in available_options:
            config[key] = image.get(key, defaults.get(key))
        # Copier les clés d'identification
        for key in IMAGE_ID_SCHEMA:
            if key.schema in image:
                config[key.schema] = image[key.schema]
        
        validate_settings(config)
        result.append(config)
    return result


def typed_image_schema(image_type):
    """
    Construct a schema for a specific image type, allowing transparency options
    """
    return cv.Any(
        cv.Schema(
            {
                cv.Optional(t.lower()): cv.ensure_list(
                    BASE_SCHEMA.extend(
                        {
                            cv.Optional(
                                CONF_TRANSPARENCY, default=t
                            ): validate_transparency((t,)),
                            cv.Optional(CONF_TYPE, default=image_type): validate_type(
                                (image_type,)
                            ),
                        }
                    )
                )
                for t in IMAGE_TYPE[image_type].allow_config.intersection(
                    TRANSPARENCY_TYPES
                )
            }
        ),
        # Allow a default configuration with no transparency preselected
        cv.ensure_list(
            BASE_SCHEMA.extend(
                {
                    cv.Optional(
                        CONF_TRANSPARENCY, default=CONF_OPAQUE
                    ): validate_transparency(),
                    cv.Optional(CONF_TYPE, default=image_type): validate_type(
                        (image_type,)
                    ),
                }
            )
        ),
    )


# The config schema can be a (possibly empty) single list of images,
# or a dictionary of image types each with a list of images
# or a dictionary with keys `defaults:` and `images:`


def _config_schema(config):
    if isinstance(config, list):
        return cv.Schema([IMAGE_SCHEMA])(config)
    if not isinstance(config, dict):
        raise cv.Invalid(
            "Badly formed image configuration, expected a list or a dictionary"
        )
    if CONF_DEFAULTS in config or CONF_IMAGES in config:
        return validate_defaults(
            cv.Schema(
                {
                    cv.Required(CONF_DEFAULTS): OPTIONS_SCHEMA,
                    cv.Required(CONF_IMAGES): cv.ensure_list(IMAGE_SCHEMA_NO_DEFAULTS),
                }
            )(config)
        )
    if CONF_ID in config or CONF_FILE in config:
        return cv.ensure_list(IMAGE_SCHEMA)([config])
    return cv.Schema(
        {cv.Optional(t.lower()): typed_image_schema(t) for t in IMAGE_TYPE}
    )(config)


CONFIG_SCHEMA = cv.All(_config_schema)


async def write_image(config, all_frames=False):
    # Cas spécial : source = sd_card → image lue à l'exécution, pas à la compilation
    if config.get(CONF_SOURCE) == SOURCE_SD_CARD:
        _LOGGER.info(f"Skipping compile-time processing for SD card image: {config[CONF_FILE]}")
        return None, None, None, None, None, None

    path = Path(config[CONF_FILE])
    if not path.is_file():
        raise core.EsphomeError(f"Could not load image file {path}")

    resize = config.get(CONF_RESIZE)
    if is_svg_file(path):
        # Local import so use of non-SVG files needn't require cairosvg installed
        from cairosvg import svg2png

        if not resize:
            resize = (None, None)
        with open(path, "rb") as file:
            image = svg2png(
                file_obj=file,
                output_width=resize[0],
                output_height=resize[1],
            )
        image = Image.open(io.BytesIO(image))
        width, height = image.size
    else:
        image = Image.open(path)
        width, height = image.size
        if resize:
            # Preserve aspect ratio
            new_width_max = min(width, resize[0])
            new_height_max = min(height, resize[1])
            ratio = min(new_width_max / width, new_height_max / height)
            width, height = int(width * ratio), int(height * ratio)

    if not resize and (width > 500 or height > 500):
        _LOGGER.warning(
            'The image "%s" you requested is very big. Please consider'
            " using the resize parameter.",
            path,
        )

    dither = (
        Image.Dither.NONE
        if config[CONF_DITHER] == "NONE"
        else Image.Dither.FLOYDSTEINBERG
    )
    type = config[CONF_TYPE]
    transparency = config[CONF_TRANSPARENCY]
    invert_alpha = config[CONF_INVERT_ALPHA]
    frame_count = 1
    if all_frames:
        try:
            frame_count = image.n_frames
        except AttributeError:
            pass
        if frame_count <= 1:
            _LOGGER.warning("Image file %s has no animation frames", path)

    total_rows = height * frame_count
    encoder = IMAGE_TYPE[type](width, total_rows, transparency, dither, invert_alpha)
    if byte_order := config.get(CONF_BYTE_ORDER):
        encoder.set_big_endian(byte_order == "BIG_ENDIAN")

    for frame_index in range(frame_count):
        image.seek(frame_index)
        pixels = encoder.convert(image.resize((width, height)), path).getdata()
        for row in range(height):
            for col in range(width):
                encoder.encode(pixels[row * width + col])
            encoder.end_row()

    rhs = [HexInt(x) for x in encoder.data]
    prog_arr = cg.progmem_array(config[CONF_RAW_DATA_ID], rhs)
    image_type = get_image_type_enum(type)
    trans_value = get_transparency_enum(encoder.transparency)

    return prog_arr, width, height, image_type, trans_value, frame_count


async def to_code(config):
    if isinstance(config, list):
        for entry in config:
            await to_code(entry)
    elif CONF_ID not in config:
        for entry in config.values():
            await to_code(entry)
    else:
        # Vérifier si c'est une image SD card
        if config.get(CONF_SOURCE) == SOURCE_SD_CARD:
            # Ajouter la dépendance au composant sd_mmc_card
            cg.add_define("USE_SD_MMC_CARD")
            
            # Créer une instance SDCardImage
            var = cg.new_Pvariable(
                config[CONF_ID],
                config[CONF_FILE],  # chemin sur la SD
                get_image_type_enum(config[CONF_TYPE]),
                get_transparency_enum(config[CONF_TRANSPARENCY])
            )
            
            # Configurer les options si présentes
            if CONF_RESIZE in config:
                cg.add(var.set_resize(config[CONF_RESIZE][0], config[CONF_RESIZE][1]))
            if config.get(CONF_DITHER) == "FLOYDSTEINBERG":
                cg.add(var.set_dither(True))
            if config.get(CONF_INVERT_ALPHA, False):
                cg.add(var.set_invert_alpha(True))
            if byte_order := config.get(CONF_BYTE_ORDER):
                cg.add(var.set_big_endian(byte_order == "BIG_ENDIAN"))
        else:
            # Image normale (locale, web, mdi, etc.)
            prog_arr, width, height, image_type, trans_value, _ = await write_image(config)
            cg.new_Pvariable(
                config[CONF_ID], prog_arr, width, height, image_type, trans_value
            )
