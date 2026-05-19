#ifndef RUNEC_ASSET_RAYLIB_H
#define RUNEC_ASSET_RAYLIB_H

#include "fc_assets.h"
#include "raylib.h"

#include <stdlib.h>
#include <string.h>

/* Compatibility shim for the RuneC UI sources copied into demo-env. */
#define rc_asset_exists fc_asset_exists
#define rc_asset_fopen fc_asset_fopen
#define rc_asset_close fc_asset_close

static const char *runec_asset_ext(const char *path, const char *fallback) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && dot[0] ? dot : fallback;
}

static Texture2D runec_load_texture_asset(const char *path) {
    Texture2D empty = {0};
    size_t size = 0;
    unsigned char *bytes = fc_asset_read_all(path, &size);
    if (!bytes || size == 0)
        return empty;
    Image img = LoadImageFromMemory(runec_asset_ext(path, ".png"),
                                    bytes, (int)size);
    free(bytes);
    if (!img.data)
        return empty;
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static Image runec_load_image_asset(const char *path) {
    Image empty = {0};
    size_t size = 0;
    unsigned char *bytes = fc_asset_read_all(path, &size);
    if (!bytes || size == 0)
        return empty;
    Image img = LoadImageFromMemory(runec_asset_ext(path, ".png"),
                                    bytes, (int)size);
    free(bytes);
    return img;
}

static Font runec_load_font_asset(const char *path, int font_size) {
    Font empty = {0};
    size_t size = 0;
    unsigned char *bytes = fc_asset_read_all(path, &size);
    if (!bytes || size == 0)
        return empty;
    Font font = LoadFontFromMemory(runec_asset_ext(path, ".ttf"),
                                   bytes, (int)size, font_size, NULL, 95);
    free(bytes);
    return font.texture.id != 0 ? font : empty;
}

#endif /* RUNEC_ASSET_RAYLIB_H */
