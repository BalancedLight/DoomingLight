#include "doom_invuln_shader.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <new>
#include <cstring>

/* Match the game's grayscale pass input, but invert scene luminance. */
static const char shader_source[] =
    "Texture2D scene : register(t0);"
    "SamplerState samp : register(s0);"
    "float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {"
    "float3 c = scene.Sample(samp, uv).rgb;"
    "float v = 1.0 - saturate(dot(c, float3(0.30, 0.59, 0.11)));"
    "return float4(v, v, v, 1.0); }";

struct DoomInvulnShader { ID3D11PixelShader *pixel; };

extern "C" DoomInvulnShader *doom_invuln_shader_create(void *device) {
    if (!device) return nullptr;
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) return nullptr;
    using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
        ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
    auto compile = reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile"));
    ID3DBlob *blob = nullptr, *errors = nullptr;
    HRESULT status = compile ? compile(shader_source, std::strlen(shader_source), nullptr,
        nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors) : E_FAIL;
    if (errors) errors->Release();
    ID3D11PixelShader *pixel = nullptr;
    if (SUCCEEDED(status) && blob)
        status = static_cast<ID3D11Device *>(device)->CreatePixelShader(
            blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pixel);
    if (blob) blob->Release();
    FreeLibrary(compiler);
    if (FAILED(status) || !pixel) return nullptr;
    auto *result = new (std::nothrow) DoomInvulnShader{pixel};
    if (!result) pixel->Release();
    return result;
}

extern "C" void *doom_invuln_shader_pixel(DoomInvulnShader *shader) {
    return shader ? shader->pixel : nullptr;
}

extern "C" void doom_invuln_shader_destroy(DoomInvulnShader *shader) {
    if (!shader) return;
    if (shader->pixel) shader->pixel->Release();
    delete shader;
}
