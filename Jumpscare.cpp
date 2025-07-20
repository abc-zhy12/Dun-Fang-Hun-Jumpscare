#include <Windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <gdiplus.h>
#include <string.h>
#include <wchar.h>
#include <vector>
#include <cstdio>
#include <ctime>
#include <cstdlib>

#define UNICODE
#define _UNICODE

#undef WINVER
#undef _WIN32_WINNT
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

HINSTANCE g_hInstance = NULL;
HWND g_hMainWnd = NULL;
UINT g_hotkeyId = VK_F1;
int g_currentImageIndex = 0;
bool g_isAnimating = false;
HWND g_hAnimWnd = NULL;
std::vector<Gdiplus::Image*> g_images;
ULONG_PTR g_gdiplusToken = 0;
NOTIFYICONDATAW nid = {};
const UINT WM_TRAYICON = WM_USER + 1;
DWORD g_animationStartTime = 0;

const wchar_t g_szWindowClass[] = L"JumpscareWindowClass";
const wchar_t g_szTitle[] = L"Jumpscare";
const wchar_t g_soundFile[] = L"00.wav";
const double FADE_IN_DURATION = 350.0;  // 0.35 seconds
const double FADE_OUT_DURATION = 150.0; // 0.15 seconds
const int FRAME_DELAY = 10; // 100 FPS

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AnimWndProc(HWND, UINT, WPARAM, LPARAM);
void RegisterHotkey();
void UnregisterHotkey();
void CreateTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void PlayAnimation();
void LoadImages();
void CleanupImages();
void PlaySoundEffect();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    InitCommonControls();
    
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) != Ok)
    {
        return 1;
    }

    g_hInstance = hInstance;

    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = g_szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex))
    {
        return 0;
    }

    g_hMainWnd = CreateWindowExW(
        0, g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 200,
        NULL, NULL, hInstance, NULL);

    if (!g_hMainWnd)
    {
        return 0;
    }

    WNDCLASSEXW animWcex = {0};
    animWcex.cbSize = sizeof(WNDCLASSEXW);
    animWcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    animWcex.lpfnWndProc = AnimWndProc;
    animWcex.hInstance = hInstance;
    animWcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    animWcex.lpszClassName = L"JumpscareAnimClass";
    animWcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassExW(&animWcex))
    {
        return 0;
    }

    LoadImages();
    
    // Create tray icon
    CreateTrayIcon(g_hMainWnd);
    
    // Hide main window
    ShowWindow(g_hMainWnd, SW_HIDE);
    
    // Register F1 as hotkey
    RegisterHotkey();

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupImages();
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_HOTKEY:
        {
            if (wParam == g_hotkeyId && !g_isAnimating)
            {
                PlayAnimation();
            }
            break;
        }

        case WM_DESTROY:
        {
            RemoveTrayIcon();
            UnregisterHotkey();
            if (g_hAnimWnd) 
            {
                DestroyWindow(g_hAnimWnd);
                g_hAnimWnd = NULL;
            }
            PostQuitMessage(0);
            break;
        }

        case WM_TRAYICON:
        {
            switch (lParam)
            {
                case WM_RBUTTONUP:
                {
                    POINT pt;
                    GetCursorPos(&pt);

                    // Create menu with only Exit option
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 1000, L"Exit");

                    SetForegroundWindow(hWnd);
                    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    break;
                }
            }
            break;
        }

        case WM_COMMAND:
        {
            // Only handle Exit command (ID 1000)
            if (LOWORD(wParam) == 1000)
            {
                DestroyWindow(hWnd);
            }
            break;
        }

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK AnimWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int shakeX = 0, shakeY = 0;
    static HDC hdcMem = NULL;
    static HBITMAP hbmMem = NULL;
    static HGDIOBJ hOld = NULL;

    switch (message)
    {
        case WM_CREATE:
        {
            // Create double buffering resources
            HDC hdc = GetDC(hWnd);
            hdcMem = CreateCompatibleDC(hdc);
            hbmMem = CreateCompatibleBitmap(hdc, 
                GetSystemMetrics(SM_CXSCREEN), 
                GetSystemMetrics(SM_CYSCREEN));
            hOld = SelectObject(hdcMem, hbmMem);
            ReleaseDC(hWnd, hdc);
            
            // Setup window properties
            SetWindowLong(hWnd, GWL_EXSTYLE, 
                GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
            
            // Cover entire screen
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 
                         GetSystemMetrics(SM_CXSCREEN), 
                         GetSystemMetrics(SM_CYSCREEN), 
                         SWP_SHOWWINDOW);
            return 0;
        }

        case WM_DESTROY:
        {
            // Clean up double buffering resources
            if (hdcMem)
            {
                SelectObject(hdcMem, hOld);
                DeleteObject(hbmMem);
                DeleteDC(hdcMem);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            if (!g_images.empty() && hdcMem)
            {
                // Clear back buffer
                RECT rc;
                GetClientRect(hWnd, &rc);
                FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
                
                Graphics graphics(hdcMem);
                Image* currentImage = g_images[g_currentImageIndex];
                
                // Apply shaking effect
                graphics.TranslateTransform(static_cast<REAL>(shakeX), static_cast<REAL>(shakeY));
                
                // Draw image stretched to full screen
                graphics.DrawImage(
                    currentImage, 
                    0, 
                    0, 
                    rc.right - rc.left, 
                    rc.bottom - rc.top
                );
                
                // Copy back buffer to screen
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
            }
            
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_TIMER:
        {
            DWORD currentTime = GetTickCount();
            double elapsed = static_cast<double>(currentTime - g_animationStartTime);
            
            BYTE alpha = 255;
            if (elapsed < FADE_IN_DURATION)
            {
                // Fade in
                alpha = static_cast<BYTE>((elapsed / FADE_IN_DURATION) * 255);
            }
            else if (elapsed < (FADE_IN_DURATION + FADE_OUT_DURATION))
            {
                // Fade out
                double fadeOutPos = (elapsed - FADE_IN_DURATION) / FADE_OUT_DURATION;
                alpha = static_cast<BYTE>((1.0 - fadeOutPos) * 255);
            }
            else
            {
                // Animation complete
                KillTimer(hWnd, 1);
                ShowWindow(hWnd, SW_HIDE);
                g_isAnimating = false;
                return 0;
            }
            
            SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
            
            // Generate random shake [-50, 50]
            shakeX = (rand() % 101) - 50;
            shakeY = (rand() % 101) - 50;
            
            // Request redraw
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

// Create tray icon
void CreateTrayIcon(HWND hWnd)
{
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    
    nid.hIcon = (HICON)LoadImageW(
        NULL, 
        MAKEINTRESOURCEW(32515), // IDI_WARNING resource ID
        IMAGE_ICON, 
        0, 
        0, 
        LR_SHARED | LR_DEFAULTSIZE
    );
    
    // Set tooltip text
    wcsncpy(nid.szTip, L"Jumpscare - Press F1 to activate", sizeof(nid.szTip)/sizeof(WCHAR) - 1);
    nid.szTip[sizeof(nid.szTip)/sizeof(WCHAR) - 1] = L'\0';

    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Register F1 as hotkey
void RegisterHotkey()
{
    // Register F1 without modifiers
    if (!RegisterHotKey(g_hMainWnd, g_hotkeyId, 0, VK_F1))
    {
        MessageBoxW(NULL, L"Failed to register F1 hotkey", L"Error", MB_ICONERROR);
    }
}

void UnregisterHotkey()
{
    UnregisterHotKey(g_hMainWnd, g_hotkeyId);
}

void LoadImages()
{
    CleanupImages();
    
    int index = 1;
    while (true)
    {
        wchar_t filename[MAX_PATH];
        wsprintfW(filename, L"%02d.png", index++);
        
        if (GetFileAttributesW(filename) == INVALID_FILE_ATTRIBUTES)
            break;
            
        Image* img = new Image(filename);
        if (img->GetLastStatus() == Ok)
        {
            g_images.push_back(img);
        }
        else
        {
            delete img;
            break;
        }
    }
    
    // Fallback if no images found
    if (g_images.empty())
    {
        Bitmap* bmp = new Bitmap(400, 400);
        Graphics graphics(bmp);
        graphics.Clear(Color(255, 255, 0, 0));
        g_images.push_back(bmp);
    }
}

void CleanupImages()
{
    for (auto img : g_images)
    {
        delete img;
    }
    g_images.clear();
}

void PlaySoundEffect()
{
  /*
  wchar_t absPath[MAX_PATH] = {0};

  // Get the EXE's full path
  DWORD pathLen = GetModuleFileNameW(NULL, absPath, MAX_PATH);
  if (pathLen == 0 || pathLen >= MAX_PATH)
  {
    // Try to play the file in the current dir when failed
    PlaySoundW(g_soundFile, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    return;
  }

  // Look for the last backslash
  wchar_t* lastBackslash = wcsrchr(absPath, L'\\');
  if (lastBackslash == NULL)
  {
    // Directly play if there's no path separator
    PlaySoundW(g_soundFile, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    return;
  }

  // Combine the paths
  size_t remaining = MAX_PATH - (lastBackslash - absPath ) - 1;
  if (wcslen(g_soundFile) >= remaining)
  {
    // Long path handler
    PlaySoundW(g_soundFile, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    return;
  }

  // Truncate path & Combine file name
  *(lastBackslash + 1) = L'\0';

  // Play
  PlaySoundW(absPath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  */

  PlaySoundW(g_soundFile, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void PlayAnimation()
{
    if (g_images.empty() || g_isAnimating)
        return;
    
    g_isAnimating = true;
    g_currentImageIndex = (g_currentImageIndex + 1) % g_images.size();
    
    // Play sound effect
    PlaySoundEffect();
    
    if (!g_hAnimWnd)
    {
        g_hAnimWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            L"JumpscareAnimClass", NULL, WS_POPUP,
            0, 0, 0, 0,
            NULL, NULL, g_hInstance, NULL);
    }
    
    // Initialize random seed
    srand(static_cast<unsigned int>(GetTickCount()));
    
    // Set global animation start time
    g_animationStartTime = GetTickCount();
    
    // Reset window properties
    SetWindowLong(g_hAnimWnd, GWL_EXSTYLE, 
                 GetWindowLong(g_hAnimWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_hAnimWnd, 0, 0, LWA_ALPHA);
    
    // Show and position window
    ShowWindow(g_hAnimWnd, SW_SHOW);
    SetWindowPos(g_hAnimWnd, HWND_TOPMOST, 0, 0, 
                 GetSystemMetrics(SM_CXSCREEN), 
                 GetSystemMetrics(SM_CYSCREEN), 
                 SWP_SHOWWINDOW);
    
    // Force redraw
    InvalidateRect(g_hAnimWnd, NULL, TRUE);
    UpdateWindow(g_hAnimWnd);
    
    // Set high frame rate timer
    KillTimer(g_hAnimWnd, 1);
    SetTimer(g_hAnimWnd, 1, FRAME_DELAY, NULL);
}