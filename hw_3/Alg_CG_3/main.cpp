// main.cpp
// Lab 3: Rotating cube in 3D with camera control and separate model / vp constant buffers
// Based on lab 1 (window + DirectX 11 init + back-buffer clear + WM_SIZE handling),
// extended with:
//  - Cube geometry (24 vertices, 36 indices)
//  - Vertex / Pixel shaders compiled at runtime via D3DCompile
//  - Input layout (POSITION + COLOR)
//  - Perspective projection
//  - Separate constant buffers for model (m) and view-projection (vp)
//  - Camera rotation via arrow keys
//  - Depth buffer
//
// Build: Visual Studio 2019/2022, x64, Debug/Release

#define _WIN32_WINNT 0x0602

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
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11RasterizerState* g_pRasterizerState = nullptr;

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

// Vertex layout: float3 position + COLORREF color (R8G8B8A8_UNORM)
struct Vertex
{
    float    x, y, z;
    COLORREF color;
};

// Cube vertices: 6 faces * 4 vertices per face (unique vertices per face for clean colors)
static const Vertex g_Vertices[] = {
    // Front (+Z) - red
    { -0.5f, -0.5f,  0.5f, RGB(255,   0,   0) },
    {  0.5f, -0.5f,  0.5f, RGB(255,   0,   0) },
    {  0.5f,  0.5f,  0.5f, RGB(255,   0,   0) },
    { -0.5f,  0.5f,  0.5f, RGB(255,   0,   0) },

    // Back (-Z) - green
    {  0.5f, -0.5f, -0.5f, RGB(0, 255,   0) },
    { -0.5f, -0.5f, -0.5f, RGB(0, 255,   0) },
    { -0.5f,  0.5f, -0.5f, RGB(0, 255,   0) },
    {  0.5f,  0.5f, -0.5f, RGB(0, 255,   0) },

    // Top (+Y) - blue
    { -0.5f,  0.5f,  0.5f, RGB(0,   0, 255) },
    {  0.5f,  0.5f,  0.5f, RGB(0,   0, 255) },
    {  0.5f,  0.5f, -0.5f, RGB(0,   0, 255) },
    { -0.5f,  0.5f, -0.5f, RGB(0,   0, 255) },

    // Bottom (-Y) - yellow
    { -0.5f, -0.5f, -0.5f, RGB(255, 255,   0) },
    {  0.5f, -0.5f, -0.5f, RGB(255, 255,   0) },
    {  0.5f, -0.5f,  0.5f, RGB(255, 255,   0) },
    { -0.5f, -0.5f,  0.5f, RGB(255, 255,   0) },

    // Left (-X) - magenta
    { -0.5f, -0.5f, -0.5f, RGB(255,   0, 255) },
    { -0.5f, -0.5f,  0.5f, RGB(255,   0, 255) },
    { -0.5f,  0.5f,  0.5f, RGB(255,   0, 255) },
    { -0.5f,  0.5f, -0.5f, RGB(255,   0, 255) },

    // Right (+X) - cyan
    {  0.5f, -0.5f,  0.5f, RGB(0, 255, 255) },
    {  0.5f, -0.5f, -0.5f, RGB(0, 255, 255) },
    {  0.5f,  0.5f, -0.5f, RGB(0, 255, 255) },
    {  0.5f,  0.5f,  0.5f, RGB(0, 255, 255) }
};

// 12 triangles = 36 indices
static const USHORT g_Indices[] = {
     0,  1,  2,   0,  2,  3,   // front
     4,  5,  6,   4,  6,  7,   // back
     8,  9, 10,   8, 10, 11,   // top
    12, 13, 14,  12, 14, 15,   // bottom
    16, 17, 18,  16, 18, 19,   // left
    20, 21, 22,  20, 22, 23    // right
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
"    float3 pos   : POSITION;\n"
"    float4 color : COLOR;\n"
"};\n"
"struct VSOutput {\n"
"    float4 pos   : SV_Position;\n"
"    float4 color : COLOR;\n"
"};\n"
"VSOutput main(VSInput vertex) {\n"
"    VSOutput result;\n"
"    result.pos = mul(mul(float4(vertex.pos, 1.0f), model), vp);\n"
"    result.color = vertex.color;\n"
"    return result;\n"
"}\n";

static const char* g_PixelShaderSrc =
"struct VSOutput {\n"
"    float4 pos   : SV_Position;\n"
"    float4 color : COLOR;\n"
"};\n"
"float4 main(VSOutput pixel) : SV_Target0 {\n"
"    return pixel.color;\n"
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

    // Vertex shader
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

    // Input layout (matches Vertex struct)
    static const D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    hr = g_pDevice->CreateInputLayout(inputDesc, _countof(inputDesc),
        pVSCode->GetBufferPointer(), pVSCode->GetBufferSize(), &g_pInputLayout);
    SAFE_RELEASE(pVSCode);
    HR_CHECK(hr, "CreateInputLayout failed\n");

    // Pixel shader
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

    // Rasterizer state: no culling, so all cube faces remain visible
    {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable = TRUE;

        hr = g_pDevice->CreateRasterizerState(&desc, &g_pRasterizerState);
        HR_CHECK(hr, "CreateRasterizerState failed\n");
    }

    // Vertex buffer
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
        data.SysMemPitch = sizeof(g_Vertices);
        data.SysMemSlicePitch = 0;

        hr = g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);
        HR_CHECK(hr, "CreateBuffer (vertex) failed\n");
    }

    // Index buffer
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
        data.SysMemPitch = sizeof(g_Indices);
        data.SysMemSlicePitch = 0;

        hr = g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
        HR_CHECK(hr, "CreateBuffer (index) failed\n");
    }

    // Model buffer (m) - default usage, updated with UpdateSubresource
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

    // Scene buffer (vp) - dynamic usage, updated with Map/Unmap
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

    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pContext->ClearRenderTargetView(g_pRTV, clearColor);
    g_pContext->ClearDepthStencilView(g_pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Camera rotation controls
    static ULONGLONG lastTick = GetTickCount64();
    ULONGLONG nowTick = GetTickCount64();
    float dt = static_cast<float>(nowTick - lastTick) * 0.001f;
    lastTick = nowTick;

    const float cameraSpeed = 1.5f; // rad/sec
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)  g_cameraYaw -= cameraSpeed * dt;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) g_cameraYaw += cameraSpeed * dt;
    if (GetAsyncKeyState(VK_UP) & 0x8000)    g_cameraPitch += cameraSpeed * dt;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)  g_cameraPitch -= cameraSpeed * dt;

    if (g_cameraPitch > 1.4f) g_cameraPitch = 1.4f;
    if (g_cameraPitch < -1.4f) g_cameraPitch = -1.4f;

    // Model matrix: rotating cube
    float t = static_cast<float>(nowTick) * 0.001f;
    DirectX::XMMATRIX model = DirectX::XMMatrixRotationRollPitchYaw(t * 0.7f, t * 1.0f, 0.0f);

    ModelBuffer modelBuffer = {};
    DirectX::XMStoreFloat4x4(&modelBuffer.model, model);
    g_pContext->UpdateSubresource(g_pModelBuffer, 0, nullptr, &modelBuffer, 0, 0);

    // View matrix from camera rotation only (camera orbits around origin)
    DirectX::XMMATRIX cameraRot = DirectX::XMMatrixRotationRollPitchYaw(g_cameraPitch, g_cameraYaw, 0.0f);
    DirectX::XMVECTOR eye = DirectX::XMVector3TransformCoord(
        DirectX::XMVectorSet(0.0f, 0.0f, -g_cameraDistance, 1.0f),
        cameraRot
    );
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        eye,
        DirectX::XMVectorZero(),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    // Projection matrix
    float aspectRatio = static_cast<float>(g_ClientWidth) / static_cast<float>(g_ClientHeight);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, aspectRatio, 0.1f, 100.0f);

    SceneBuffer sceneBuffer = {};
    DirectX::XMStoreFloat4x4(&sceneBuffer.vp, DirectX::XMMatrixMultiply(view, proj));

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = g_pContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    HR_CHECK(hr, "Map(scene buffer) failed\n");
    if (SUCCEEDED(hr))
    {
        *reinterpret_cast<SceneBuffer*>(mapped.pData) = sceneBuffer;
        g_pContext->Unmap(g_pSceneBuffer, 0);
    }

    // Draw the cube
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_pContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pContext->IASetInputLayout(g_pInputLayout);
    g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pContext->RSSetState(g_pRasterizerState);
    g_pContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pContext->PSSetShader(g_pPixelShader, nullptr, 0);

    ID3D11Buffer* vsCBuffers[] = { g_pModelBuffer, g_pSceneBuffer };
    g_pContext->VSSetConstantBuffers(0, _countof(vsCBuffers), vsCBuffers);

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
