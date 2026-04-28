// main.cpp
//
// Build: Visual Studio 2019/2022, x64, Debug/Release

#define _WIN32_WINNT 0x0602
#define NOMINMAX

#include <dxgi1_3.h>
#include <windows.h>
#include <tchar.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <cfloat>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

// SAFE_RELEASE helper
#define SAFE_RELEASE(p) do { if ((p) != nullptr) { (p)->Release(); (p) = nullptr; } } while(0)

using std::wstring;

// Globals
HINSTANCE           g_hInst = nullptr;
HWND                g_hWnd = nullptr;
UINT                g_ClientWidth = 1280;
UINT                g_ClientHeight = 720;

IDXGISwapChain1* g_pSwapChain = nullptr;
ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
ID3D11RenderTargetView* g_pRTV = nullptr;
ID3D11Texture2D* g_pDepthStencilTex = nullptr;
ID3D11DepthStencilView* g_pDSV = nullptr;
bool                g_bInitialized = false;

// Geometry / shader resources
ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;

// Textures
ID3D11Texture2D* g_pCubeTexture = nullptr;
ID3D11ShaderResourceView* g_pCubeSRV = nullptr;
ID3D11Texture2D* g_pSkyboxTexture = nullptr;
ID3D11ShaderResourceView* g_pSkyboxSRV = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

// Shaders / pipeline state
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11VertexShader* g_pSkyboxVertexShader = nullptr;
ID3D11PixelShader* g_pSkyboxPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11RasterizerState* g_pRasterizerState = nullptr;
ID3D11DepthStencilState* g_pSkyboxDepthState = nullptr;

// Separate constant buffers for model and vp matrices
ID3D11Buffer* g_pModelBuffer = nullptr; // m
ID3D11Buffer* g_pSceneBuffer = nullptr; // vp

// Camera state (rotation only)
float g_cameraYaw = 0.0f;
float g_cameraPitch = 0.0f;
const float g_cameraDistance = 5.0f;

struct ModelBuffer
{
    DirectX::XMFLOAT4X4 model;
};

struct SceneBuffer
{
    DirectX::XMFLOAT4X4 vp;
};

// Vertex layout: position + texture coordinates
struct Vertex
{
    float x, y, z;
    float u, v;
};

// Textured cube vertices: 6 faces * 4 vertices per face
static const Vertex g_Vertices[] = {
    // Front
    { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },

    // Back
    {  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },
    {  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

    // Top
    { -0.5f,  0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

    // Bottom
    { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 0.0f, 0.0f },

    // Left
    { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

    // Right
    {  0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },
    {  0.5f,  0.5f,  0.5f, 0.0f, 0.0f }
};

// 12 triangles = 36 indices
static const USHORT g_Indices[] = {
     0,  1,  2,   0,  2,  3,
     4,  5,  6,   4,  6,  7,
     8,  9, 10,   8, 10, 11,
    12, 13, 14,  12, 14, 15,
    16, 17, 18,  16, 18, 19,
    20, 21, 22,  20, 22, 23
};

// HLSL source compiled at runtime
static const char* g_VertexShaderSrc =
"cbuffer ModelBuffer : register(b0) {\n"
"    row_major float4x4 model;\n"
"};\n"
"cbuffer SceneBuffer : register(b1) {\n"
"    row_major float4x4 vp;\n"
"};\n"
"struct VSInput {\n"
"    float3 pos : POSITION;\n"
"    float2 uv  : TEXCOORD;\n"
"};\n"
"struct VSOutput {\n"
"    float4 pos : SV_Position;\n"
"    float2 uv  : TEXCOORD;\n"
"};\n"
"VSOutput main(VSInput vertex) {\n"
"    VSOutput result;\n"
"    result.pos = mul(mul(float4(vertex.pos, 1.0f), model), vp);\n"
"    result.uv = vertex.uv;\n"
"    return result;\n"
"}\n";

static const char* g_PixelShaderSrc =
"Texture2D colorTexture : register(t0);\n"
"SamplerState colorSampler : register(s0);\n"
"struct VSOutput {\n"
"    float4 pos : SV_Position;\n"
"    float2 uv  : TEXCOORD;\n"
"};\n"
"float4 main(VSOutput pixel) : SV_Target0 {\n"
"    return colorTexture.Sample(colorSampler, pixel.uv);\n"
"}\n";

static const char* g_SkyboxVertexShaderSrc =
"cbuffer ModelBuffer : register(b0) {\n"
"    row_major float4x4 model;\n"
"};\n"
"cbuffer SceneBuffer : register(b1) {\n"
"    row_major float4x4 vp;\n"
"};\n"
"struct VSInput {\n"
"    float3 pos : POSITION;\n"
"    float2 uv  : TEXCOORD;\n"
"};\n"
"struct VSOutput {\n"
"    float4 pos : SV_Position;\n"
"    float3 dir : TEXCOORD0;\n"
"};\n"
"VSOutput main(VSInput vertex) {\n"
"    VSOutput result;\n"
"    float4 worldPos = mul(float4(vertex.pos, 1.0f), model);\n"
"    result.pos = mul(worldPos, vp);\n"
"    result.dir = worldPos.xyz;\n"
"    return result;\n"
"}\n";

static const char* g_SkyboxPixelShaderSrc =
"TextureCube skyTexture : register(t0);\n"
"SamplerState skySampler : register(s0);\n"
"struct VSOutput {\n"
"    float4 pos : SV_Position;\n"
"    float3 dir : TEXCOORD0;\n"
"};\n"
"float4 main(VSOutput pixel) : SV_Target0 {\n"
"    return skyTexture.Sample(skySampler, normalize(pixel.dir));\n"
"}\n";

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow);
HRESULT InitD3D();
HRESULT InitTriangle();
void Render();
void Cleanup();
void OnResize(UINT width, UINT height);
HRESULT CreateDepthStencilResources(UINT width, UINT height);
IDXGIAdapter1* GetHardwareAdapter(IDXGIFactory1* pFactory);



inline void HR_CHECK(HRESULT hr, const char* msg = nullptr)
{
    if (FAILED(hr))
    {
        if (msg) OutputDebugStringA(msg);
        assert(false && "HRESULT failed - check debug output");
    }
}

#pragma pack(push, 1)
struct DDS_PIXELFORMAT
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t RGBBitCount;
    uint32_t RBitMask;
    uint32_t GBitMask;
    uint32_t BBitMask;
    uint32_t ABitMask;
};

struct DDS_HEADER
{
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};
#pragma pack(pop)

static constexpr uint32_t DDS_MAGIC = 0x20534444;
static constexpr uint32_t DDS_FOURCC = 0x00000004;
static constexpr uint32_t DDS_RGB = 0x00000040;

struct LoadedDDS
{
    std::vector<uint8_t> bytes;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
    UINT mipLevels = 0;
    size_t dataOffset = 0;
    bool compressed = false;
};

static bool IsBCFormat(DXGI_FORMAT fmt)
{
    return fmt == DXGI_FORMAT_BC1_UNORM ||
        fmt == DXGI_FORMAT_BC2_UNORM ||
        fmt == DXGI_FORMAT_BC3_UNORM;
}

static UINT GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_UNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM: return 16;
    default: return 0;
    }
}

static UINT GetBytesPerPixel(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return 4;
    default:
        return 0;
    }
}

static DXGI_FORMAT GetDDSFormat(const DDS_PIXELFORMAT& ddpf)
{
    if (ddpf.flags & DDS_FOURCC)
    {
        switch (ddpf.fourCC)
        {
        case 0x31545844: return DXGI_FORMAT_BC1_UNORM; // DXT1
        case 0x33545844: return DXGI_FORMAT_BC2_UNORM; // DXT3
        case 0x35545844: return DXGI_FORMAT_BC3_UNORM; // DXT5
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    if ((ddpf.flags & DDS_RGB) && ddpf.RGBBitCount == 32)
    {
        if (ddpf.RBitMask == 0x000000ff &&
            ddpf.GBitMask == 0x0000ff00 &&
            ddpf.BBitMask == 0x00ff0000 &&
            ddpf.ABitMask == 0xff000000)
        {
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        if (ddpf.RBitMask == 0x00ff0000 &&
            ddpf.GBitMask == 0x0000ff00 &&
            ddpf.BBitMask == 0x000000ff &&
            ddpf.ABitMask == 0xff000000)
        {
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        }
    }

    return DXGI_FORMAT_UNKNOWN;
}

static bool LoadDDSFile(const wchar_t* fileName, LoadedDDS& out)
{
    out = LoadedDDS{};

    std::ifstream file(fileName, std::ios::binary);
    if (!file)
        return false;

    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0)
        return false;

    out.bytes.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(out.bytes.data()), size))
        return false;

    if (out.bytes.size() < 4 + sizeof(DDS_HEADER))
        return false;

    const uint32_t magic = *reinterpret_cast<const uint32_t*>(out.bytes.data());
    if (magic != DDS_MAGIC)
        return false;

    const DDS_HEADER* header = reinterpret_cast<const DDS_HEADER*>(out.bytes.data() + 4);
    if (header->size != 124 || header->ddspf.size != 32)
        return false;

    out.format = GetDDSFormat(header->ddspf);
    if (out.format == DXGI_FORMAT_UNKNOWN)
        return false;

    out.width = header->width;
    out.height = header->height;
    out.mipLevels = header->mipMapCount ? header->mipMapCount : 1u;
    out.dataOffset = 4 + sizeof(DDS_HEADER);
    out.compressed = IsBCFormat(out.format);

    return true;
}

static HRESULT CreateTexture2DFromDDS(const wchar_t* fileName, ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv)
{
    if (!texture || !srv)
        return E_INVALIDARG;

    LoadedDDS dds;
    if (!LoadDDSFile(fileName, dds))
        return E_FAIL;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = dds.format;
    desc.ArraySize = 1;
    desc.MipLevels = dds.mipLevels;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width = dds.width;
    desc.Height = dds.height;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(dds.mipLevels);
    const uint8_t* src = dds.bytes.data() + dds.dataOffset;
    UINT w = dds.width;
    UINT h = dds.height;
    size_t offset = 0;

    for (UINT i = 0; i < dds.mipLevels; ++i)
    {
        UINT rowPitch = 0;
        UINT slicePitch = 0;
        if (dds.compressed)
        {
            const UINT blockBytes = GetBytesPerBlock(dds.format);
            rowPitch = std::max(1u, (w + 3u) / 4u) * blockBytes;
            slicePitch = rowPitch * std::max(1u, (h + 3u) / 4u);
        }
        else
        {
            const UINT bpp = GetBytesPerPixel(dds.format);
            rowPitch = w * bpp;
            slicePitch = rowPitch * h;
        }

        initData[i].pSysMem = src + offset;
        initData[i].SysMemPitch = rowPitch;
        initData[i].SysMemSlicePitch = slicePitch;

        offset += slicePitch;
        w = std::max(1u, w / 2u);
        h = std::max(1u, h / 2u);
    }

    HRESULT hr = g_pDevice->CreateTexture2D(&desc, initData.data(), texture);
    if (FAILED(hr))
        return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = dds.format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MostDetailedMip = 0;
    viewDesc.Texture2D.MipLevels = dds.mipLevels;

    hr = g_pDevice->CreateShaderResourceView(*texture, &viewDesc, srv);
    if (FAILED(hr))
        return hr;

    return S_OK;
}

static HRESULT CreateCubemapFromDDSFaces(const wchar_t* const fileNames[6], ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv)
{
    if (!texture || !srv)
        return E_INVALIDARG;

    LoadedDDS faces[6];
    if (!LoadDDSFile(fileNames[0], faces[0]))
        return E_FAIL;

    for (int i = 1; i < 6; ++i)
    {
        if (!LoadDDSFile(fileNames[i], faces[i]))
            return E_FAIL;
        if (faces[i].width != faces[0].width || faces[i].height != faces[0].height || faces[i].format != faces[0].format)
            return E_FAIL;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = faces[0].format;
    desc.ArraySize = 6;
    desc.MipLevels = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width = faces[0].width;
    desc.Height = faces[0].height;

    D3D11_SUBRESOURCE_DATA data[6] = {};
    for (int i = 0; i < 6; ++i)
    {
        const UINT rowPitch = faces[i].compressed
            ? std::max(1u, (faces[i].width + 3u) / 4u) * GetBytesPerBlock(faces[i].format)
            : faces[i].width * GetBytesPerPixel(faces[i].format);

        data[i].pSysMem = faces[i].bytes.data() + faces[i].dataOffset;
        data[i].SysMemPitch = rowPitch;
        data[i].SysMemSlicePitch = faces[i].compressed
            ? rowPitch * std::max(1u, (faces[i].height + 3u) / 4u)
            : rowPitch * faces[i].height;
    }

    HRESULT hr = g_pDevice->CreateTexture2D(&desc, data, texture);
    if (FAILED(hr))
        return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = faces[0].format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    viewDesc.TextureCube.MostDetailedMip = 0;
    viewDesc.TextureCube.MipLevels = 1;

    hr = g_pDevice->CreateShaderResourceView(*texture, &viewDesc, srv);
    if (FAILED(hr))
        return hr;

    return S_OK;
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    g_hInst = hInstance;

    if (FAILED(InitWindow(hInstance, nCmdShow)))
        return 0;

    if (FAILED(InitD3D()))
    {
        MessageBox(nullptr, _T("DirectX 11 init failed"), _T("Error"), MB_OK | MB_ICONERROR);
        Cleanup();
        return 0;
    }

    if (FAILED(InitTriangle()))
    {
        MessageBox(nullptr, _T("Cube resources init failed"), _T("Error"), MB_OK | MB_ICONERROR);
        Cleanup();
        return 0;
    }
    g_bInitialized = true;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Render();
        }
    }

    Cleanup();

#ifdef _DEBUG
    {
        IDXGIDebug1* dxgiDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
        {
            dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
            dxgiDebug->Release();
        }
    }
#endif

    return (int)msg.wParam;
}

// Window initialization
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX11SampleWindowClass";

    if (!RegisterClassEx(&wc))
    {
        HR_CHECK(E_FAIL, "RegisterClassEx failed\n");
        return E_FAIL;
    }

    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindow(
        wc.lpszClassName,
        L"DX11 - Rotating Cube",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hWnd)
    {
        HR_CHECK(E_FAIL, "CreateWindow failed\n");
        return E_FAIL;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    return S_OK;
}

// Pick a hardware adapter (skip Microsoft Basic Render Driver)
IDXGIAdapter1* GetHardwareAdapter(IDXGIFactory1* pFactory)
{
    if (!pFactory) return nullptr;
    IDXGIAdapter1* adapter = nullptr;
    IDXGIAdapter1* chosen = nullptr;
    for (UINT i = 0; SUCCEEDED(pFactory->EnumAdapters1(i, &adapter)); ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            chosen = adapter;
            break;
        }
        SAFE_RELEASE(adapter);
    }
    return chosen;
}

HRESULT CreateDepthStencilResources(UINT width, UINT height)
{
    SAFE_RELEASE(g_pDSV);
    SAFE_RELEASE(g_pDepthStencilTex);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.CPUAccessFlags = 0;
    depthDesc.MiscFlags = 0;

    HRESULT hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &g_pDepthStencilTex);
    HR_CHECK(hr, "CreateTexture2D (depth) failed\n");

    hr = g_pDevice->CreateDepthStencilView(g_pDepthStencilTex, nullptr, &g_pDSV);
    HR_CHECK(hr, "CreateDepthStencilView failed\n");

    return S_OK;
}

// D3D11 + SwapChain init
HRESULT InitD3D()
{
    HRESULT hr = S_OK;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    IDXGIFactory1* pFactory1 = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory1);
    HR_CHECK(hr, "CreateDXGIFactory1 failed\n");

    IDXGIAdapter1* adapter = GetHardwareAdapter(pFactory1);

    D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    hr = D3D11CreateDevice(
        adapter,
        driverType,
        nullptr,
        createFlags,
        &featureLevel,
        1,
        D3D11_SDK_VERSION,
        &g_pDevice,
        nullptr,
        &g_pContext
    );

    if (FAILED(hr))
    {
        if (adapter)
        {
            SAFE_RELEASE(adapter);
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                createFlags,
                &featureLevel,
                1,
                D3D11_SDK_VERSION,
                &g_pDevice,
                nullptr,
                &g_pContext
            );
        }
    }
    HR_CHECK(hr, "D3D11CreateDevice failed\n");

    IDXGIDevice* dxgiDevice = nullptr;
    hr = g_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    HR_CHECK(hr, "QueryInterface IDXGIDevice failed\n");

    IDXGIAdapter* dxgiAdapter = nullptr;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    SAFE_RELEASE(dxgiDevice);
    HR_CHECK(hr, "GetAdapter failed\n");

    IDXGIFactory2* pFactory2 = nullptr;
    hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&pFactory2);
    SAFE_RELEASE(dxgiAdapter);
    HR_CHECK(hr, "GetParent IDXGIFactory2 failed\n");

    pFactory2->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER);

    DXGI_SWAP_CHAIN_DESC1 scd1 = {};
    scd1.Width = g_ClientWidth;
    scd1.Height = g_ClientHeight;
    scd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd1.SampleDesc.Count = 1;
    scd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd1.BufferCount = 2;
    scd1.Scaling = DXGI_SCALING_STRETCH;
    scd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd1.Flags = 0;

    IDXGISwapChain1* swapChain1 = nullptr;
    hr = pFactory2->CreateSwapChainForHwnd(g_pDevice, g_hWnd, &scd1, nullptr, nullptr, &swapChain1);
    if (FAILED(hr))
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = g_ClientWidth;
        sd.BufferDesc.Height = g_ClientHeight;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = g_hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        IDXGIFactory* pFactoryLegacy = nullptr;
        hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactoryLegacy);
        if (SUCCEEDED(hr))
        {
            hr = pFactoryLegacy->CreateSwapChain(g_pDevice, &sd, (IDXGISwapChain**)&swapChain1);
            SAFE_RELEASE(pFactoryLegacy);
        }
        HR_CHECK(hr, "CreateSwapChain fallback failed\n");
    }

    g_pSwapChain = swapChain1;

    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    HR_CHECK(hr, "GetBuffer failed\n");

    hr = g_pDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
    SAFE_RELEASE(backBuffer);
    HR_CHECK(hr, "CreateRenderTargetView failed\n");

    hr = CreateDepthStencilResources(g_ClientWidth, g_ClientHeight);
    HR_CHECK(hr, "CreateDepthStencilResources failed\n");

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_ClientWidth;
    vp.Height = (FLOAT)g_ClientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pContext->RSSetViewports(1, &vp);

    SAFE_RELEASE(pFactory2);
    SAFE_RELEASE(pFactory1);
    SAFE_RELEASE(adapter);

    return S_OK;
}

// Compile shaders, create vertex/index buffers and constant buffers
HRESULT InitTriangle()
{
    HRESULT hr = S_OK;

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVSCode = nullptr;
    ID3DBlob* pErr = nullptr;
    hr = D3DCompile(g_VertexShaderSrc, strlen(g_VertexShaderSrc), "VS",
        nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &pVSCode, &pErr);
    if (FAILED(hr))
    {
        if (pErr) OutputDebugStringA((const char*)pErr->GetBufferPointer());
        SAFE_RELEASE(pErr);
        HR_CHECK(hr, "D3DCompile VS failed\n");
        return hr;
    }
    SAFE_RELEASE(pErr);

    hr = g_pDevice->CreateVertexShader(pVSCode->GetBufferPointer(), pVSCode->GetBufferSize(), nullptr, &g_pVertexShader);
    HR_CHECK(hr, "CreateVertexShader failed\n");

    static const D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    hr = g_pDevice->CreateInputLayout(inputDesc, _countof(inputDesc),
        pVSCode->GetBufferPointer(), pVSCode->GetBufferSize(), &g_pInputLayout);
    SAFE_RELEASE(pVSCode);
    HR_CHECK(hr, "CreateInputLayout failed\n");

    ID3DBlob* pPSCode = nullptr;
    hr = D3DCompile(g_PixelShaderSrc, strlen(g_PixelShaderSrc), "PS",
        nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pPSCode, &pErr);
    if (FAILED(hr))
    {
        if (pErr) OutputDebugStringA((const char*)pErr->GetBufferPointer());
        SAFE_RELEASE(pErr);
        HR_CHECK(hr, "D3DCompile PS failed\n");
        return hr;
    }
    SAFE_RELEASE(pErr);
    hr = g_pDevice->CreatePixelShader(pPSCode->GetBufferPointer(), pPSCode->GetBufferSize(), nullptr, &g_pPixelShader);
    SAFE_RELEASE(pPSCode);
    HR_CHECK(hr, "CreatePixelShader failed\n");

    ID3DBlob* pSkyVSCode = nullptr;
    hr = D3DCompile(g_SkyboxVertexShaderSrc, strlen(g_SkyboxVertexShaderSrc), "SkyVS",
        nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &pSkyVSCode, &pErr);
    if (FAILED(hr))
    {
        if (pErr) OutputDebugStringA((const char*)pErr->GetBufferPointer());
        SAFE_RELEASE(pErr);
        HR_CHECK(hr, "D3DCompile Skybox VS failed\n");
        return hr;
    }
    SAFE_RELEASE(pErr);
    hr = g_pDevice->CreateVertexShader(pSkyVSCode->GetBufferPointer(), pSkyVSCode->GetBufferSize(), nullptr, &g_pSkyboxVertexShader);
    HR_CHECK(hr, "CreateSkyboxVertexShader failed\n");

    ID3DBlob* pSkyPSCode = nullptr;
    hr = D3DCompile(g_SkyboxPixelShaderSrc, strlen(g_SkyboxPixelShaderSrc), "SkyPS",
        nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pSkyPSCode, &pErr);
    if (FAILED(hr))
    {
        if (pErr) OutputDebugStringA((const char*)pErr->GetBufferPointer());
        SAFE_RELEASE(pErr);
        HR_CHECK(hr, "D3DCompile Skybox PS failed\n");
        return hr;
    }
    SAFE_RELEASE(pErr);
    hr = g_pDevice->CreatePixelShader(pSkyPSCode->GetBufferPointer(), pSkyPSCode->GetBufferSize(), nullptr, &g_pSkyboxPixelShader);
    SAFE_RELEASE(pSkyPSCode);
    HR_CHECK(hr, "CreateSkyboxPixelShader failed\n");

    {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable = TRUE;

        hr = g_pDevice->CreateRasterizerState(&desc, &g_pRasterizerState);
        HR_CHECK(hr, "CreateRasterizerState failed\n");
    }

    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        desc.StencilEnable = FALSE;
        hr = g_pDevice->CreateDepthStencilState(&desc, &g_pSkyboxDepthState);
        HR_CHECK(hr, "CreateDepthStencilState failed\n");
    }

    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(g_Vertices);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = g_Vertices;

        hr = g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);
        HR_CHECK(hr, "CreateBuffer (vertex) failed\n");
    }

    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(g_Indices);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = g_Indices;

        hr = g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
        HR_CHECK(hr, "CreateBuffer (index) failed\n");
    }

    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(ModelBuffer);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer);
        HR_CHECK(hr, "CreateBuffer (model cbuffer) failed\n");
    }

    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(SceneBuffer);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pSceneBuffer);
        HR_CHECK(hr, "CreateBuffer (scene cbuffer) failed\n");
    }

    hr = CreateTexture2DFromDDS(L"cube.dds", &g_pCubeTexture, &g_pCubeSRV);
    HR_CHECK(hr, "CreateTexture2DFromDDS(cube.dds) failed\n");

    const wchar_t* skyboxFaces[6] = {
        L"skybox_posx.dds",
        L"skybox_negx.dds",
        L"skybox_posy.dds",
        L"skybox_negy.dds",
        L"skybox_posz.dds",
        L"skybox_negz.dds"
    };
    hr = CreateCubemapFromDDSFaces(skyboxFaces, &g_pSkyboxTexture, &g_pSkyboxSRV);
    HR_CHECK(hr, "CreateCubemapFromDDSFaces failed\n");

    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MinLOD = -FLT_MAX;
        desc.MaxLOD = FLT_MAX;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 16;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.BorderColor[0] = 1.0f;
        desc.BorderColor[1] = 1.0f;
        desc.BorderColor[2] = 1.0f;
        desc.BorderColor[3] = 1.0f;

        hr = g_pDevice->CreateSamplerState(&desc, &g_pSampler);
        HR_CHECK(hr, "CreateSamplerState failed\n");
    }

    return S_OK;
}

// Resize handling
void OnResize(UINT width, UINT height)
{
    if (!g_pSwapChain || !g_pDevice || !g_pContext) return;
    if (width == 0 || height == 0) return;

    g_ClientWidth = width;
    g_ClientHeight = height;

    g_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    SAFE_RELEASE(g_pRTV);
    SAFE_RELEASE(g_pDSV);
    SAFE_RELEASE(g_pDepthStencilTex);

    HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        HR_CHECK(hr, "ResizeBuffers failed\n");
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    HR_CHECK(hr, "GetBuffer after ResizeBuffers failed\n");

    hr = g_pDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
    SAFE_RELEASE(backBuffer);
    HR_CHECK(hr, "CreateRenderTargetView after resize failed\n");

    hr = CreateDepthStencilResources(width, height);
    HR_CHECK(hr, "CreateDepthStencilResources after resize failed\n");

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_ClientWidth;
    vp.Height = (FLOAT)g_ClientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pContext->RSSetViewports(1, &vp);
}

// Frame rendering
void Render()
{
    if (!g_pContext || !g_pRTV || !g_pSwapChain || !g_pDSV) return;

    g_pContext->OMSetRenderTargets(1, &g_pRTV, g_pDSV);

    const FLOAT clearColor[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
    g_pContext->ClearRenderTargetView(g_pRTV, clearColor);
    g_pContext->ClearDepthStencilView(g_pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    static ULONGLONG lastTick = GetTickCount64();
    ULONGLONG nowTick = GetTickCount64();
    float dt = static_cast<float>(nowTick - lastTick) * 0.001f;
    lastTick = nowTick;

    const float cameraSpeed = 1.5f;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)  g_cameraYaw -= cameraSpeed * dt;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) g_cameraYaw += cameraSpeed * dt;
    if (GetAsyncKeyState(VK_UP) & 0x8000)    g_cameraPitch += cameraSpeed * dt;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)  g_cameraPitch -= cameraSpeed * dt;

    if (g_cameraPitch > 1.4f) g_cameraPitch = 1.4f;
    if (g_cameraPitch < -1.4f) g_cameraPitch = -1.4f;

    float t = static_cast<float>(nowTick) * 0.001f;

    DirectX::XMMATRIX cameraRot = DirectX::XMMatrixRotationRollPitchYaw(g_cameraPitch, g_cameraYaw, 0.0f);
    DirectX::XMVECTOR eye = DirectX::XMVector3TransformCoord(
        DirectX::XMVectorSet(0.0f, 0.0f, -g_cameraDistance, 1.0f),
        cameraRot
    );
    DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), cameraRot);

    DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), cameraRot);

    float aspectRatio = static_cast<float>(g_ClientWidth) / static_cast<float>(g_ClientHeight);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, aspectRatio, 0.1f, 100.0f);

    SceneBuffer sceneBuffer = {};
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, DirectX::XMVectorZero(), up);
    DirectX::XMStoreFloat4x4(&sceneBuffer.vp, DirectX::XMMatrixMultiply(view, proj));

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = g_pContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    HR_CHECK(hr, "Map(scene buffer) failed\n");
    if (SUCCEEDED(hr))
    {
        *reinterpret_cast<SceneBuffer*>(mapped.pData) = sceneBuffer;
        g_pContext->Unmap(g_pSceneBuffer, 0);
    }

    DirectX::XMMATRIX skyView = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorZero(),
        forward,
        up
    );
    SceneBuffer skySceneBuffer = {};
    DirectX::XMStoreFloat4x4(&skySceneBuffer.vp, DirectX::XMMatrixMultiply(skyView, proj));

    hr = g_pContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    HR_CHECK(hr, "Map(sky scene buffer) failed\n");
    if (SUCCEEDED(hr))
    {
        *reinterpret_cast<SceneBuffer*>(mapped.pData) = skySceneBuffer;
        g_pContext->Unmap(g_pSceneBuffer, 0);
    }

    ModelBuffer modelBuffer = {};
    DirectX::XMMATRIX skyModel = DirectX::XMMatrixScaling(40.0f, 40.0f, 40.0f);
    DirectX::XMStoreFloat4x4(&modelBuffer.model, skyModel);
    g_pContext->UpdateSubresource(g_pModelBuffer, 0, nullptr, &modelBuffer, 0, 0);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_pContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pContext->IASetInputLayout(g_pInputLayout);
    g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pContext->RSSetState(g_pRasterizerState);

    // Skybox pass
    g_pContext->OMSetDepthStencilState(g_pSkyboxDepthState, 0);
    g_pContext->VSSetShader(g_pSkyboxVertexShader, nullptr, 0);
    g_pContext->PSSetShader(g_pSkyboxPixelShader, nullptr, 0);
    ID3D11Buffer* vsCBuffers[] = { g_pModelBuffer, g_pSceneBuffer };
    g_pContext->VSSetConstantBuffers(0, _countof(vsCBuffers), vsCBuffers);
    ID3D11ShaderResourceView* skyResources[] = { g_pSkyboxSRV };
    g_pContext->PSSetShaderResources(0, 1, skyResources);
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pContext->PSSetSamplers(0, 1, samplers);
    g_pContext->DrawIndexed(_countof(g_Indices), 0, 0);

    // Restore regular scene VP for the cube pass
    hr = g_pContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    HR_CHECK(hr, "Map(scene buffer restore) failed\n");
    if (SUCCEEDED(hr))
    {
        *reinterpret_cast<SceneBuffer*>(mapped.pData) = sceneBuffer;
        g_pContext->Unmap(g_pSceneBuffer, 0);
    }

    // Cube pass
    DirectX::XMMATRIX cubeModel = DirectX::XMMatrixRotationRollPitchYaw(t * 0.7f, t * 1.0f, 0.0f);
    DirectX::XMStoreFloat4x4(&modelBuffer.model, cubeModel);
    g_pContext->UpdateSubresource(g_pModelBuffer, 0, nullptr, &modelBuffer, 0, 0);

    g_pContext->OMSetDepthStencilState(nullptr, 0);
    g_pContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pContext->PSSetShader(g_pPixelShader, nullptr, 0);
    ID3D11ShaderResourceView* cubeResources[] = { g_pCubeSRV };
    g_pContext->PSSetShaderResources(0, 1, cubeResources);
    g_pContext->DrawIndexed(_countof(g_Indices), 0, 0);

    HRESULT presentHr = g_pSwapChain->Present(1, 0);
    if (FAILED(presentHr))
    {
        HR_CHECK(presentHr, "Present failed\n");
    }
}

// Resource cleanup
void Cleanup()
{
    if (g_pContext)
    {
        g_pContext->ClearState();
        g_pContext->Flush();
    }

    SAFE_RELEASE(g_pSceneBuffer);
    SAFE_RELEASE(g_pModelBuffer);

    SAFE_RELEASE(g_pSampler);
    SAFE_RELEASE(g_pSkyboxDepthState);
    SAFE_RELEASE(g_pSkyboxPixelShader);
    SAFE_RELEASE(g_pSkyboxVertexShader);
    SAFE_RELEASE(g_pCubeSRV);
    SAFE_RELEASE(g_pCubeTexture);
    SAFE_RELEASE(g_pSkyboxSRV);
    SAFE_RELEASE(g_pSkyboxTexture);

    SAFE_RELEASE(g_pRasterizerState);
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);

    SAFE_RELEASE(g_pDSV);
    SAFE_RELEASE(g_pDepthStencilTex);
    SAFE_RELEASE(g_pRTV);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pContext);
    SAFE_RELEASE(g_pDevice);
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        if (!g_bInitialized) break;

        if (wParam == SIZE_MINIMIZED)
        {
            // skip when minimized
        }
        else
        {
            OnResize(width, height);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
