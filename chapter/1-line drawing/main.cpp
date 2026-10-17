#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <array>
#include "tgaimage.h"
#include "model.h"

constexpr int width  = 800;
constexpr int height = 800;

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255, 128,  64, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

void line(int x0, int y0, int x1, int y1, TGAImage &framebuffer, TGAColor color)
{
    bool steep = std::abs(x0 - x1) < std::abs(y0 - y1);    //是否陡峭
    if(steep){
        std::swap(x0, y0);
        std::swap(x1, y1);
    } 
    if(x0 > x1){
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    // float yplus = (y1 - y0) / static_cast<float>(x1-x0);
    int y = y0;
    int ierror = 0;
    for(int x=x0; x<=x1; x++){
        // float t = (x-x0) / static_cast<float>(x1-x0);
        // int y = std::round(y0 + (y1-y0)*t);
        if(steep){
            framebuffer.set(y, x, color);
        }
        else{
            framebuffer.set(x, y, color);
        }
        ierror += 2 * std::abs(y1-y0);
        y += (y1 > y0 ? 1 : -1) * (ierror > x1 - x0);
        ierror -= 2 * (x1-x0)   * (ierror > x1 - x0);
    }
}

int main(int argc, char** argv) {

    TGAImage framebuffer(width, height, TGAImage::RGB);

    Model model("obj/diablo3_pose/diablo3_pose.obj");

    std::cout<<"Vertices: "
             <<model.nverts()
             <<std::endl;
    
    std::cout<<"Faces: "
             <<model.nfaces()
             <<std::endl;

    int nf = model.nfaces();
    int nv = model.nverts();

    for(int i=0;i<nf;i++){
        std::array<int,3> idxs = model.getface(i);

        std::array<float,3> v1 = model.getvert(idxs[0]);
        std::array<float,3> v2 = model.getvert(idxs[1]);
        std::array<float,3> v3 = model.getvert(idxs[2]);

        int ax=std::round((v1[0]+1) * width / 2),ay=std::round((v1[1]+1) * height / 2);
        int bx=std::round((v2[0]+1) * width / 2),by=std::round((v2[1]+1) * height / 2);
        int cx=std::round((v3[0]+1) * width / 2),cy=std::round((v3[1]+1) * height / 2);

        line(ax, ay, bx, by, framebuffer, red);
        line(ax, ay, cx, cy, framebuffer, red);
        line(bx, by, cx, cy, framebuffer, red);
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}