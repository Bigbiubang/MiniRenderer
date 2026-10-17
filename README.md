# MiniRenderer

A C++20 CPU software rasterizer built while studying [TinyRenderer](https://github.com/ssloy/tinyrenderer). It renders textured OBJ scenes to TGA images and implements lighting, shadows, transparency, and screen-space ambient occlusion without a graphics API.

![Diablo model rendered by MiniRenderer](docs/images/diablo.png)

## Features

- Triangle rasterization with depth testing, back-face culling, clipping against the camera's positive-w plane, and perspective-correct interpolation.
- Diffuse textures, tangent-space normal maps, and specular maps with Phong lighting.
- Directional-light shadow mapping using a fitted orthographic light projection and 3×3 percentage-closer filtering.
- Depth-sorted alpha blending for transparent fragments.
- Screen-space ambient occlusion (SSAO) sampled from the camera depth buffer.
- Optional 4× MSAA with per-sample coverage, depth, and transparent blending. The camera pass bins triangles into horizontal bands that can render in parallel.

The SSAO pass also writes a grayscale diagnostic image:

![SSAO factor for the Diablo scene](docs/images/ssao-diablo.png)

The same silhouette at 2× display zoom, rendered with 1× and 4× coverage sampling:

| 1× | 4× MSAA |
| :---: | :---: |
| ![Silhouette without MSAA](docs/images/msaa-1x-detail.png) | ![Silhouette with 4x MSAA](docs/images/msaa-4x-detail.png) |

[View the full 4× render](docs/images/diablo-msaa4.png).

## Build and run

You need a C++20 compiler and CMake 3.12 or newer. OpenMP is optional; CMake enables it when available.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tinyrenderer obj/diablo3_pose/diablo3_pose.obj obj/floor.obj
```

Run the executable from the repository root so it can find the textures next to the OBJ files. It writes `framebuffer.tga` and `ao.tga` in the current directory. The screenshots above were converted to PNG with `convert framebuffer.tga -auto-orient ...` and `convert ao.tga -auto-orient ...`; the orientation step is needed when displaying these TGA files through ImageMagick. The default is 1× sampling; use `--msaa 4` for 4× MSAA. Add `--timings` to print the time spent loading, rendering shadows, rendering the camera view, and computing SSAO plus writing images.

To render the head and its transparent eye layers:

```sh
./build/tinyrenderer --msaa 4 \
  obj/african_head/african_head.obj \
  obj/african_head/african_head_eye_inner.obj \
  obj/african_head/african_head_eye_outer.obj \
  obj/floor.obj
```

![Head and transparent eye layers rendered by MiniRenderer](docs/images/african-head.png)

## Rendering pipeline

1. Load triangle meshes and their `_diffuse.tga`, `_nm_tangent.tga`, and `_spec.tga` textures in [`model.cpp`](model.cpp).
2. Render opaque texels from the light's direction into a shadow depth buffer.
3. Bin triangles into screen bands and render opaque and transparent geometry from the camera with the Phong shader in [`main.cpp`](main.cpp). [`our_gl.cpp`](our_gl.cpp) handles clipping, per-sample rasterization, depth testing, transparency composition, and MSAA resolve.
4. Estimate SSAO from the resolved opaque depth buffer, multiply the result into the final image, and save both images.

## Performance

The table shows the median of five Release runs of the Diablo scene at 800×800 on an Intel Core i7-14700KF. Set `OMP_NUM_THREADS` to control the number of threads. Times include texture loading and TGA output and will vary by machine.

| MSAA | Threads | Total | Camera pass | SSAO + output |
| --- | ---: | ---: | ---: | ---: |
| 1× | 1 | 605 ms | 53 ms | 491 ms |
| 1× | 4 | 344 ms | 27 ms | 255 ms |
| 4× | 1 | 639 ms | 84 ms | 494 ms |
| 4× | 4 | 364 ms | 48 ms | 256 ms |

The camera pass and SSAO run in parallel. Shadow-map rendering and asset loading are currently serial. The 4× mode uses more memory because it stores separate color, depth, and transparent fragments for each sample.

The camera, directional light, 800×800 output size, and shadow-map size are currently fixed in `main.cpp`. The OBJ loader expects triangulated faces with position, UV, and normal indices. The shadow pass treats only fully opaque texels as occluders; SSAO uses the opaque depth buffer and is applied to the composited image. These are current limitations of the renderer.

## Project origin

This is an altered and extended implementation of Dmitry V. Sokolov's [TinyRenderer](https://github.com/ssloy/tinyrenderer) tutorial, not the original project. The tutorial's sample meshes and textures are included under `obj/`, and the original license notice is preserved in [`LICENSE.txt`](LICENSE.txt). The `chapter/` directory contains earlier learning-stage implementations; the root sources are the current renderer.
