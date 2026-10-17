#include "our_gl.h"
#include "scene.h"
#include "scene_file.h"
#include "ssao.h"
#include "texture.h"
#include <iostream>
#include <sstream>

struct FlatShader : IShader {
    TGAColor color;
    explicit FlatShader(TGAColor value) : color(value) {}
    FragmentOutput fragment(const vec3, const vec3, const vec3) const override {
        return {false, color, {0, 0, 0, color[3]}};
    }
};

struct TextureShader : IShader {
    const Texture2D &texture;
    explicit TextureShader(const Texture2D &value) : texture(value) {}
    FragmentOutput fragment(const vec3 bar, const vec3 dx, const vec3 dy) const override {
        const vec2 uv{bar.y, bar.z};
        const vec2 uv_dx{dx.y, dx.z}, uv_dy{dy.y, dy.z};
        const auto gray = static_cast<std::uint8_t>(std::round(texture.sample(uv, uv_dx, uv_dy).x*255.));
        return {false, {gray, gray, gray, 255}, {0, 0, 0, 255}};
    }
};

struct SplitShader : IShader {
    FragmentOutput fragment(const vec3, const vec3, const vec3) const override {
        return {false, {0, 0, 100, 255}, {0, 80, 0, 255}};
    }
};

bool test_model_transforms() {
    SceneInstance instance;
    instance.translation = {3, 4, 5};
    instance.rotation = {0, 0, 90};
    instance.scale = {2, 3, 4};
    const mat<4,4> model = make_model_matrix(instance);
    const vec4 point = model * vec4{1, 2, 3, 1};
    if (std::abs(point.x+3) > 1e-12 || std::abs(point.y-6) > 1e-12 ||
        std::abs(point.z-17) > 1e-12 || std::abs(point.w-1) > 1e-12) return false;

    instance.scale = {2, 1, 3};
    const mat<4,4> scaled = make_model_matrix(instance);
    const vec4 normal = scaled.invert_transpose() * vec4{1, 1, 1, 0};
    const vec4 tangent = scaled * vec4{1, -1, 0, 0};
    return std::abs(normal*tangent) < 1e-12;
}

bool test_camera_orbit() {
    const vec3 eye{0, 1, 2}, center{0, 1, 0}, up{0, 1, 0};
    const vec3 quarter = orbit_eye(eye, center, up, std::numbers::pi/2.);
    const vec3 full = orbit_eye(eye, center, up, 2.*std::numbers::pi);
    return std::abs(quarter.x-2) < 1e-12 && std::abs(quarter.y-1) < 1e-12 &&
           std::abs(quarter.z) < 1e-12 && norm(full-eye) < 1e-12 &&
           std::abs(norm(quarter-center)-norm(eye-center)) < 1e-12;
}

bool test_scene_file() {
    std::istringstream input("# scene\nsize 640 480\nlight 0 1 0\n"
                             "instance ../obj/sample.obj\ntranslate 2 3 4\n"
                             "instance ../obj/sample.obj\nrotate 0 90 0\n");
    SceneConfig config;
    std::string error;
    if (!parse_scene(input, "/tmp/scenes", config, error)) return false;
    if (config.width != 640 || config.height != 480 || config.light.y != 1 ||
        config.instances.size() != 2 || config.instances[0].translation.x != 2 ||
        config.instances[1].translation.x != 0 || config.instances[1].rotation.y != 90 ||
        config.instances[0].path != "/tmp/obj/sample.obj") return false;

    std::istringstream bad("instance mesh.obj\nscale 1 2 3 extra\n");
    return !parse_scene(bad, "/tmp", config, error) && error.find("line 2") != std::string::npos;
}

bool test_ambient_and_ssao() {
    extern mat<4,4> Perspective, Viewport;
    constexpr int size = 32;
    init_perspective(2., 90., 1., .1, 10.);
    init_viewport(0, 0, size, size);
    const vec4 plane = Perspective*vec4{0, 0, 0, 1};
    std::vector<double> depth(size*size, plane.z/plane.w);
    const std::vector<double> visibility = compute_ssao(depth, size, size, Perspective, Viewport, .5, 32);
    if (visibility[16+16*size] < .98) return false;
    const vec4 raised = Perspective*vec4{0, 0, .35, 1};
    for (int y=14; y<=18; y++)
        for (int x=14; x<=18; x++) depth[x+y*size] = raised.z/raised.w;
    const std::vector<double> with_occluder = compute_ssao(depth, size, size, Perspective, Viewport, .5, 32);
    double darkest = 1.;
    for (int y=11; y<=21; y++)
        for (int x=11; x<=21; x++) darkest = std::min(darkest, with_occluder[x+y*size]);
    if (darkest > .95) return false;

    Triangle triangle = {{-.5, -.5, 0, 1}, {.5, -.5, 0, 1}, {0, .5, 0, 1}};
    SplitShader shader;
    RenderTarget target(size, size, 1);
    rasterize(triangle, shader, target, RenderPass::Opaque, 0, size);
    TGAImage direct(size, size, TGAImage::RGB), ambient(size, size, TGAImage::RGB);
    target.resolve(direct, depth, &ambient);
    return direct.get(16, 16)[2] == 100 && direct.get(16, 16)[1] == 0 &&
           ambient.get(16, 16)[2] == 0 && ambient.get(16, 16)[1] == 80;
}

bool test_texture_filtering() {
    TGAImage colors(2, 2, TGAImage::RGB);
    colors.set(0, 0, {0, 0, 255, 255});
    colors.set(1, 0, {0, 255, 0, 255});
    colors.set(0, 1, {255, 0, 0, 255});
    colors.set(1, 1, {255, 255, 255, 255});
    Texture2D bilinear(colors);
    const vec4 middle = bilinear.sample({.5, .5});
    if (std::abs(middle.x-.5) > .01 || std::abs(middle.y-.5) > .01 ||
        std::abs(middle.z-.5) > .01) return false;
    const vec4 nearest = bilinear.sample({.5, .5}, {}, {}, TextureFilter::Nearest);
    if (nearest.x != 1 || nearest.y != 1 || nearest.z != 1) return false;

    TGAImage checker(4, 4, TGAImage::GRAYSCALE);
    for (int y=0; y<4; y++)
        for (int x=0; x<4; x++)
            checker.set(x, y, {static_cast<std::uint8_t>((x+y)%2 ? 255 : 0), 0, 0, 0, 1});
    Texture2D mipmap(checker);
    if (mipmap.sample({.125, .125}).x != 0) return false;
    if (std::abs(mipmap.sample({.125, .125}, {.5, 0}, {0, .5}).x-.5) > .01) return false;

    TGAImage alpha(2, 1, TGAImage::RGBA);
    alpha.set(0, 0, {0, 0, 255, 255});
    alpha.set(1, 0, {255, 0, 0, 0});
    const vec4 blend = Texture2D(alpha).sample({.5, .5});
    if (std::abs(blend.x-1.) >= .01 || blend.y >= .01 || blend.z >= .01 ||
        std::abs(blend.w-.5) >= .01) return false;

    TGAImage dense(31, 31, TGAImage::GRAYSCALE);
    for (int y=0; y<31; y++)
        for (int x=0; x<31; x++)
            dense.set(x, y, {static_cast<std::uint8_t>((x+y)%2 ? 255 : 0), 0, 0, 0, 1});
    Texture2D minified(dense);
    TextureShader shader(minified);
    constexpr int size = 32;
    init_viewport(0, 0, size, size);
    Triangle triangle = {{-.5, -.5, 0, 1}, {.5, -.5, 0, 1}, {0, .5, 0, 1}};
    RenderTarget target(size, size, 1);
    TGAImage image(size, size, TGAImage::RGB);
    rasterize(triangle, shader, target, RenderPass::Opaque, 0, size);
    std::vector<double> depth;
    target.resolve(image, depth);
    return std::abs(static_cast<int>(image.get(16, 16)[0])-128) < 12;
}

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
    if (!test_camera_orbit()) {
        std::cerr << "Camera orbit must preserve radius and close after one turn\n";
        return 1;
    }
    if (!test_scene_file()) {
        std::cerr << "Scene file parsing or relative paths are incorrect\n";
        return 1;
    }
    if (!test_model_transforms()) {
        std::cerr << "Instance transforms or transformed normals are incorrect\n";
        return 1;
    }
    if (!test_ambient_and_ssao()) {
        std::cerr << "SSAO must leave a flat surface clear and keep ambient separate\n";
        return 1;
    }
    if (!test_texture_filtering()) {
        std::cerr << "Texture filtering must blend texels and choose mip levels\n";
        return 1;
    }
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
