#include <algorithm>
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

void init_perspective(const double f) {
    Perspective = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0, -1/f,1}}};
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

void rasterize(const Triangle &clip, const IShader &shader, TGAImage &framebuffer, RenderPass pass, TransparentBuffer &transparent) {
    struct ClipVertex {
        vec4 position;
        vec3 weights; // barycentric coordinates in the original triangle
    };
    constexpr double min_w = 1e-6;
    std::vector<ClipVertex> polygon = {
        {clip[0], {1, 0, 0}}, {clip[1], {0, 1, 0}}, {clip[2], {0, 0, 1}}
    };
    std::vector<ClipVertex> clipped;
    for (int i=0; i<3; i++) {
        const ClipVertex &a = polygon[i], &b = polygon[(i+1)%3];
        bool inside_a = a.position.w >= min_w;
        bool inside_b = b.position.w >= min_w;
        if (inside_a) clipped.push_back(a);
        if (inside_a != inside_b) {
            double t = (min_w-a.position.w)/(b.position.w-a.position.w);
            clipped.push_back({a.position + (b.position-a.position)*t,
                               a.weights + (b.weights-a.weights)*t});
        }
    }
    if (clipped.size() < 3) return;

    for (size_t i=1; i+1<clipped.size(); i++) {
        const ClipVertex tri[3] = {clipped[0], clipped[i], clipped[i+1]};
        vec4 ndc[3] = {tri[0].position/tri[0].position.w,
                       tri[1].position/tri[1].position.w,
                       tri[2].position/tri[2].position.w};
        vec2 screen[3] = {(Viewport*ndc[0]).xy(), (Viewport*ndc[1]).xy(), (Viewport*ndc[2]).xy()};

        mat<3,3> ABC = {{{screen[0].x, screen[0].y, 1.},
                         {screen[1].x, screen[1].y, 1.},
                         {screen[2].x, screen[2].y, 1.}}};
        if (ABC.det() < 1) continue; // backface culling and subpixel triangles
        mat<3,3> barycentric = ABC.invert_transpose();

        auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x});
        auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y});
        if (bbmaxx < 0 || bbmaxy < 0 || bbminx >= framebuffer.width() || bbminy >= framebuffer.height()) continue;
#pragma omp parallel for
        for (int x=static_cast<int>(std::max(0., std::ceil(bbminx)));
             x<=static_cast<int>(std::min<double>(bbmaxx, framebuffer.width()-1)); x++) {
            for (int y=static_cast<int>(std::max(0., std::ceil(bbminy)));
                 y<=static_cast<int>(std::min<double>(bbmaxy, framebuffer.height()-1)); y++) {
                vec3 bc = barycentric * vec3{static_cast<double>(x), static_cast<double>(y), 1.};
                if (bc.x<0 || bc.y<0 || bc.z<0) continue;
                double z = bc * vec3{ndc[0].z, ndc[1].z, ndc[2].z};
                if (z <= zbuffer[x+y*framebuffer.width()]) continue;
                vec3 corrected = {bc[0]/tri[0].position.w, bc[1]/tri[1].position.w, bc[2]/tri[2].position.w};
                corrected = corrected / (corrected.x + corrected.y + corrected.z);
                vec3 original = tri[0].weights*corrected[0] + tri[1].weights*corrected[1] + tri[2].weights*corrected[2];
                auto [discard, color] = shader.fragment(original);
                if (discard) continue;
                if (pass == RenderPass::Opaque) {
                    if (color[3] != 255) continue;
                    zbuffer[x+y*framebuffer.width()] = z;
                    framebuffer.set(x, y, color);
                } else {
                    if (color[3] == 0 || color[3] == 255) continue;
                    transparent[x+y*framebuffer.width()].push_back({z, color});
                }
            }
        }
    }
}

void blend_transparency(TransparentBuffer &transparent, TGAImage &framebuffer) {
    for (int y=0; y<framebuffer.height(); y++) {
        for (int x=0; x<framebuffer.width(); x++) {
            auto &fragments = transparent[x+y*framebuffer.width()];
            if (fragments.empty()) continue;
            std::sort(fragments.begin(), fragments.end(),
                      [](const auto &a, const auto &b) { return a.z < b.z; });
            TGAColor background = framebuffer.get(x, y);
            for (const auto &fragment : fragments) {
                int alpha = fragment.color[3];
                for (int channel : {0,1,2})
                    background[channel] = (fragment.color[channel]*alpha + background[channel]*(255-alpha)) / 255;
            }
            framebuffer.set(x, y, background);
        }
    }
}
