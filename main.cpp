#include "our_gl.h"
#include "model.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

extern mat<4,4> ModelView, Perspective, Viewport, LightView, LightProjection;
extern std::vector<double> zbuffer, sbuffer;

mat<4,4> fit_orthographic(const std::vector<Model> &models, const mat<4,4> &view) {
    double low[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    double high[3] = {-low[0], -low[1], -low[2]};
    for (const Model &model : models) {
        for (int i=0; i<model.nverts(); i++) {
            vec4 p = view * model.vert(i);
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
    BlankShader(const Model &m, const mat<4,4> &matrix): model(m), transform(matrix) {}
    vec2 varying_uv[3];

    virtual vec4 vertex(const int face, const int vert) {
        varying_uv[vert] = model.uv(face, vert);
        return transform * model.vert(face, vert);
    }

    virtual std::pair<bool, TGAColor> fragment(const vec3 bar) const {
        vec2 uv = varying_uv[0]*bar[0] + varying_uv[1]*bar[1] + varying_uv[2]*bar[2];
        // Only fully opaque texels cast shadows in this first version.
        return {false, {255, 255, 255, static_cast<std::uint8_t>(model.diffuse(uv).w*255.)}};
    }
};


struct PhongShader : IShader {
    const Model &model;
    vec4 l;              // light direction in eye coordinates
    mat<4,4> normal_matrix;
    vec2 varying_uv[3];  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    vec4 varying_nrm[3]; // vertex normals used when the model has no normal map
    vec3 eye;
    vec3 center;
    vec4 camera_pos;     //ModelView 下的camera_pos;
    vec4 tri[3];         // triangle in view coordinates
    vec4 varying_world[3];
    mat<4,4> shadow_matrix; // world coordinates -> shadow-map pixels
    int shadow_width, shadow_height;

    PhongShader(const vec3 light, const Model &m, const vec3 &eye_, const vec3 &center_,
                const mat<4,4> &light_viewport, int sw, int sh)
        : model(m), normal_matrix(ModelView.invert_transpose()), eye(eye_), center(center_),
          shadow_matrix(light_viewport * LightProjection * LightView), shadow_width(sw), shadow_height(sh) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.})); // transform the light vector to view coordinates
        camera_pos = ModelView * vec4{eye.x, eye.y, eye.z, 1.};

    }

    virtual vec4 vertex(const int face, const int vert) {
        varying_uv[vert] = model.uv(face, vert);
        varying_nrm[vert] = normal_matrix * model.normal(face, vert);
        varying_world[vert] = model.vert(face, vert);
        vec4 gl_Position = ModelView * varying_world[vert];
        tri[vert] = gl_Position;
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        TGAColor gl_FragColor = {255, 255, 255, 255};             // output color of the fragment
        vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
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
                    vec3 map_n = model.normal(uv).xyz();
                    normal = normalized(tangent*map_n.x + b*map_n.y + base_n*map_n.z);
                }
            }
        }
        vec4 n = {normal.x, normal.y, normal.z, 0};

        vec4 r = normalized(n * (n * l)*2 - l);                   // reflected light direction

        vec4 pos = bar[0] * tri[0] + bar[1] * tri[1] + bar[2] * tri[2];
        vec4 v = normalized(camera_pos - pos);

        double ambient = .4;                                      // ambient light intensity
        vec4 basecolor = model.diffuse(uv);                        //归一化后的漫反射基础颜色

        double diff = std::max(0., n * l);

        // diffuse light intensity
        double spec = 2. * model.specular(uv) * std::pow(std::max(r * v, 0.), 35); // specular map controls highlight strength

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

        for (int channel : {0,1,2}) {
            gl_FragColor[channel] *= std::min(1., basecolor[2 - channel]*(ambient + diff) + spec);
        }
        gl_FragColor[3] = static_cast<std::uint8_t>(basecolor.w * 255.);
        return {false, gl_FragColor};                             // do not discard the pixel
    }
};

struct DrawCommand {
    PhongShader shader;
    Triangle clip;
};

int main(int argc, char** argv) {
    int msaa_samples = 1;
    bool timings = false;
    std::vector<std::string> paths;
    for (int i=1; i<argc; i++) {
        if (std::string(argv[i]) == "--msaa") {
            if (++i >= argc || (std::string(argv[i]) != "1" && std::string(argv[i]) != "4")) {
                std::cerr << "--msaa expects 1 or 4\n";
                return 1;
            }
            msaa_samples = argv[i][0]-'0';
        } else if (std::string(argv[i]) == "--timings") {
            timings = true;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::cerr << "Usage: " << argv[0] << " [--msaa 1|4] [--timings] obj/model.obj [obj/other.obj ...]" << std::endl;
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3  light{ 1, 1, 1}; // direction toward the light
    constexpr vec3    eye{-1, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector
    constexpr int shadow_width = 1024, shadow_height = 1024;

    std::vector<Model> models;
    models.reserve(paths.size());
    for (const std::string &path : paths) models.emplace_back(path);
    int vertex_count = 0;
    int face_count = 0;
    for (const Model &model : models) {
        vertex_count += model.nverts();
        face_count += model.nfaces();
    }
    if (!vertex_count) return 1;
    const auto loaded = Clock::now();

    // Pass 1: fit an orthographic light projection around all model vertices.
    LightView = make_lookat(center + normalized(light), center, up);
    LightProjection = fit_orthographic(models, LightView);
    mat<4,4> light_viewport = {{{shadow_width/2.,0,0,shadow_width/2.},
                               {0,shadow_height/2.,0,shadow_height/2.},
                               {0,0,1,0}, {0,0,0,1}}};
    init_sbuffer(shadow_width, shadow_height);
    for (const Model &model : models) {
        BlankShader shader(model, LightProjection * LightView);
        for (int f=0; f<model.nfaces(); f++) {
            Triangle clip = {shader.vertex(f, 0), shader.vertex(f, 1), shader.vertex(f, 2)};
            rasterize_shadow(clip, shader, light_viewport, shadow_width, shadow_height, sbuffer);
        }
    }
    const auto shadow_done = Clock::now();

    // Pass 2: render from the camera, testing each fragment against sbuffer.
    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix

    TGAImage framebuffer(width, height, TGAImage::RGB);
    RenderTarget target(width, height, msaa_samples);
    std::vector<DrawCommand> commands;
    commands.reserve(face_count);
    for (const Model &model : models) {
        PhongShader shader(light, model, eye, center, light_viewport, shadow_width, shadow_height);
        for (int f=0; f<model.nfaces(); f++) {
            Triangle clip = {shader.vertex(f, 0), shader.vertex(f, 1), shader.vertex(f, 2)};
            commands.push_back({shader, {clip[0], clip[1], clip[2]}});
        }
    }

    // Bin triangles once, preserving draw order within each band.
    constexpr int band_height = 64;
    constexpr int band_count = (height+band_height-1)/band_height;
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
    target.resolve(framebuffer, zbuffer);
    const auto camera_done = Clock::now();

    // Screen-space ambient occlusion from the camera depth buffer.
    constexpr double ao_radius = .1;
    constexpr int ao_samples = 128;
    const double ao_radius_x = Viewport[0][0]*ao_radius;
    const double ao_radius_y = Viewport[1][1]*ao_radius;
    TGAImage ao_image(width, height, TGAImage::GRAYSCALE);
#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const double z = zbuffer[x+y*width];
            if (z <= -999.) continue;
            std::uint32_t seed = 42u + static_cast<std::uint32_t>(x+y*width);
            seed ^= seed >> 16;
            seed *= 0x7feb352du;
            seed ^= seed >> 15;
            seed *= 0x846ca68bu;
            seed ^= seed >> 16;
            std::minstd_rand rng(seed);
            std::uniform_real_distribution<double> offset(-1., 1.);
            int occluded = 0, valid = 0;
            for (int i=0; i<ao_samples; i++) {
                double px = x + ao_radius_x*offset(rng);
                double py = y + ao_radius_y*offset(rng);
                double pz = z + ao_radius*offset(rng);
                if (px < 0 || px >= width || py < 0 || py >= height) continue;
                const double d = zbuffer[static_cast<int>(px) + static_cast<int>(py)*width];
                if (z + 5.*ao_radius < d) continue; // avoid halos at depth discontinuities
                valid++;
                occluded += d > pz;
            }
            const double visibility = valid ? 1. - .4*occluded/valid : 1.;
            const double factor = visibility*visibility*(3. - 2.*visibility); // smoothstep(0, 1, visibility)
            const std::uint8_t gray = static_cast<std::uint8_t>(factor*255.);
            ao_image.set(x, y, {gray, gray, gray, 255});
            TGAColor color = framebuffer.get(x, y);
            for (int channel : {0,1,2}) color[channel] = static_cast<std::uint8_t>(color[channel]*factor);
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
