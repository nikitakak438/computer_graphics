// main.cpp
// Пример: минимальное оконное приложение DirectX 11
// - Инициализация DirectX11 (с Debug-слоем в Debug конфигурации)
// - Очистка back-buffer фиксированным цветом каждый кадр
// - Обработка изменения размера окна (WM_SIZE -> ResizeBuffers + пересоздание RTV)
// - Корректная очистка и проверка live-объектов через DXGI debug интерфейс
//
// Требования: Visual Studio 2019/2022, платформа x64, Debug/Release
//
// Сборка: добавить main.cpp в Empty Project (C++), установить платформу x64.
// В Debug конфигурации должны быть установлены Graphics Tools (Optional Feature) для работы debug-слоя.

#define _WIN32_WINNT 0x0602

#include <dxgi1_3.h>
#include <windows.h>
#include <tchar.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>
#include <cassert>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib") // для DXGI debug

// Макросы для удобства
#define SAFE_RELEASE(p) do { if ((p) != nullptr) { (p)->Release(); (p) = nullptr; } } while(0)

using std::wstring;

// Глобальные переменные (для простоты примера)
HINSTANCE           g_hInst = nullptr;
HWND                g_hWnd = nullptr;
UINT                g_ClientWidth = 1280;
UINT                g_ClientHeight = 720;

IDXGISwapChain1* g_pSwapChain = nullptr; // используем IDXGISwapChain1 если возможен
ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
ID3D11RenderTargetView* g_pRTV = nullptr;
bool                g_bInitialized = false;

// Прототипы
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow);
HRESULT InitD3D();
void Render();
void Cleanup();
void OnResize(UINT width, UINT height);
IDXGIAdapter1* GetHardwareAdapter(IDXGIFactory1* pFactory);

// Утилита для проверки HRESULT
inline void HR_CHECK(HRESULT hr, const char* msg = nullptr)
{
    if (FAILED(hr))
    {
        if (msg) OutputDebugStringA(msg);
        assert(false && "HRESULT failed - check debug output");
        // В релизе можно обработать иначе
    }
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    g_hInst = hInstance;

    // Регистрация и создание окна
    if (FAILED(InitWindow(hInstance, nCmdShow)))
        return 0;

    // Инициализация DirectX
    if (FAILED(InitD3D()))
    {
        MessageBox(nullptr, _T("Ошибка инициализации DirectX 11"), _T("Ошибка"), MB_OK | MB_ICONERROR);
        Cleanup();
        return 0;
    }
    g_bInitialized = true;

    // Главный цикл (поддерживает обновление вне зависимости от сообщений)
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        // Обработка сообщений
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // Рисуем кадр
            Render();
        }
    }

    // Очистка ресурсов
    Cleanup();

    // После полного Release device/context вызовем DXGI debug report
#ifdef _DEBUG
    {
        IDXGIDebug1* dxgiDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
        {
            // Покажет все live-объекты (должны быть 0)
            dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
            dxgiDebug->Release();
        }
    }
#endif

    return (int)msg.wParam;
}

// Инициализация окна
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    // Регистрация класса окна
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

    // Рассчитываем размеры окна под требуемую клиентскую область
    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindow(
        wc.lpszClassName,
        L"DX11 - Clear Color + Resize (x64)",
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

// Выбор аппаратного адаптера (не Microsoft Basic Render Driver). Возвращает адаптер с +1 ссылкой.
IDXGIAdapter1* GetHardwareAdapter(IDXGIFactory1* pFactory)
{
    if (!pFactory) return nullptr;
    IDXGIAdapter1* adapter = nullptr;
    IDXGIAdapter1* chosen = nullptr;
    for (UINT i = 0; SUCCEEDED(pFactory->EnumAdapters1(i, &adapter)); ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        // Пропускаем Microsoft Basic Render Driver
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            chosen = adapter; // передаем владение
            break;
        }
        SAFE_RELEASE(adapter);
    }
    return chosen; // может быть nullptr
}

// Инициализация D3D11 + SwapChain
HRESULT InitD3D()
{
    HRESULT hr = S_OK;

    // Создание D3D11 device и context
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Попробуем выбрать аппаратный адаптер через DXGI
    IDXGIFactory1* pFactory1 = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory1);
    HR_CHECK(hr, "CreateDXGIFactory1 failed\n");

    IDXGIAdapter1* adapter = GetHardwareAdapter(pFactory1);

    // Создаём устройство (если adapter != nullptr, указываем DRIVER_TYPE_UNKNOWN)
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
        // Попытка fallback на WARP если не удалось
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

    // Получаем IDXGIFactory2 если возможно - для CreateSwapChainForHwnd
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

    // Отключаем ALT+ENTER (чтобы DXGI не оставлял ссылки на window mode)
    pFactory2->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER);

    // Настройка swap chain (Flip-discard recommended, если поддерживается)
    DXGI_SWAP_CHAIN_DESC1 scd1 = {};
    scd1.Width = g_ClientWidth;
    scd1.Height = g_ClientHeight;
    scd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd1.SampleDesc.Count = 1;
    scd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd1.BufferCount = 2; // двойная буферизация для flip
    scd1.Scaling = DXGI_SCALING_STRETCH;
    scd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd1.Flags = 0;

    IDXGISwapChain1* swapChain1 = nullptr;
    hr = pFactory2->CreateSwapChainForHwnd(g_pDevice, g_hWnd, &scd1, nullptr, nullptr, &swapChain1);
    if (FAILED(hr))
    {
        // fallback: попытка CreateSwapChain (старый метод) - если CreateSwapChainForHwnd недоступен
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
        // Получим legacy factory
        hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactoryLegacy);
        if (SUCCEEDED(hr))
        {
            hr = pFactoryLegacy->CreateSwapChain(g_pDevice, &sd, (IDXGISwapChain**)&swapChain1);
            SAFE_RELEASE(pFactoryLegacy);
        }
        HR_CHECK(hr, "CreateSwapChain fallback failed\n");
    }

    // Присваиваем глобальному указателю (как IDXGISwapChain1)
    g_pSwapChain = swapChain1; // swapChain1 может быть nullptr, тогда ошибка уже взята выше

    // Создаем Render Target View
    // Получаем back buffer
    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    HR_CHECK(hr, "GetBuffer failed\n");

    hr = g_pDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
    SAFE_RELEASE(backBuffer);
    HR_CHECK(hr, "CreateRenderTargetView failed\n");

    // Устанавливаем viewport сразу
    D3D11_VIEWPORT vp;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_ClientWidth;
    vp.Height = (FLOAT)g_ClientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pContext->RSSetViewports(1, &vp);

    // Освободим factory2
    SAFE_RELEASE(pFactory2);
    SAFE_RELEASE(pFactory1);
    SAFE_RELEASE(adapter);

    return S_OK;
}

// Функция-resize (ресайз буферов swap chain)
void OnResize(UINT width, UINT height)
{
    if (!g_pSwapChain || !g_pDevice || !g_pContext) return;

    if (width == 0 || height == 0) return; // свернуто или нулевой размер

    // Обновим глобальные размеры
    g_ClientWidth = width;
    g_ClientHeight = height;

    // Сброс состояния контекста и отвязка RT
    g_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    // Освобождаем RTV перед ResizeBuffers
    SAFE_RELEASE(g_pRTV);

    // Попытка ResizeBuffers (0,0,...) чтобы оставить формат
    HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        // Можно логировать и пробовать восстановление; в учебном примере завершаем assert
        HR_CHECK(hr, "ResizeBuffers failed\n");
    }

    // Получаем новый backbuffer и создаём RTV
    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    HR_CHECK(hr, "GetBuffer after ResizeBuffers failed\n");

    hr = g_pDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
    SAFE_RELEASE(backBuffer);
    HR_CHECK(hr, "CreateRenderTargetView after resize failed\n");

    // Обновим viewport
    D3D11_VIEWPORT vp;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_ClientWidth;
    vp.Height = (FLOAT)g_ClientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pContext->RSSetViewports(1, &vp);
}

// Рисование кадра
void Render()
{
    if (!g_pContext || !g_pRTV || !g_pSwapChain) return;

    // Привязываем рендер-таргет
    g_pContext->OMSetRenderTargets(1, &g_pRTV, nullptr);

    // Очистка цветом (фиксированный цвет)
    const FLOAT clearColor[4] = { 0.15f, 0.45f, 0.75f, 1.0f }; // можно поменять
    g_pContext->ClearRenderTargetView(g_pRTV, clearColor);

    // В данном задании рисования нет — только очистка экрана

    // Present (1 = VSync on)
    HRESULT hr = g_pSwapChain->Present(1, 0);
    if (FAILED(hr))
    {
        // В реальном приложении стоит обрабатывать DEVICE_REMOVED и т.д.
        HR_CHECK(hr, "Present failed\n");
    }
}

// Очистка ресурсов (правильный порядок)
void Cleanup()
{
    // Сброс состояния и сброс привязок
    if (g_pContext)
    {
        g_pContext->ClearState();
        g_pContext->Flush();
    }

    // Освобождаем представления и swap chain
    SAFE_RELEASE(g_pRTV);

    // Прежде чем освобождать swap chain, можно удалить привязки
    SAFE_RELEASE(g_pSwapChain);

    // Освобождаем контекст (перед ReportLiveObjects)
    SAFE_RELEASE(g_pContext);

    // Освобождаем device (report будет вызван в WinMain через DXGIGetDebugInterface1 после Cleanup)
    SAFE_RELEASE(g_pDevice);
}

// Обработчик сообщений окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        // Игнорируем WM_SIZE до инициализации D3D
        if (!g_bInitialized) break;

        if (wParam == SIZE_MINIMIZED)
        {
            // можно ничего не делать
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