// coop_menu.cpp -- [R10-MENU] In-game CO-OP Menu (F9) + Title-Screen CO-OP Button.
//
// Matches dollsMP's dmp_menu.h flow (Host / Join, Steam P2P / LAN UDP, Steam Identity
// Game vs Spacewar 480 + Restart-as-AppID, Password, Server IP, Port, Connect/Disconnect),
// rendered natively via UE5's AHUD::ReceiveDrawHUD (works on DX12 with zero swapchain hooks)
// and unhides WBP_MGT_MainScreen.WBP_NBMM_Coop ("CO-OP (EXPERIMENTAL)") on the title screen.
//
// Kill switch: MENU_OFF.txt (Toggles::menuEnabled, default ON).

#include "ms2_ue.h"
#include "coop_state.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace coop {
namespace menu {

namespace {

using ue::UObject;
using ue::UFunction;

bool g_open = false;
bool g_hudHooked = false;
bool g_coopBtnShown = false;
int  g_sel = 0;        // 0=Network, 1=Identity, 2=Password, 3=Server(LAN), 4=Port(LAN), 5=Host, 6=Join, 7=Disconnect, 8=RestartApp
bool g_editing = false;
bool g_prevKeys[256] = {};
std::string g_feedback;
DWORD g_feedbackUntil = 0;

struct FLinearColor { float R, G, B, A; };

SHORT PollKeyState(int vk) {
    using Fn = SHORT (WINAPI *)(int);
    static Fn s_fn = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (!u32) u32 = LoadLibraryW(L"user32.dll");
        if (u32) s_fn = reinterpret_cast<Fn>(GetProcAddress(u32, "GetAsyncKeyState"));
    }
    return s_fn ? s_fn(vk) : 0;
}

bool IsProcessForeground() {
    using GetFgFn = HWND (WINAPI *)();
    using GetPidFn = DWORD (WINAPI *)(HWND, LPDWORD);
    static GetFgFn s_fg = nullptr;
    static GetPidFn s_pid = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (!u32) u32 = LoadLibraryW(L"user32.dll");
        if (u32) {
            s_fg = reinterpret_cast<GetFgFn>(GetProcAddress(u32, "GetForegroundWindow"));
            s_pid = reinterpret_cast<GetPidFn>(GetProcAddress(u32, "GetWindowThreadProcessId"));
        }
    }
    if (!s_fg || !s_pid) return true;
    HWND hwnd = s_fg();
    if (!hwnd) return false;
    DWORD pid = 0;
    s_pid(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

bool KeyJustPressed(int vk) {
    const bool down = (PollKeyState(vk) & 0x8000) != 0;
    const bool prev = g_prevKeys[vk & 255];
    g_prevKeys[vk & 255] = down;
    return down && !prev;
}

void SetFeedback(const std::string& msg) {
    g_feedback = msg;
    g_feedbackUntil = GetTickCount() + 8000u;
}

void DrawRect(UObject* hud, float x, float y, float w, float h, FLinearColor col) {
    static UFunction* s_fn = nullptr;
    if (!s_fn) s_fn = ue::FindFunction(hud, "DrawRect");
    if (!s_fn) return;
    ue::Params p(s_fn);
    p.Set<FLinearColor>("RectColor", col);
    p.Set<float>("ScreenX", x);
    p.Set<float>("ScreenY", y);
    p.Set<float>("ScreenW", w);
    p.Set<float>("ScreenH", h);
    ue::Call(hud, s_fn, p.data());
}

void DrawText(UObject* hud, const std::string& text, float x, float y, FLinearColor col, float scale = 1.0f) {
    static UFunction* s_fn = nullptr;
    if (!s_fn) s_fn = ue::FindFunction(hud, "DrawText");
    if (!s_fn) return;
    std::wstring w(text.begin(), text.end());
    w.push_back(L'\0');
    ue::FString fs;
    fs.Data = &w[0];
    fs.Num = (int32_t)w.size();
    fs.Max = (int32_t)w.size();
    ue::Params p(s_fn);
    p.Set<ue::FString>("Text", fs);
    p.Set<FLinearColor>("TextColor", col);
    p.Set<float>("ScreenX", x);
    p.Set<float>("ScreenY", y);
    p.Set<UObject*>("Font", nullptr);
    p.Set<float>("Scale", scale);
    p.Set<bool>("bScalePosition", false);
    ue::Call(hud, s_fn, p.data());
}

void HandleInput() {
    if (!IsProcessForeground()) return;
    if (KeyJustPressed(VK_F9)) {
        g_open = !g_open;
        g_editing = false;
        return;
    }
    if (!g_open) return;

    steam::Config cfg = steam::GetConfig();

    if (KeyJustPressed(VK_ESCAPE)) {
        if (g_editing) g_editing = false;
        else g_open = false;
        return;
    }

    if (g_editing) {
        std::string* field = nullptr;
        if (g_sel == 2) field = &cfg.password;
        else if (g_sel == 3) field = &cfg.server;
        if (field) {
            if (KeyJustPressed(VK_RETURN)) {
                g_editing = false;
                steam::SaveConfig(cfg);
                return;
            }
            if (KeyJustPressed(VK_BACK) && !field->empty()) {
                field->pop_back();
                steam::SaveConfig(cfg);
                return;
            }
            const bool shift = (PollKeyState(VK_SHIFT) & 0x8000) != 0;
            for (int vk = '0'; vk <= '9'; ++vk) {
                if (KeyJustPressed(vk) && field->size() < 48) {
                    field->push_back((char)vk);
                    steam::SaveConfig(cfg);
                }
            }
            for (int vk = 'A'; vk <= 'Z'; ++vk) {
                if (KeyJustPressed(vk) && field->size() < 48) {
                    field->push_back(shift ? (char)vk : (char)(vk + 32));
                    steam::SaveConfig(cfg);
                }
            }
            if (KeyJustPressed(VK_OEM_PERIOD) && field->size() < 48) { field->push_back('.'); steam::SaveConfig(cfg); }
            if (KeyJustPressed(VK_OEM_MINUS) && field->size() < 48) { field->push_back(shift ? '_' : '-'); steam::SaveConfig(cfg); }
        }
        return;
    }

    if (KeyJustPressed(VK_UP))   { g_sel = (g_sel + 8) % 9; return; }
    if (KeyJustPressed(VK_DOWN)) { g_sel = (g_sel + 1) % 9; return; }

    const bool left  = KeyJustPressed(VK_LEFT);
    const bool right = KeyJustPressed(VK_RIGHT);
    const bool enter = KeyJustPressed(VK_RETURN);

    if (g_sel == 0 && (left || right || enter)) {
        cfg.network = cfg.network ? 0 : 1;
        steam::SaveConfig(cfg);
    } else if (g_sel == 1 && (left || right || enter)) {
        cfg.steamIdentity = cfg.steamIdentity ? 0 : 1;
        steam::SaveConfig(cfg);
    } else if ((g_sel == 2 || g_sel == 3) && enter) {
        g_editing = true;
    } else if (g_sel == 4 && (left || right)) {
        int p = (int)cfg.port + (right ? 1 : -1);
        if (p < 1024) p = 1024;
        if (p > 65535) p = 65535;
        cfg.port = (uint16_t)p;
        steam::SaveConfig(cfg);
    } else if (g_sel == 5 && enter) {
        std::string err;
        if (steam::StartSession(true, &err)) SetFeedback("Hosting started...");
        else SetFeedback(err);
    } else if (g_sel == 6 && enter) {
        std::string err;
        if (steam::StartSession(false, &err)) SetFeedback("Joining session...");
        else SetFeedback(err);
    } else if (g_sel == 7 && enter) {
        steam::StopSession();
        state().peer.present = false;
        state().peer.live = false;
        SetFeedback("Disconnected.");
    } else if (g_sel == 8 && enter) {
        const uint32_t targetApp = (cfg.steamIdentity == 1) ? 480u : 2584270u;
        std::string err;
        if (steam::RestartWithAppId(targetApp, &err)) {
            SetFeedback("Relaunching with new Steam AppID...");
            ExitProcess(0);
        } else {
            SetFeedback(err);
        }
    }
}

void OnDrawHUD(UObject* hud, UFunction*, void*) {
    if (!state().toggles.menuEnabled || !hud) return;

    // Small always-visible status badge in top-left when connected or waiting
    const int st = steam::State();
    if (!g_open) {
        if (st != steam::kStateIdle) {
            const FLinearColor bg{0.06f, 0.08f, 0.11f, 0.78f};
            const FLinearColor fg = (st == steam::kStateConnected) ? FLinearColor{0.25f, 0.92f, 0.55f, 1.0f}
                                  : (st == steam::kStateError)     ? FLinearColor{0.95f, 0.35f, 0.35f, 1.0f}
                                                                   : FLinearColor{0.95f, 0.80f, 0.30f, 1.0f};
            DrawRect(hud, 16.0f, 16.0f, 420.0f, 28.0f, bg);
            DrawText(hud, "[MS2 CO-OP | F9] " + steam::StatusText(), 24.0f, 22.0f, fg, 1.0f);
        }
        return;
    }

    const steam::Config cfg = steam::GetConfig();
    const float x = 40.0f, y = 40.0f, w = 520.0f, h = 360.0f;
    DrawRect(hud, x, y, w, h, FLinearColor{0.05f, 0.06f, 0.09f, 0.92f});
    DrawRect(hud, x, y, w, 34.0f, FLinearColor{0.12f, 0.16f, 0.22f, 0.98f});
    DrawText(hud, "MORTAL SHELL II CO-OP  [F9 CLOSE | ARROWS + ENTER]", x + 14.0f, y + 9.0f,
             FLinearColor{0.90f, 0.85f, 0.65f, 1.0f}, 1.05f);

    const std::string rows[9] = {
        std::string("Network:        < ") + (cfg.network == 0 ? "STEAM P2P" : "LAN UDP") + " >",
        std::string("Steam Identity: < ") + (cfg.steamIdentity == 0 ? "MORTAL SHELL II" : "SPACEWAR (480)") + " >",
        std::string("Co-op Password: ") + (cfg.password.empty() ? "(empty - press Enter)" : cfg.password) + (g_editing && g_sel == 2 ? "_" : ""),
        std::string("LAN Server IP:  ") + cfg.server + (g_editing && g_sel == 3 ? "_" : ""),
        std::string("LAN Port:       < ") + std::to_string(cfg.port) + " >",
        "[ START HOST ]",
        "[ JOIN SESSION ]",
        "[ DISCONNECT ]",
        std::string("[ RESTART AS ") + (cfg.steamIdentity == 1 ? "SPACEWAR (480)" : "MORTAL SHELL II") + " ]"
    };

    for (int i = 0; i < 9; ++i) {
        const float ry = y + 48.0f + i * 28.0f;
        if (i == g_sel) {
            DrawRect(hud, x + 10.0f, ry - 2.0f, w - 20.0f, 24.0f, FLinearColor{0.18f, 0.36f, 0.45f, 0.85f});
        }
        const FLinearColor tc = (i == g_sel) ? FLinearColor{1.0f, 1.0f, 1.0f, 1.0f}
                                             : FLinearColor{0.82f, 0.85f, 0.88f, 1.0f};
        DrawText(hud, rows[i], x + 20.0f, ry + 2.0f, tc, 1.0f);
    }

    const float sy = y + h - 54.0f;
    DrawRect(hud, x + 10.0f, sy, w - 20.0f, 44.0f, FLinearColor{0.08f, 0.10f, 0.14f, 0.95f});
    DrawText(hud, "Status: " + steam::StatusText(), x + 18.0f, sy + 6.0f,
             FLinearColor{0.45f, 0.90f, 0.70f, 1.0f}, 0.95f);
    if (!g_feedback.empty() && GetTickCount() < g_feedbackUntil) {
        DrawText(hud, g_feedback, x + 18.0f, sy + 24.0f, FLinearColor{0.95f, 0.80f, 0.35f, 1.0f}, 0.9f);
    }
}

// Unhide WBP_MGT_MainScreen.WBP_NBMM_Coop ("CO-OP (EXPERIMENTAL)") on the title screen
void UnhideTitleCoopButton() {
    if (g_coopBtnShown) return;
    for (UObject* scr : ue::FindAllOf("WBP_MGT_MainScreen_C")) {
        if (!scr || !ue::IsValid(scr)) continue;
        UObject* btn = nullptr;
        if (ue::Get<UObject*>(scr, "WBP_NBMM_Coop", btn) && btn && ue::IsValid(btn)) {
            if (UFunction* fnVis = ue::FindFunction(btn, "SetVisibility")) {
                ue::Params p(fnVis);
                p.Set<uint8_t>("InVisibility", 0); // ESlateVisibility::Visible
                ue::Call(btn, fnVis, p.data());
                g_coopBtnShown = true;
                net::Log("menu: unhid WBP_MGT_MainScreen.WBP_NBMM_Coop");
            }
        }
    }
}

}  // namespace

void Tick30() {
    if (!state().toggles.menuEnabled) return;
    HandleInput();
}

void Tick2() {
    if (!state().toggles.menuEnabled) return;
    if (!g_hudHooked) {
        if (ue::HookPost("HUD.ReceiveDrawHUD", &OnDrawHUD)) {
            g_hudHooked = true;
            net::Log("menu: hooked HUD.ReceiveDrawHUD (press F9 for CO-OP menu)");
        }
    }
    if (state().puppet.phase == Phase::Menu) {
        UnhideTitleCoopButton();
    }
}

void Reset() {
    g_coopBtnShown = false;
}

bool IsOpen() { return g_open; }

}  // namespace menu
}  // namespace coop
