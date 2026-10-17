#include "our_gl.h"
#include <iostream>

struct FlatShader : IShader {
    TGAColor color;
    explicit FlatShader(TGAColor value) : color(value) {}
    std::pair<bool, TGAColor> fragment(const vec3) const override { return {false, color}; }
};

bool test_projection_and_clipping() {
    extern mat<4,4> Perspective;
    init_perspective(2., 90., 1., .1, 10.);
    const vec4 near_point = Perspective * vec4{0, 0, 1.9, 1};
    const vec4 far_point = Perspective * vec4{0, 0, -8., 1};
    if (std::abs(near_point.z/near_point.w-1.) > 1e-12 ||
        std::abs(far_point.z/far_point.w+1.) > 1e-12) return false;
    init_perspective(2., 90., 2., .1, 10.);
    const vec4 wide_x = Perspective * vec4{1, 0, 0, 1};
    const vec4 wide_y = Perspective * vec4{0, 1, 0, 1};
    if (std::abs(wide_x.x/wide_x.w-.25) > 1e-12 ||
        std::abs(wide_y.y/wide_y.w-.5) > 1e-12) return false;
    init_perspective(2., 90., 1., .1, 10.);

    constexpr int size = 32;
    init_viewport(0, 0, size, size);
    FlatShader white({255, 255, 255, 255});
    const Triangle base = {{-.5, -.5, 0, 1}, {.5, -.5, 0, 1}, {0, .5, 0, 1}};
    for (int axis=0; axis<3; axis++) {
        for (double side : {-1., 1.}) {
            Triangle partial = {base[0], base[1], base[2]};
            Triangle outside = {base[0], base[1], base[2]};
            if (axis == 2) {
                partial[0].z = side*1.5;
                for (vec4 &vertex : outside) vertex.z = side*1.5;
            } else {
                for (vec4 &vertex : partial) vertex[axis] += side*.8;
                for (vec4 &vertex : outside) vertex[axis] += side*2.;
            }
            RenderTarget target(size, size, 1);
            rasterize(partial, white, target, RenderPass::Opaque, 0, size);
            int covered = 0;
            for (double depth : target.depth) {
                if (depth <= -999.) continue;
                if (depth < -1.-1e-12 || depth > 1.+1e-12) return false;
                covered++;
            }
            if (!covered) return false;
            RenderTarget rejected(size, size, 1);
            rasterize(outside, white, rejected, RenderPass::Opaque, 0, size);
            for (double depth : rejected.depth) if (depth > -999.) return false;
        }
    }
    Triangle across_near = {
        Perspective * vec4{-.5, -.5, 0, 1},
        Perspective * vec4{.5, -.5, 0, 1},
        Perspective * vec4{0, .5, 1.95, 1}
    };
    RenderTarget near_target(size, size, 1);
    rasterize(across_near, white, near_target, RenderPass::Opaque, 0, size);
    int near_coverage = 0;
    for (double depth : near_target.depth) {
        if (depth <= -999.) continue;
        if (depth < -1.-1e-12 || depth > 1.+1e-12) return false;
        near_coverage++;
    }
    if (!near_coverage) return false;

    Triangle behind_camera = {
        Perspective * vec4{-.5, -.5, 2.5, 1},
        Perspective * vec4{.5, -.5, 2.5, 1},
        Perspective * vec4{0, .5, 2.5, 1}
    };
    RenderTarget rejected(size, size, 1);
    rasterize(behind_camera, white, rejected, RenderPass::Opaque, 0, size);
    for (double depth : rejected.depth) if (depth > -999.) return false;
    return true;
}

TGAColor render_layers(bool near_first) {
    constexpr int size = 8;
    init_viewport(0, 0, size, size);
    TGAImage image(size, size, TGAImage::RGB);
    RenderTarget target(size, size, 1);
    FlatShader opaque({255, 0, 0, 255});
    FlatShader far({0, 0, 255, 128});
    FlatShader near({0, 255, 0, 128});
    FlatShader hidden({255, 255, 255, 128});

    Triangle base = {{-.8, -.8, 0., 1.}, {.8, -.8, 0., 1.}, {0., .8, 0., 1.}};
    Triangle far_tri = {{-.8, -.8, .25, 1.}, {.8, -.8, .25, 1.}, {0., .8, .25, 1.}};
    Triangle near_tri = {{-.8, -.8, .5, 1.}, {.8, -.8, .5, 1.}, {0., .8, .5, 1.}};
    Triangle hidden_tri = {{-.8, -.8, -.5, 1.}, {.8, -.8, -.5, 1.}, {0., .8, -.5, 1.}};

    rasterize(base, opaque, target, RenderPass::Opaque, 0, size);
    if (near_first) {
        rasterize(near_tri, near, target, RenderPass::Transparent, 0, size);
        rasterize(far_tri, far, target, RenderPass::Transparent, 0, size);
    } else {
        rasterize(far_tri, far, target, RenderPass::Transparent, 0, size);
        rasterize(near_tri, near, target, RenderPass::Transparent, 0, size);
    }
    rasterize(hidden_tri, hidden, target, RenderPass::Transparent, 0, size);
    blend_transparency(target);
    std::vector<double> pixel_depth;
    target.resolve(image, pixel_depth);
    return image.get(4, 4);
}

bool test_msaa_coverage() {
    constexpr int size = 8;
    init_viewport(0, 0, size, size);
    FlatShader white({255, 255, 255, 255});
    // This small triangle reaches one of pixel (4,4)'s four subpixel samples.
    Triangle corner = {{-.2, -.2, 0., 1.}, {.1, -.2, 0., 1.}, {-.2, .1, 0., 1.}};
    TGAImage image_1x(size, size, TGAImage::RGB), image_4x(size, size, TGAImage::RGB);
    RenderTarget target_1x(size, size, 1), target_4x(size, size, 4);
    rasterize(corner, white, target_1x, RenderPass::Opaque, 0, size);
    rasterize(corner, white, target_4x, RenderPass::Opaque, 0, size);
    std::vector<double> pixel_depth;
    target_1x.resolve(image_1x, pixel_depth);
    target_4x.resolve(image_4x, pixel_depth);
    if (image_1x.get(4, 4)[0] != 0 || image_4x.get(4, 4)[0] != 64) return false;

    FlatShader blue({255, 0, 0, 255}), red({0, 0, 255, 128});
    Triangle base = {{-.8, -.8, 0., 1.}, {.8, -.8, 0., 1.}, {0., .8, 0., 1.}};
    Triangle translucent = {{-.2, -.2, .5, 1.}, {.1, -.2, .5, 1.}, {-.2, .1, .5, 1.}};
    RenderTarget layered(size, size, 4);
    TGAImage blended(size, size, TGAImage::RGB);
    rasterize(base, blue, layered, RenderPass::Opaque, 0, size);
    rasterize(translucent, red, layered, RenderPass::Transparent, 0, size);
    blend_transparency(layered);
    layered.resolve(blended, pixel_depth);
    const TGAColor pixel = blended.get(4, 4);
    return pixel[0] == 223 && pixel[1] == 0 && pixel[2] == 32;
}

int main() {
    if (!test_projection_and_clipping()) {
        std::cerr << "Projection and all six clip planes must preserve visible fragments\n";
        return 1;
    }
    const TGAColor a = render_layers(true), b = render_layers(false);
    // Blue opaque, red translucent behind green translucent: expected BGRA.
    if (a[0] != 63 || a[1] != 128 || a[2] != 63 ||
        b[0] != a[0] || b[1] != a[1] || b[2] != a[2]) {
        std::cerr << "Transparent layers must sort by depth and reject hidden fragments\n";
        return 1;
    }
    if (!test_msaa_coverage()) {
        std::cerr << "MSAA must preserve partial triangle coverage\n";
        return 1;
    }
    return 0;
}
