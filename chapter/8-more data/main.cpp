#include "our_gl.h"
#include "model.h"

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer

struct PhongShader : IShader {
    const Model &model;
    vec4 l;              // light direction in eye coordinates
    mat<4,4> normal_matrix;
    vec2 varying_uv[3];  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    vec4 varying_nrm[3]; // vertex normals used when the model has no normal map
    vec3 eye;
    vec3 center;
    vec4 varying_pos[3];    //ModelView 下的观察面片
    vec4 camera_pos;     //ModelView 下的camera_pos;

    PhongShader(const vec3 light, const Model &m, const vec3 &eye_, const vec3 &center_) : model(m), normal_matrix(ModelView.invert_transpose()), eye(eye_), center(center_) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.})); // transform the light vector to view coordinates
        camera_pos = ModelView * vec4{eye.x, eye.y, eye.z, 1.};

    }

    virtual vec4 vertex(const int face, const int vert) {
        varying_uv[vert] = model.uv(face, vert);
        if (!model.has_normalmap()) varying_nrm[vert] = normal_matrix * model.normal(face, vert);
        vec4 gl_Position = ModelView * model.vert(face, vert);
        varying_pos[vert] = gl_Position;
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        TGAColor gl_FragColor = {255, 255, 255, 255};             // output color of the fragment
        vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];

        vec4 n = model.has_normalmap()
            ? normalized(normal_matrix * model.normal(uv))
            : normalized(varying_nrm[0] * bar[0] + varying_nrm[1] * bar[1] + varying_nrm[2] * bar[2]);

        vec4 r = normalized(n * (n * l)*2 - l);                   // reflected light direction

        vec4 pos = bar[0] * varying_pos[0] + bar[1] * varying_pos[1] + bar[2] * varying_pos[2];
        vec4 v = normalized(camera_pos - pos);

        double ambient = .4;                                      // ambient light intensity
        vec4 basecolor = model.diffuse(uv);                        //归一化后的漫反射基础颜色

        double diff = std::max(0., n * l);

        // diffuse light intensity
        double spec = 2. * model.specular(uv) * std::pow(std::max(r * v, 0.), 35); // specular map controls highlight strength

        for (int channel : {0,1,2}) {
            gl_FragColor[channel] *= std::min(1., basecolor[2 - channel]*(ambient + diff) + spec);
        }
        gl_FragColor[3] = static_cast<std::uint8_t>(basecolor.w * 255.);
        return {false, gl_FragColor};                             // do not discard the pixel
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3  light{ 1, 1, 1}; // light source
    constexpr vec3    eye{-1, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector

    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB);
    TransparentBuffer transparent(width*height);

    std::vector<Model> models;
    models.reserve(argc-1);
    for (int m=1; m<argc; m++) models.emplace_back(argv[m]);

    for (RenderPass pass : {RenderPass::Opaque, RenderPass::Transparent}) {
        for (const Model &model : models) {
            PhongShader shader(light, model, eye, center);
            for (int f=0; f<model.nfaces(); f++) {
                Triangle clip = { shader.vertex(f, 0),
                                  shader.vertex(f, 1),
                                  shader.vertex(f, 2) };
                rasterize(clip, shader, framebuffer, pass, transparent);
            }
        }
    }

    blend_transparency(transparent, framebuffer);
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}
