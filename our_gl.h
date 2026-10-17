#pragma once

#include "tgaimage.h"
#include "geometry.h"

mat<4,4> make_lookat(const vec3 eye, const vec3 center, const vec3 up);
void lookat(const vec3 eye, const vec3 center, const vec3 up);
void init_perspective(double camera_distance, double fov_degrees, double aspect,
                      double near_plane, double far_plane);
void init_viewport (const int x, const int y, const int w, const int h);
void init_zbuffer (const int x, const int y);
void init_sbuffer(const int x, const int y);

struct IShader {
    static TGAColor sample2D(const TGAImage &img, const vec2 &uvf) {
        return img.get(uvf[0] * img.width(), uvf[1] * img.height());
    }
    virtual std::pair<bool,TGAColor> fragment(const vec3 bar, const vec3 dbar_dx,
                                               const vec3 dbar_dy) const = 0;
};

typedef vec4 Triangle[3]; // a triangle primitive is made of three ordered points
enum class RenderPass { Opaque, Transparent };
struct TransparentFragment {
    double z;
    TGAColor color;
};
using TransparentBuffer = std::vector<std::vector<TransparentFragment>>;
struct RenderTarget {
    int width, height, samples;
    std::vector<double> depth;
    std::vector<TGAColor> color;
    TransparentBuffer transparent;

    RenderTarget(int width, int height, int samples);
    void resolve(TGAImage &framebuffer, std::vector<double> &pixel_depth) const;
};
void rasterize(const Triangle &clip, const IShader &shader, RenderTarget &target, RenderPass pass,
               int y_begin, int y_end);
void rasterize_shadow(const Triangle &clip, const IShader &shader, const mat<4,4> &light_viewport,
                      int width, int height, std::vector<double> &depth_buffer);
void blend_transparency(RenderTarget &target);
