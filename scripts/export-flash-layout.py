"""PlatformIO post script: export the actual uploader's image offsets; never upload."""
Import("env")
import json
from pathlib import Path

def export_layout(source, target, env):
    board = env.BoardConfig()
    images = [{"offset": int(str(offset), 0), "path": env.subst(str(filename))}
              for offset, filename in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append({"offset": int(env.subst("$ESP32_APP_OFFSET"), 0),
                   "path": env.subst("$BUILD_DIR/${PROGNAME}.bin")})
    layout = {"chip": board.get("build.mcu"),
              "flashSize": board.get("upload.flash_size"), "images": images,
              "frameworkSource": env.PioPlatform().get_package_dir("framework-arduinoespressif32"),
              "sdkPackage": env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"),
              "librarySource": env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")}
    Path(env.subst("$BUILD_DIR/flash-layout.json")).write_text(json.dumps(layout, indent=2) + "\n")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_layout)
