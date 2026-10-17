#include <cstdlib>

#include "our_gl.h"
#include "model.h"
#include "geometry.h"

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer


constexpr double Ka = 0.1;      //环境光系数
constexpr double Kd = 0.7;      //漫反射系数
constexpr double Ks = 0.2;       //高光系数
constexpr int    p = 32;               //高光指数
constexpr vec3 Ia = {1., 0., 0.};   //环境光
constexpr vec3 Il = {1., 0., 0.};   //光源
constexpr vec3 lightpos = {1, 2, 2};



struct RandomShader : IShader {
    const Model &model;
    TGAColor color = {};
    vec3 tri[3];  // triangle in eye coordinates

    RandomShader(const Model &m) : model(m) {
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {

        return {false, color};                                    // do not discard the pixel
    }
};

struct PhongShader : IShader {
    const Model &model;
    TGAColor color = {};
    vec3 eye, center;
    vec3 eye_camera;
    vec3 tri[3];  // triangle in eye coordinates
    vec3 v;       //from fragment to view point
    vec3 n;       //表面法向量
    vec3 lightpos_camera;
    

    PhongShader(const Model &m, const vec3& eye_, const vec3& center_) : model(m), eye(eye_), center(center_) {
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates

        if (vert == 2) {
            vec3 ab_ = {tri[1].x-tri[0].x, tri[1].y-tri[0].y,tri[1].z-tri[0].z};
            vec3 ac_ = {tri[2].x-tri[0].x, tri[2].y-tri[0].y,tri[2].z-tri[0].z};

            
            n = normalized(cross(ab_, ac_));

            lightpos_camera = (ModelView * vec4{lightpos.x, lightpos.y, lightpos.z, 1.}).xyz();
            eye_camera = (ModelView * vec4{eye.x, eye.y, eye.z, 1.}).xyz();
        }

        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        vec3 pos = bar[0]*tri[0] + bar[1]*tri[1] + bar[2]*tri[2];
        vec3 l = normalized(lightpos_camera - pos);
        vec3 v = normalized(eye_camera - pos);
        vec3 r = 2. * (n * l) * n - l;

        vec3 ambient = Ka * Ia;
        vec3 diffuse = Kd * Il * (std::max(0., n * l));
        vec3 specular = Ks * Il * pow((std::max(0., r * v)), p);
        
        vec3 c = (ambient + diffuse + specular) * 255.;

        TGAColor result = {static_cast<uint8_t>(c[2]), static_cast<uint8_t>(c[1]),static_cast<uint8_t>(c[0])};

        return {false, result};                                    // do not discard the pixel
    }
};



int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3    eye{-1, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector



    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB, {177, 195, 209, 255});

    for (int m=1; m<argc; m++) {                    // iterate through all input objects
        Model model(argv[m]);                       // load the data
        PhongShader shader(model, eye, center);
        for (int f=0; f<model.nfaces(); f++) {      // iterate through all facets
            // shader.color = { static_cast<uint8_t>(std::rand()%255), static_cast<uint8_t>(std::rand()%255), static_cast<uint8_t>(std::rand()%255) };
            Triangle clip = { shader.vertex(f, 0),  // assemble the primitive
                              shader.vertex(f, 1),
                              shader.vertex(f, 2) };
            rasterize(clip, shader, framebuffer);   // rasterize the primitive
        }
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}