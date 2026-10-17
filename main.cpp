#include "our_gl.h"
#include "model.h"
#include "scene.h"
#include "scene_file.h"
#include "ssao.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>

extern mat<4,4> ModelView, Perspective, Viewport, LightView, LightProjection;
extern std::vector<double> zbuffer, sbuffer;

mat<4,4> fit_orthographic(const std::vector<Model> &models,
                          const std::vector<SceneInstance> &instances, const mat<4,4> &view) {
    double low[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    double high[3] = {-low[0], -low[1], -low[2]};
    for (const SceneInstance &instance : instances) {
        const Model &model = models[instance.mesh];
        const mat<4,4> light_model = view * instance.model;
        for (int i=0; i<model.nverts(); i++) {
            vec4 p = light_model * model.vert(i);
            for (int axis=0; axis<3; axis++) {
                low[axis] = std::min(low[axis], p[axis]);
                high[axis] = std::max(high[axis], p[axis]);
            }
        }
    }
    double mid[3], half[3];
    for (int axis=0; axis<3; axis++) {
        mid[axis] = (low[axis]+high[axis])/2.;
        half[axis] = std::max((high[axis]-low[axis])*.55, .1);
    }
    return {{{1/half[0],0,0,-mid[0]/half[0]},
             {0,1/half[1],0,-mid[1]/half[1]},
             {0,0,1/half[2],-mid[2]/half[2]},
             {0,0,0,1}}};
}

struct BlankShader : IShader {
    const Model &model;
    mat<4,4> transform;
    TextureFilter filter;
    BlankShader(const Model &m, const mat<4,4> &matrix, const mat<4,4> &model_matrix,
                TextureFilter texture_filter)
        : model(m), transform(matrix*model_matrix), filter(texture_filter) {}
    vec2 varying_uv[3];

    virtual vec4 vertex(const int face, const int vert) {
        varying_uv[vert] = model.uv(face, vert);
        return transform * model.vert(face, vert);
    }

    virtual FragmentOutput fragment(const vec3 bar, const vec3, const vec3) const {
        vec2 uv = varying_uv[0]*bar[0] + varying_uv[1]*bar[1] + varying_uv[2]*bar[2];
        // Only fully opaque texels cast shadows in this first version.
        const auto alpha = static_cast<std::uint8_t>(model.diffuse(uv, {}, {}, filter).w*255.);
        return {false, {255, 255, 255, alpha}, {0, 0, 0, alpha}};
    }
};


struct PhongShader : IShader {
    const Model &model;
    TextureFilter filter;
    vec4 l;              // light direction in eye coordinates
    mat<4,4> normal_matrix;
    mat<4,4> object_to_world;
    vec2 varying_uv[3];  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    vec4 varying_nrm[3]; // vertex normals used when the model has no normal map
    vec3 eye;
    vec3 center;
    vec4 camera_pos;     //ModelView 下的camera_pos;
    vec4 tri[3];         // triangle in view coordinates
    vec4 varying_world[3];
    mat<4,4> shadow_matrix; // world coordinates -> shadow-map pixels
    int shadow_width, shadow_height;

    PhongShader(const vec3 light, const Model &m, const mat<4,4> &model_matrix,
                const vec3 &eye_, const vec3 &center_,
                const mat<4,4> &light_viewport, int sw, int sh, TextureFilter texture_filter)
        : model(m), filter(texture_filter), normal_matrix((ModelView*model_matrix).invert_transpose()),
          object_to_world(model_matrix), eye(eye_), center(center_),
          shadow_matrix(light_viewport * LightProjection * LightView), shadow_width(sw), shadow_height(sh) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.})); // transform the light vector to view coordinates
        camera_pos = ModelView * vec4{eye.x, eye.y, eye.z, 1.};

    }

    virtual vec4 vertex(const int face, const int vert) {
        varying_uv[vert] = model.uv(face, vert);
        varying_nrm[vert] = normal_matrix * model.normal(face, vert);
        varying_world[vert] = object_to_world * model.vert(face, vert);
        vec4 gl_Position = ModelView * varying_world[vert];
        tri[vert] = gl_Position;
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual FragmentOutput fragment(const vec3 bar, const vec3 dbar_dx,
                                    const vec3 dbar_dy) const {
        vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
        vec2 uv_dx = varying_uv[0] * dbar_dx[0] + varying_uv[1] * dbar_dx[1] + varying_uv[2] * dbar_dx[2];
        vec2 uv_dy = varying_uv[0] * dbar_dy[0] + varying_uv[1] * dbar_dy[1] + varying_uv[2] * dbar_dy[2];
        vec3 base_n = normalized((varying_nrm[0]*bar[0] + varying_nrm[1]*bar[1] + varying_nrm[2]*bar[2]).xyz());
        vec3 normal = base_n;
        if (model.has_normalmap()) {
            vec3 e1 = (tri[1]-tri[0]).xyz(), e2 = (tri[2]-tri[0]).xyz();
            vec2 d1 = varying_uv[1]-varying_uv[0], d2 = varying_uv[2]-varying_uv[0];
            double det = cross(d1, d2);
            if (std::abs(det) > 1e-12) {
                vec3 tangent = (e1*d2.y - e2*d1.y) / det;
                vec3 bitangent = (e2*d1.x - e1*d2.x) / det;
                tangent = tangent - base_n*(base_n*tangent);
                if (norm(tangent) > 1e-12) {
                    tangent = normalized(tangent);
                    vec3 b = cross(base_n, tangent);
                    if (b*bitangent < 0) b = b*-1.;
                    vec3 map_n = model.normal(uv, uv_dx, uv_dy, filter).xyz();
                    normal = normalized(tangent*map_n.x + b*map_n.y + base_n*map_n.z);
                }
            }
        }
        vec4 n = {normal.x, normal.y, normal.z, 0};

        vec4 r = normalized(n * (n * l)*2 - l);                   // reflected light direction

        vec4 pos = bar[0] * tri[0] + bar[1] * tri[1] + bar[2] * tri[2];
        vec4 v = normalized(camera_pos - pos);

        double ambient = .4;                                      // ambient light intensity
        vec4 basecolor = model.diffuse(uv, uv_dx, uv_dy, filter);  //归一化后的漫反射基础颜色

        double diff = std::max(0., n * l);

        // diffuse light intensity
        double spec = 2. * model.specular(uv, uv_dx, uv_dy, filter) * std::pow(std::max(r * v, 0.), 35); // specular map controls highlight strength

        vec4 world = varying_world[0]*bar[0] + varying_world[1]*bar[1] + varying_world[2]*bar[2];
        vec4 shadow_pos = shadow_matrix * world;
        double sx = shadow_pos.x / shadow_pos.w, sy = shadow_pos.y / shadow_pos.w;
        if (sx >= 0 && sx < shadow_width && sy >= 0 && sy < shadow_height) {
            constexpr double shadow_bias = .015;
            const double receiver_depth = shadow_pos.z / shadow_pos.w;
            const int center_x = static_cast<int>(sx), center_y = static_cast<int>(sy);
            // Average neighboring shadow-map texels to soften hard, jagged edges.
            int lit = 0, samples = 0;
            for (int dy=-1; dy<=1; dy++) {
                for (int dx=-1; dx<=1; dx++) {
                    int x = center_x + dx, y = center_y + dy;
                    if (x < 0 || x >= shadow_width || y < 0 || y >= shadow_height) continue;
                    double shadow_depth = sbuffer[x + y*shadow_width];
                    lit += receiver_depth >= shadow_depth - shadow_bias;
                    samples++;
                }
            }
            double visibility = static_cast<double>(lit) / samples;
            diff *= visibility;
            spec *= visibility;
        }

        const auto alpha = static_cast<std::uint8_t>(basecolor.w * 255.);
        TGAColor direct_color = {0, 0, 0, alpha};
        TGAColor ambient_color = {0, 0, 0, alpha};
        for (int channel : {0,1,2}) {
            direct_color[channel] = static_cast<std::uint8_t>(255.*std::min(1., basecolor[2-channel]*diff + spec));
            ambient_color[channel] = static_cast<std::uint8_t>(255.*basecolor[2-channel]*ambient);
        }
        return {false, direct_color, ambient_color};
    }
};

struct DrawCommand {
    PhongShader shader;
    Triangle clip;
};

int main(int argc, char** argv) {
    SceneConfig scene;
    int &msaa_samples = scene.msaa_samples;
    bool timings = false;
    TextureFilter &texture_filter = scene.texture_filter;
    int &width = scene.width, &height = scene.height;
    vec3 &eye = scene.eye, &center = scene.center, &up = scene.up, &light = scene.light;
    double &fov = scene.fov, &near_plane = scene.near_plane, &far_plane = scene.far_plane;
    std::vector<SceneInstance> &instances = scene.instances;
    auto parse_double = [&](int &i, double &value) {
        if (++i >= argc) return false;
        try {
            size_t used = 0;
            const std::string token = argv[i];
            value = std::stod(token, &used);
            return used == token.size() && std::isfinite(value);
        } catch (const std::exception &) {
            return false;
        }
    };
    auto parse_int = [&](int &i, int &value) {
        if (++i >= argc) return false;
        try {
            size_t used = 0;
            const std::string token = argv[i];
            value = std::stoi(token, &used);
            return used == token.size();
        } catch (const std::exception &) {
            return false;
        }
    };
    auto parse_vec3 = [&](int &i, vec3 &value) {
        for (int axis=0; axis<3; axis++)
            if (!parse_double(i, value[axis])) return false;
        return true;
    };
    auto usage = [&] {
        std::cerr << "Usage: " << argv[0]
                  << " [--scene file] [--msaa 1|4] [--timings] [--size width height]"
                  << " [--eye x y z] [--center x y z] [--up x y z]"
                  << " [--light x y z]"
                  << " [--fov degrees] [--near distance] [--far distance]"
                  << " [--texture-filter nearest|trilinear]"
                  << " [obj/model.obj [--translate x y z] [--rotate x y z] [--scale x y z]]"
                  << " [obj/other.obj [transforms] ...]\n";
    };
    std::string scene_path;
    for (int i=1; i<argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            usage();
            return 0;
        }
        if (arg == "--scene") {
            if (++i >= argc || !scene_path.empty()) {
                std::cerr << "--scene expects exactly one file\n";
                return 1;
            }
            scene_path = argv[i];
        }
    }
    if (!scene_path.empty()) {
        std::string error;
        if (!load_scene_file(scene_path, scene, error)) {
            std::cerr << error << '\n';
            return 1;
        }
    }
    const size_t file_instance_count = instances.size();
    for (int i=1; i<argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--scene") {
            i++;
        } else if (arg == "--msaa") {
            if (++i >= argc || (std::string(argv[i]) != "1" && std::string(argv[i]) != "4")) {
                std::cerr << "--msaa expects 1 or 4\n";
                return 1;
            }
            msaa_samples = argv[i][0]-'0';
        } else if (arg == "--timings") {
            timings = true;
        } else if (arg == "--texture-filter") {
            if (++i >= argc || (std::string(argv[i]) != "nearest" && std::string(argv[i]) != "trilinear")) {
                std::cerr << "--texture-filter expects nearest or trilinear\n";
                return 1;
            }
            texture_filter = std::string(argv[i]) == "nearest" ? TextureFilter::Nearest : TextureFilter::Trilinear;
        } else if (arg == "--size") {
            if (!parse_int(i, width) || !parse_int(i, height)) {
                std::cerr << "--size expects width and height\n";
                return 1;
            }
        } else if (arg == "--eye" || arg == "--center" || arg == "--up" || arg == "--light") {
            vec3 &value = arg == "--eye" ? eye : arg == "--center" ? center :
                          arg == "--up" ? up : light;
            if (!parse_vec3(i, value)) {
                std::cerr << arg << " expects three numbers\n";
                return 1;
            }
        } else if (arg == "--fov" || arg == "--near" || arg == "--far") {
            double &value = arg == "--fov" ? fov : arg == "--near" ? near_plane : far_plane;
            if (!parse_double(i, value)) {
                std::cerr << arg << " expects a number\n";
                return 1;
            }
        } else if (arg == "--translate" || arg == "--rotate" || arg == "--scale") {
            if (instances.size() <= file_instance_count) {
                std::cerr << arg << " requires a preceding OBJ path\n";
                return 1;
            }
            vec3 &value = arg == "--translate" ? instances.back().translation :
                          arg == "--rotate" ? instances.back().rotation : instances.back().scale;
            if (!parse_vec3(i, value)) {
                std::cerr << arg << " expects three numbers\n";
                return 1;
            }
        } else if (arg == "--help") {
            usage();
            return 0;
        } else if (arg.starts_with("--")) {
            std::cerr << "Unknown option: " << arg << '\n';
            usage();
            return 1;
        } else {
            instances.push_back({.path = arg});
        }
    }
    if (instances.empty()) {
        usage();
        return 1;
    }
    if (width < 1 || height < 1 || width > 4096 || height > 4096 ||
        fov <= 0 || fov >= 180 || near_plane <= 0 || far_plane <= near_plane ||
        norm(eye-center) < 1e-6 || norm(cross(up, eye-center)) < 1e-6 || norm(light) < 1e-6 ||
        (msaa_samples != 1 && msaa_samples != 4)) {
        std::cerr << "Invalid camera, light, or image settings\n";
        return 1;
    }
    for (SceneInstance &instance : instances) {
        if (instance.scale.x <= 1e-6 || instance.scale.y <= 1e-6 || instance.scale.z <= 1e-6) {
            std::cerr << "--scale components must be greater than 1e-6\n";
            return 1;
        }
        instance.model = make_model_matrix(instance);
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();

    constexpr int shadow_width = 1024, shadow_height = 1024;

    std::vector<Model> models;
    models.reserve(instances.size());
    std::unordered_map<std::string, int> mesh_by_path;
    for (SceneInstance &instance : instances) {
        const auto [entry, inserted] = mesh_by_path.try_emplace(instance.path, static_cast<int>(models.size()));
        if (inserted) models.emplace_back(instance.path);
        instance.mesh = entry->second;
    }
    int vertex_count = 0;
    int face_count = 0;
    for (const SceneInstance &instance : instances) {
        const Model &model = models[instance.mesh];
        vertex_count += model.nverts();
        face_count += model.nfaces();
    }
    if (!vertex_count) return 1;
    const auto loaded = Clock::now();

    // Pass 1: fit an orthographic light projection around all model vertices.
    const vec3 light_up = std::abs(normalized(light).y) > .99 ? vec3{0, 0, 1} : vec3{0, 1, 0};
    LightView = make_lookat(center + normalized(light), center, light_up);
    LightProjection = fit_orthographic(models, instances, LightView);
    mat<4,4> light_viewport = {{{shadow_width/2.,0,0,shadow_width/2.},
                               {0,shadow_height/2.,0,shadow_height/2.},
                               {0,0,1,0}, {0,0,0,1}}};
    init_sbuffer(shadow_width, shadow_height);
    for (const SceneInstance &instance : instances) {
        const Model &model = models[instance.mesh];
        BlankShader shader(model, LightProjection * LightView, instance.model, texture_filter);
        for (int f=0; f<model.nfaces(); f++) {
            Triangle clip = {shader.vertex(f, 0), shader.vertex(f, 1), shader.vertex(f, 2)};
            rasterize_shadow(clip, shader, light_viewport, shadow_width, shadow_height, sbuffer);
        }
    }
    const auto shadow_done = Clock::now();

    // Pass 2: render from the camera, testing each fragment against sbuffer.
    lookat(eye, center, up);                                   // build the ModelView   matrix
    const int viewport_x = width/16, viewport_y = height/16;
    const int viewport_width = width-2*viewport_x, viewport_height = height-2*viewport_y;
    init_perspective(norm(eye-center), fov, static_cast<double>(viewport_width)/viewport_height,
                     near_plane, far_plane);                    // build the Perspective matrix
    init_viewport(viewport_x, viewport_y, viewport_width, viewport_height);

    TGAImage framebuffer(width, height, TGAImage::RGB);
    RenderTarget target(width, height, msaa_samples);
    std::vector<DrawCommand> commands;
    commands.reserve(face_count);
    for (const SceneInstance &instance : instances) {
        const Model &model = models[instance.mesh];
        PhongShader shader(light, model, instance.model, eye, center,
                           light_viewport, shadow_width, shadow_height, texture_filter);
        for (int f=0; f<model.nfaces(); f++) {
            Triangle clip = {shader.vertex(f, 0), shader.vertex(f, 1), shader.vertex(f, 2)};
            commands.push_back({shader, {clip[0], clip[1], clip[2]}});
        }
    }

    // Bin triangles once, preserving draw order within each band.
    constexpr int band_height = 64;
    const int band_count = (height+band_height-1)/band_height;
    std::vector<std::vector<int>> band_commands(band_count);
    for (size_t i=0; i<commands.size(); i++) {
        double min_y = height, max_y = -1.;
        bool crosses_camera = false;
        for (const vec4 &vertex : commands[i].clip) {
            if (vertex.w <= 1e-6) {
                crosses_camera = true;
                break;
            }
            double y = (Viewport * (vertex/vertex.w)).y;
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
        if (crosses_camera) {
            for (auto &band : band_commands) band.push_back(static_cast<int>(i));
            continue;
        }
        if (max_y < -.25 || min_y > height-1+.25) continue;
        int first = std::clamp(static_cast<int>(std::floor(std::max(0., min_y-.25)/band_height)), 0, band_count-1);
        int last = std::clamp(static_cast<int>(std::floor(std::min<double>(height-1, max_y+.25)/band_height)), 0, band_count-1);
        for (int band=first; band<=last; band++) band_commands[band].push_back(static_cast<int>(i));
    }

    // Each horizontal band owns its samples, so triangles can run in parallel without depth-buffer locks.
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int band=0; band<band_count; band++) {
        int y_begin = band*band_height, y_end = std::min(height, y_begin+band_height);
        for (RenderPass pass : {RenderPass::Opaque, RenderPass::Transparent}) {
            for (int index : band_commands[band]) {
                const DrawCommand &command = commands[index];
                rasterize(command.clip, command.shader, target, pass, y_begin, y_end);
            }
        }
    }

    blend_transparency(target);
    TGAImage ambient_image(width, height, TGAImage::RGB);
    target.resolve(framebuffer, zbuffer, &ambient_image);
    const auto camera_done = Clock::now();

    // Estimate view-space occlusion from opaque depth, then modulate ambient light only.
    constexpr double ao_radius = .4;
    constexpr int ao_samples = 32;
    const std::vector<double> visibility = compute_ssao(zbuffer, width, height, Perspective,
                                                         Viewport, ao_radius, ao_samples);
    TGAImage ao_image(width, height, TGAImage::GRAYSCALE);
#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const int index = x+y*width;
            const bool has_opaque_depth = zbuffer[index] > -999.;
            const double factor = has_opaque_depth ? visibility[index] : 1.;
            if (has_opaque_depth) {
                const std::uint8_t gray = static_cast<std::uint8_t>(factor*255.);
                ao_image.set(x, y, {gray, gray, gray, 255});
            }
            TGAColor color = framebuffer.get(x, y);
            const TGAColor ambient = ambient_image.get(x, y);
            for (int channel : {0,1,2})
                color[channel] = static_cast<std::uint8_t>(std::min(255.,
                    color[channel] + ambient[channel]*factor));
            framebuffer.set(x, y, color);
        }
    }
    ao_image.write_tga_file("ao.tga");
    framebuffer.write_tga_file("framebuffer.tga");
    if (timings) {
        const auto finished = Clock::now();
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
        std::cerr << "load " << ms(start, loaded) << " ms, shadow " << ms(loaded, shadow_done)
                  << " ms, camera " << ms(shadow_done, camera_done)
                  << " ms, SSAO + write " << ms(camera_done, finished) << " ms\n";
    }
    return 0;
}
