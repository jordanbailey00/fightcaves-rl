#ifndef FC_ASSET_RAYLIB_H
#define FC_ASSET_RAYLIB_H

#include "fc_assets.h"
#include "raylib.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static const char* fc_asset_ext(const char* path, const char* fallback) {
    const char* dot = path ? strrchr(path, '.') : NULL;
    return dot && dot[0] ? dot : fallback;
}

static Texture2D fc_load_texture_asset(const char* path) {
    Texture2D empty = {0};
    size_t size = 0;
    unsigned char* bytes = fc_asset_read_all(path, &size);
    Image img;
    Texture2D tex;

    if (!bytes || size == 0) return empty;
    img = LoadImageFromMemory(fc_asset_ext(path, ".png"), bytes, (int)size);
    free(bytes);
    if (!img.data) return empty;

    tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static Image fc_load_image_asset(const char* path) {
    Image empty = {0};
    size_t size = 0;
    unsigned char* bytes = fc_asset_read_all(path, &size);
    Image img;

    if (!bytes || size == 0) return empty;
    img = LoadImageFromMemory(fc_asset_ext(path, ".png"), bytes, (int)size);
    free(bytes);
    return img;
}

static Font fc_load_font_asset(const char* path, int font_size) {
    Font empty = {0};
    size_t size = 0;
    unsigned char* bytes = fc_asset_read_all(path, &size);
    Font font;

    if (!bytes || size == 0) return empty;
    font = LoadFontFromMemory(fc_asset_ext(path, ".ttf"), bytes, (int)size,
                              font_size, NULL, 95);
    free(bytes);
    return font.texture.id != 0 ? font : empty;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif /* FC_ASSET_RAYLIB_H */
