#include "texture.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
vec4 decode(const TGAColor &color) {
    if (color.bytespp == 1) {
        const double gray = color[0]/255.;
        return {gray, gray, gray, 1};
    }
    return {color[2]/255., color[1]/255., color[0]/255.,
            color.bytespp == 4 ? color[3]/255. : 1.};
}

TGAColor encode(const vec4 &color, const int bytespp) {
    const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::round(value*255.), 0., 255.));
    };
    if (bytespp == 1) return {byte(color.x), 0, 0, 0, 1};
    return {byte(color.z), byte(color.y), byte(color.x), byte(color.w),
            static_cast<std::uint8_t>(bytespp)};
}

vec4 mix_alpha(const vec4 &a, const vec4 &b, const double t) {
    const double alpha = a.w*(1-t) + b.w*t;
    if (alpha <= 0) return {0, 0, 0, 0};
    return {(a.x*a.w*(1-t) + b.x*b.w*t)/alpha,
            (a.y*a.w*(1-t) + b.y*b.w*t)/alpha,
            (a.z*a.w*(1-t) + b.z*b.w*t)/alpha, alpha};
}

vec4 bilinear(const TGAImage &image, const vec2 &uv) {
    const double x = std::clamp(uv.x, 0., 1.)*image.width()-.5;
    const double y = std::clamp(uv.y, 0., 1.)*image.height()-.5;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double tx = x-x0, ty = y-y0;
    const auto texel = [&](int px, int py) {
        return decode(image.get(std::clamp(px, 0, image.width()-1),
                                std::clamp(py, 0, image.height()-1)));
    };
    return mix_alpha(mix_alpha(texel(x0, y0), texel(x0+1, y0), tx),
                     mix_alpha(texel(x0, y0+1), texel(x0+1, y0+1), tx), ty);
}
}

Texture2D::Texture2D(TGAImage image) {
    if (!image.width() || !image.height()) return;
    levels.push_back(std::move(image));
    while (levels.back().width() > 1 || levels.back().height() > 1) {
        const TGAImage &source = levels.back();
        const int width = std::max(1, source.width()/2);
        const int height = std::max(1, source.height()/2);
        const int bytespp = source.get(0, 0).bytespp;
        TGAImage next(width, height, bytespp);
        for (int y=0; y<height; y++) {
            for (int x=0; x<width; x++) {
                const int x0 = x*source.width()/width, x1 = (x+1)*source.width()/width;
                const int y0 = y*source.height()/height, y1 = (y+1)*source.height()/height;
                vec4 sum = {};
                for (int py=y0; py<y1; py++) {
                    for (int px=x0; px<x1; px++) {
                        const vec4 c = decode(source.get(px, py));
                        sum.x += c.x*c.w;
                        sum.y += c.y*c.w;
                        sum.z += c.z*c.w;
                        sum.w += c.w;
                    }
                }
                const double count = (x1-x0)*(y1-y0);
                vec4 average = {0, 0, 0, sum.w/count};
                if (sum.w > 0) average = {sum.x/sum.w, sum.y/sum.w, sum.z/sum.w, sum.w/count};
                next.set(x, y, encode(average, bytespp));
            }
        }
        levels.push_back(std::move(next));
    }
}

bool Texture2D::load(const std::string &filename) {
    TGAImage image;
    if (!image.read_tga_file(filename)) return false;
    *this = Texture2D(std::move(image));
    return true;
}

vec4 Texture2D::sample(const vec2 &uv, const vec2 &uv_dx, const vec2 &uv_dy,
                       const TextureFilter filter) const {
    if (levels.empty()) return {0, 0, 0, 0};
    if (filter == TextureFilter::Nearest) {
        const TGAImage &base = levels[0];
        const int x = std::clamp(static_cast<int>(uv.x*base.width()), 0, base.width()-1);
        const int y = std::clamp(static_cast<int>(uv.y*base.height()), 0, base.height()-1);
        return decode(base.get(x, y));
    }
    const double width = levels[0].width(), height = levels[0].height();
    const double footprint_x = std::hypot(uv_dx.x*width, uv_dx.y*height);
    const double footprint_y = std::hypot(uv_dy.x*width, uv_dy.y*height);
    const double lod = std::min<double>(levels.size()-1,
                                       std::log2(std::max({1., footprint_x, footprint_y})));
    const int low = static_cast<int>(lod);
    const vec4 base = bilinear(levels[low], uv);
    if (lod == low) return base;
    const int high = std::min<int>(low+1, levels.size()-1);
    return mix_alpha(base, bilinear(levels[high], uv), lod-low);
}
