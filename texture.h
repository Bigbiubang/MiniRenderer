#pragma once

#include "geometry.h"
#include "tgaimage.h"
#include <string>
#include <vector>

enum class TextureFilter { Nearest, Trilinear };

class Texture2D {
    std::vector<TGAImage> levels;

public:
    Texture2D() = default;
    explicit Texture2D(TGAImage image);
    bool load(const std::string &filename);
    bool empty() const { return levels.empty(); }
    vec4 sample(const vec2 &uv, const vec2 &uv_dx = {}, const vec2 &uv_dy = {},
                TextureFilter filter = TextureFilter::Trilinear) const;
};
