#include <algorithm>
#include <numbers>
#include "our_gl.h"

mat<4,4> ModelView, Viewport, Perspective; // "OpenGL" state matrices
std::vector<double> zbuffer;               // depth buffer

mat<4, 4> LightView, LightProjection;
std::vector<double> sbuffer;         //shadowMapDepth buffer

mat<4,4> make_lookat(const vec3 eye, const vec3 center, const vec3 up) {
    vec3 n = normalized(eye-center);
    vec3 l = normalized(cross(up,n));
    vec3 m = normalized(cross(n, l));
    return mat<4,4>{{{l.x,l.y,l.z,0}, {m.x,m.y,m.z,0}, {n.x,n.y,n.z,0}, {0,0,0,1}}} *
           mat<4,4>{{{1,0,0,-center.x}, {0,1,0,-center.y}, {0,0,1,-center.z}, {0,0,0,1}}};
}

void lookat(const vec3 eye, const vec3 center, const vec3 up) {
    ModelView = make_lookat(eye, center, up);
}

void init_perspective(const double camera_distance, const double fov_degrees, const double aspect,
                      const double near_plane, const double far_plane) {
    // lookat keeps the target at z=0 and the camera at z=camera_distance.
    // Map camera-space depths near/far to NDC +1/-1; larger depth wins the z-test.
    const double scale = 1./std::tan(fov_degrees*std::numbers::pi/360.);
    const double depth_scale = (near_plane+far_plane)/(far_plane-near_plane);
    Perspective = {{{scale/aspect,0,0,0}, {0,scale,0,0},
                    {0,0,depth_scale,2*near_plane*far_plane/(far_plane-near_plane)-depth_scale*camera_distance},
                    {0,0,-1,camera_distance}}};
}

void init_viewport(const int x, const int y, const int w, const int h) {
    Viewport = {{{w/2., 0, 0, x+w/2.}, {0, h/2., 0, y+h/2.}, {0,0,1,0}, {0,0,0,1}}};
}

void init_zbuffer(const int width, const int height) {
    zbuffer = std::vector(width*height, -1000.);
}

void init_sbuffer(const int width, const int height) {
    sbuffer = std::vector(width*height, -1000.);
}

void rasterize_shadow(const Triangle &clip, const IShader &shader, const mat<4,4> &light_viewport,
                      const int width, const int height, std::vector<double> &depth_buffer) {
    vec4 projected[3] = {light_viewport*clip[0], light_viewport*clip[1], light_viewport*clip[2]};
    mat<3,3> ABC = {{{projected[0].x, projected[0].y, 1.},
                     {projected[1].x, projected[1].y, 1.},
                     {projected[2].x, projected[2].y, 1.}}};
    if (std::abs(ABC.det()) < 1.) return;
    mat<3,3> barycentric = ABC.invert_transpose();
    auto [bbminx,bbmaxx] = std::minmax({projected[0].x, projected[1].x, projected[2].x});
    auto [bbminy,bbmaxy] = std::minmax({projected[0].y, projected[1].y, projected[2].y});
    if (bbmaxx < 0 || bbmaxy < 0 || bbminx >= width || bbminy >= height) return;
    for (int x=static_cast<int>(std::max(0., std::ceil(bbminx)));
         x<=static_cast<int>(std::min<double>(bbmaxx, width-1)); x++) {
        for (int y=static_cast<int>(std::max(0., std::ceil(bbminy)));
             y<=static_cast<int>(std::min<double>(bbmaxy, height-1)); y++) {
            vec3 bar = barycentric * vec3{static_cast<double>(x), static_cast<double>(y), 1.};
            if (bar.x<0 || bar.y<0 || bar.z<0) continue;
            double z = bar * vec3{projected[0].z, projected[1].z, projected[2].z};
            double &depth = depth_buffer[x+y*width];
            if (z <= depth) continue;
            auto [discard, color] = shader.fragment(bar);
            if (!discard && color[3] == 255) depth = z;
        }
    }
}

RenderTarget::RenderTarget(int w, int h, int count)
    : width(w), height(h), samples(count), depth(w*h*count, -1000.),
      color(w*h*count, TGAColor{0, 0, 0, 255}), transparent(w*h*count) {}

void RenderTarget::resolve(TGAImage &framebuffer, std::vector<double> &pixel_depth) const {
    pixel_depth.assign(width*height, -1000.);
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const int pixel = x+y*width;
            TGAColor resolved = {0, 0, 0, 255};
            for (int channel : {0,1,2}) {
                int total = 0;
                for (int sample=0; sample<samples; sample++)
                    total += color[pixel*samples+sample][channel];
                resolved[channel] = static_cast<std::uint8_t>((total+samples/2)/samples);
            }
            for (int sample=0; sample<samples; sample++)
                pixel_depth[pixel] = std::max(pixel_depth[pixel], depth[pixel*samples+sample]);
            framebuffer.set(x, y, resolved);
        }
    }
}

void rasterize(const Triangle &clip, const IShader &shader, RenderTarget &target, RenderPass pass,
               int y_begin, int y_end) {
    struct ClipVertex {
        vec4 position;
        vec3 weights; // barycentric coordinates in the original triangle
    };
    constexpr double min_w = 1e-6;
    std::vector<ClipVertex> polygon = {
        {clip[0], {1, 0, 0}}, {clip[1], {0, 1, 0}}, {clip[2], {0, 0, 1}}
    };
    std::vector<ClipVertex> clipped;
    auto clip_against = [&](const auto &distance) {
        clipped.clear();
        for (size_t i=0; i<polygon.size(); i++) {
            const ClipVertex &a = polygon[i], &b = polygon[(i+1)%polygon.size()];
            const double da = distance(a.position), db = distance(b.position);
            const bool inside_a = da >= 0, inside_b = db >= 0;
            if (inside_a) clipped.push_back(a);
            if (inside_a != inside_b) {
                const double t = da/(da-db);
                clipped.push_back({a.position + (b.position-a.position)*t,
                                   a.weights + (b.weights-a.weights)*t});
            }
        }
        polygon.swap(clipped);
    };
    // Guard the perspective divide, then clip -w <= x,y,z <= w.
    clip_against([](const vec4 &p) { return p.w-min_w; });
    if (polygon.size() < 3) return;
    for (int axis=0; axis<3; axis++) {
        clip_against([axis](const vec4 &p) { return p.w+p[axis]; });
        if (polygon.size() < 3) return;
        clip_against([axis](const vec4 &p) { return p.w-p[axis]; });
        if (polygon.size() < 3) return;
    }

    for (size_t i=1; i+1<polygon.size(); i++) {
        const ClipVertex tri[3] = {polygon[0], polygon[i], polygon[i+1]};
        vec4 ndc[3] = {tri[0].position/tri[0].position.w,
                       tri[1].position/tri[1].position.w,
                       tri[2].position/tri[2].position.w};
        vec2 screen[3] = {(Viewport*ndc[0]).xy(), (Viewport*ndc[1]).xy(), (Viewport*ndc[2]).xy()};

        auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x});
        auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y});
        const double extent = target.samples == 4 ? .25 : 0.;
        if (bbmaxx+extent < 0 || bbmaxy+extent < y_begin ||
            bbminx-extent >= target.width || bbminy-extent >= y_end) continue;

        mat<3,3> ABC = {{{screen[0].x, screen[0].y, 1.},
                         {screen[1].x, screen[1].y, 1.},
                         {screen[2].x, screen[2].y, 1.}}};
        double area = ABC.det();
        if (area < (target.samples == 1 ? 1. : 1e-12)) continue; // back-face culling
        mat<3,3> barycentric = ABC.invert_transpose();

        const int x0 = static_cast<int>(std::max(0., std::ceil(bbminx-extent)));
        const int x1 = static_cast<int>(std::min<double>(target.width-1, std::floor(bbmaxx+extent)));
        const int y0 = static_cast<int>(std::max<double>(y_begin, std::ceil(bbminy-extent)));
        const int y1 = static_cast<int>(std::min<double>(y_end-1, std::floor(bbmaxy+extent)));
        constexpr vec2 offsets[4] = {{-.25,-.25}, {.25,-.25}, {-.25,.25}, {.25,.25}};
        for (int y=y0; y<=y1; y++) {
            for (int x=x0; x<=x1; x++) {
                bool visible[4] = {};
                double depths[4] = {};
                vec3 shade_bar = {};
                int visible_count = 0;
                for (int sample=0; sample<target.samples; sample++) {
                    const vec2 offset = target.samples == 1 ? vec2{0,0} : offsets[sample];
                    vec3 bc = barycentric * vec3{x+offset.x, y+offset.y, 1.};
                    if (bc.x<0 || bc.y<0 || bc.z<0) continue;
                    double z = bc * vec3{ndc[0].z, ndc[1].z, ndc[2].z};
                    const int index = (x+y*target.width)*target.samples+sample;
                    if (z <= target.depth[index]) continue;
                    visible[sample] = true;
                    depths[sample] = z;
                    if (visible_count++ == 0) {
                        vec3 corrected = {bc[0]/tri[0].position.w, bc[1]/tri[1].position.w, bc[2]/tri[2].position.w};
                        corrected = corrected / (corrected.x + corrected.y + corrected.z);
                        shade_bar = tri[0].weights*corrected[0] + tri[1].weights*corrected[1] + tri[2].weights*corrected[2];
                    }
                }
                if (!visible_count) continue;
                auto [discard, frag_color] = shader.fragment(shade_bar);
                if (discard) continue;
                if (pass == RenderPass::Opaque && frag_color[3] != 255) continue;
                if (pass == RenderPass::Transparent && (frag_color[3] == 0 || frag_color[3] == 255)) continue;
                for (int sample=0; sample<target.samples; sample++) {
                    if (!visible[sample]) continue;
                    const int index = (x+y*target.width)*target.samples+sample;
                    if (pass == RenderPass::Opaque) {
                        target.depth[index] = depths[sample];
                        target.color[index] = frag_color;
                    } else {
                        target.transparent[index].push_back({depths[sample], frag_color});
                    }
                }
            }
        }
    }
}

void blend_transparency(RenderTarget &target) {
    for (size_t index=0; index<target.transparent.size(); index++) {
        auto &fragments = target.transparent[index];
        if (fragments.empty()) continue;
        std::sort(fragments.begin(), fragments.end(),
                  [](const auto &a, const auto &b) { return a.z < b.z; });
        TGAColor &background = target.color[index];
        for (const auto &fragment : fragments) {
            int alpha = fragment.color[3];
            for (int channel : {0,1,2})
                background[channel] = (fragment.color[channel]*alpha + background[channel]*(255-alpha)) / 255;
        }
    }
}
