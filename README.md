## Overview
Experimetal rendering engine for loading and displaying Gltf models as shown below.
 

![image](https://user-images.githubusercontent.com/8186559/132597501-5a13ab40-7f4c-40af-926a-332e30fe29bb.png)

## Features
* Physically Based Rendering
* Image Based Lighting using Split Sum Approximation
* Normalmap and Roughness map filtering using von Mises-Fisher convolution
* MSAA

## Build
This project uses [Cmake with Visual Studio](https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-160). 

After cloning, update submodules: `git submodule update --init --recursive`

Then open the project root folder in Visual Studio to build.

## Dependencies
* [DirectX12 Agility SDK](https://devblogs.microsoft.com/directx/directx12agility/)
* [DirectXShaderCompiler](https://github.com/ehsannas/DirectXShaderCompiler)
* [DirectXTex](https://github.com/Microsoft/DirectXTex)
* [DirectXTK](https://github.com/Microsoft/DirectXTK) for SimpleMath
* [WinPixEventRuntime](https://devblogs.microsoft.com/pix/download/)
* [TinyGltf](https://github.com/syoyo/tinygltf) 
* [ImGui](https://github.com/ocornut/imgui)
* [MikkTSpace](https://github.com/mmikk/MikkTSpace)
* [SpookyHash](https://github.com/k0dai/spookyhash)
* [JSON for Modern C++](https://github.com/nlohmann/json)
* [Tracy](https://github.com/wolfpld/tracy)

