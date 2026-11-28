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

#include "MinHook.h"

#include <algorithm>
#include <atomic>
#include <cmath>
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
unsigned g_drawCount = 0;   // ReceiveDrawHUD calls seen (diagnostic)
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

// ---------------------------------------------------------------------------
// Drawing primitives (AHUD::DrawRect / DrawText / GetTextSize through ProcessEvent)
// ---------------------------------------------------------------------------
void DrawRect(UObject* hud, float x, float y, float w, float h, FLinearColor col) {
    static UFunction* s_fn = nullptr;
    if (!s_fn) s_fn = ue::FindFunction(hud, "DrawRect");
    if (!s_fn || w <= 0.0f || h <= 0.0f) return;
    ue::Params p(s_fn);
    p.Set<FLinearColor>("RectColor", col);
    p.Set<float>("ScreenX", x);
    p.Set<float>("ScreenY", y);
    p.Set<float>("ScreenW", w);
    p.Set<float>("ScreenH", h);
    ue::Call(hud, s_fn, p.data());
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

ue::FString MakeFString(std::wstring& buf) {   // buf must outlive the call
    buf.push_back(L'\0');
    ue::FString fs;
    fs.Data = &buf[0];
    fs.Num = (int32_t)buf.size();
    fs.Max = (int32_t)buf.size();
    return fs;
}

void DrawTextRaw(UObject* hud, const std::string& text, float x, float y, FLinearColor col,
                 UObject* font, float scale) {
    static UFunction* s_fn = nullptr;
    if (!s_fn) s_fn = ue::FindFunction(hud, "DrawText");
    if (!s_fn || text.empty()) return;
    std::wstring w = Widen(text);
    ue::Params p(s_fn);
    p.Set<ue::FString>("Text", MakeFString(w));
    p.Set<FLinearColor>("TextColor", col);
    p.Set<float>("ScreenX", x);
    p.Set<float>("ScreenY", y);
    p.Set<UObject*>("Font", font);
    p.Set<float>("Scale", scale);
    p.Set<bool>("bScalePosition", false);
    ue::Call(hud, s_fn, p.data());
}

void MeasureRaw(UObject* hud, const std::string& text, UObject* font, float scale, float* outW, float* outH) {
    static UFunction* s_fn = nullptr;
    if (!s_fn) s_fn = ue::FindFunction(hud, "GetTextSize");
    *outW = 0.0f; *outH = 0.0f;
    if (!s_fn || text.empty()) return;
    std::wstring w = Widen(text);
    ue::Params p(s_fn);
    p.Set<ue::FString>("Text", MakeFString(w));
    p.Set<UObject*>("Font", font);
    p.Set<float>("Scale", scale);
    ue::Call(hud, s_fn, p.data());
    *outW = p.Get<float>("OutWidth");
    *outH = p.Get<float>("OutHeight");
}

// ---------------------------------------------------------------------------
// The game's own UI fonts (Sparta/UI/Fonts): Trajan Pro for headings and
// buttons, Crimson Text for body copy -- the faces the inventory, journal and
// title screens use.  Found with ONE FindAllOf("Font") scan (a FindObject miss
// walks the whole object array), retried rarely until they are loaded.  A
// missing face falls back to the engine font (nullptr).
// ---------------------------------------------------------------------------
enum FontId { kTrajan = 0, kTrajanBold, kCrimson, kCrimsonItalic, kCrimsonSemi, kFontCount };
const char* const kFontNames[kFontCount] = {
    "Trajan_Pro_Regular_Font",
    "Trajan_Pro_SemiBold_Font",
    "CrimsonText-Regular_Font",
    "CrimsonText-Italic_Font",
    "CrimsonText-SemiBold_Font",
};
UObject* g_fonts[kFontCount] = {};
float    g_fontUnitH[kFontCount] = {};   // measured line height at Scale 1 (0 = not yet)
DWORD    g_fontNextScan = 0;
bool     g_wantFonts = false;          // set once something is drawn
int      g_fontScans = 0;

void ScanFonts() {
    bool missing = false;
    for (int i = 0; i < kFontCount; ++i) {
        if (g_fonts[i] && !ue::IsValid(g_fonts[i])) { g_fonts[i] = nullptr; g_fontUnitH[i] = 0.0f; }
        if (!g_fonts[i]) missing = true;
    }
    if (!missing || g_fontScans >= 8) return;
    const DWORD now = GetTickCount();
    if (g_fontScans > 0 && (int)(now - g_fontNextScan) < 0) return;
    ++g_fontScans;
    g_fontNextScan = now + 20000u;
    int found = 0;
    for (UObject* f : ue::FindAllOf("Font")) {
        const std::string name = ue::GetName(f);
        for (int i = 0; i < kFontCount; ++i) {
            if (!g_fonts[i] && name == kFontNames[i]) { g_fonts[i] = f; g_fontUnitH[i] = 0.0f; }
        }
    }
    for (int i = 0; i < kFontCount; ++i) if (g_fonts[i]) ++found;
    net::Log("menu: game UI fonts %d/%d found (scan %d)", found, (int)kFontCount, g_fontScans);
}

// ---------------------------------------------------------------------------
// Ui: one frame's drawing context.  Every size is in 1080p reference pixels and
// scaled by the viewport height, as the game's UMG DPI curve does.
// ---------------------------------------------------------------------------
namespace pal {
// Sampled from the game's inventory/journal screens.
constexpr FLinearColor kScrim      {0.00f, 0.00f, 0.00f, 0.55f};
constexpr FLinearColor kPanel      {0.016f, 0.014f, 0.012f, 0.90f};
constexpr FLinearColor kRule       {0.55f, 0.49f, 0.38f, 0.55f};
constexpr FLinearColor kRuleFaint  {0.55f, 0.49f, 0.38f, 0.22f};
constexpr FLinearColor kOrnament   {0.78f, 0.68f, 0.48f, 0.95f};
constexpr FLinearColor kTitle      {0.86f, 0.80f, 0.68f, 1.00f};
constexpr FLinearColor kText       {0.80f, 0.76f, 0.68f, 1.00f};
constexpr FLinearColor kTextBright {0.96f, 0.92f, 0.84f, 1.00f};
constexpr FLinearColor kLabel      {0.56f, 0.53f, 0.48f, 1.00f};
constexpr FLinearColor kDim        {0.36f, 0.34f, 0.31f, 1.00f};
constexpr FLinearColor kSelBand    {0.62f, 0.55f, 0.42f, 0.20f};
constexpr FLinearColor kShadow     {0.00f, 0.00f, 0.00f, 0.85f};
constexpr FLinearColor kGood       {0.60f, 0.76f, 0.52f, 1.00f};
constexpr FLinearColor kWarn       {0.86f, 0.72f, 0.45f, 1.00f};
constexpr FLinearColor kBad        {0.82f, 0.40f, 0.33f, 1.00f};
}  // namespace pal

FLinearColor WithAlpha(FLinearColor c, float a) { c.A *= a; return c; }

struct Ui {
    UObject* hud = nullptr;
    float sw = 1920.0f, sh = 1080.0f;   // viewport, pixels
    float s = 1.0f;                     // reference px -> pixels

    bool Begin(UObject* h) {
        hud = h;
        UObject* canvas = nullptr;
        if (!ue::Get<UObject*>(hud, "Canvas", canvas) || !canvas) return false;
        float cx = 0.0f, cy = 0.0f;
        if (ue::PropSize(canvas, "ClipX") == 8) {
            double dx = 0, dy = 0;
            ue::Get<double>(canvas, "ClipX", dx); ue::Get<double>(canvas, "ClipY", dy);
            cx = (float)dx; cy = (float)dy;
        } else {
            ue::Get<float>(canvas, "ClipX", cx); ue::Get<float>(canvas, "ClipY", cy);
        }
        if (cx < 64.0f || cy < 64.0f) return false;
        sw = cx; sh = cy;
        s = sh / 1080.0f;
        if (s < 0.5f) s = 0.5f;
        if (s > 3.0f) s = 3.0f;
        return true;
    }

    float P(float ref) const { return ref * s; }

    void Rect(float x, float y, float w, float h, FLinearColor c) { DrawRect(hud, x, y, w, h, c); }

    // Segment [x0,x1) snapped to whole pixels, so adjacent translucent segments
    // neither overlap (dark seams) nor leave gaps.
    void Span(float x0, float x1, float y, float h, FLinearColor c) {
        const float a = std::floor(x0 + 0.5f), b = std::floor(x1 + 0.5f);
        if (b > a) DrawRect(hud, a, y, b - a, h, c);
    }

    // Hairline that fades out toward both ends, like the game's dividers.
    void FadedRule(float x, float y, float w, FLinearColor c, float thick = 1.0f) {
        const int n = 16;
        const float seg = w / n;
        for (int i = 0; i < n; ++i) {
            const float t = (i + 0.5f) / n;               // 0..1
            const float e = 1.0f - std::fabs(t * 2.0f - 1.0f);
            const float a = e < 0.5f ? e * 2.0f : 1.0f;   // plateau in the middle
            Span(x + i * seg, x + (i + 1) * seg, y, thick, WithAlpha(c, a));
        }
    }

    // Horizontal band, transparent at the edges (selection highlight).
    void FadedBand(float x, float y, float w, float h, FLinearColor c) {
        const int n = 36;
        const float seg = w / n;
        for (int i = 0; i < n; ++i) {
            const float t = (i + 0.5f) / n;
            const float e = 1.0f - std::fabs(t * 2.0f - 1.0f);
            Span(x + i * seg, x + (i + 1) * seg, y, h, WithAlpha(c, std::sqrt(e)));
        }
    }

    // Left-to-right fade (status toast background).
    void FadeRight(float x, float y, float w, float h, FLinearColor c, float solidFrac) {
        const int n = 14;
        const float solid = w * solidFrac;
        Rect(x, y, solid, h, c);
        const float seg = (w - solid) / n;
        for (int i = 0; i < n; ++i)
            Span(x + solid + i * seg, x + solid + (i + 1) * seg, y, h, WithAlpha(c, 1.0f - (i + 0.5f) / n));
    }

    void Frame(float x, float y, float w, float h, FLinearColor c) {
        const float t = 1.0f;
        Rect(x, y, w, t, c);
        Rect(x, y + h - t, w, t, c);
        Rect(x, y + t, t, h - 2 * t, c);
        Rect(x + w - t, y + t, t, h - 2 * t, c);
    }

    // Diamond ornament (the game's divider end-caps and selection marker).
    void Diamond(float cx, float cy, float r, FLinearColor c) {
        const int steps = (int)(r + 0.5f);
        if (steps < 1) { Rect(cx - 1, cy - 1, 2, 2, c); return; }
        for (int i = -steps; i <= steps; ++i) {
            const float half = (float)(steps - std::abs(i));
            Rect(cx - half - 0.5f, cy + i - 0.5f, half * 2.0f + 1.0f, 1.0f, c);
        }
    }

    // Small filled triangle pointing left/right/up/down (keycap arrows).
    void Arrow(float cx, float cy, float r, int dir, FLinearColor c) {
        const int steps = (int)(r + 0.5f);
        for (int i = 0; i <= steps; ++i) {
            const float len = (float)(i * 2 + 1);
            switch (dir) {
                case 0: Rect(cx - steps / 2.0f + i, cy - len / 2, 1, len, c); break;      // left
                case 1: Rect(cx + steps / 2.0f - i, cy - len / 2, 1, len, c); break;      // right
                case 2: Rect(cx - len / 2, cy - steps / 2.0f + i, len, 1, c); break;      // up
                default: Rect(cx - len / 2, cy + steps / 2.0f - i, len, 1, c); break;     // down
            }
        }
    }

    // Ornamental divider: faded rule with a diamond in the middle.
    void Divider(float x, float y, float w) {
        FadedRule(x, y, w, pal::kRule);
        const float cx = x + w * 0.5f;
        Rect(cx - P(14), y - 1, P(28), 3, pal::kShadow);
        Diamond(cx, y, P(4), pal::kOrnament);
    }

    float FontScale(FontId f, float px) {
        UObject* font = g_fonts[f];
        if (g_fontUnitH[f] <= 0.0f) {
            float w = 0, h = 0;
            MeasureRaw(hud, "Mg", font, 1.0f, &w, &h);
            g_fontUnitH[f] = h > 1.0f ? h : 16.0f;
        }
        return P(px) / g_fontUnitH[f];
    }

    float Width(const std::string& t, FontId f, float px) {
        float w = 0, h = 0;
        MeasureRaw(hud, t, g_fonts[f], FontScale(f, px), &w, &h);
        return w;
    }

    // Text with the game's soft drop shadow. align: 0 left, 1 center, 2 right.
    // Returns the drawn width.
    float Text(const std::string& t, float x, float y, FontId f, float px, FLinearColor c, int align = 0,
               bool shadow = true) {
        if (t.empty()) return 0.0f;
        const float sc = FontScale(f, px);
        float w = 0;
        if (align != 0) {
            float h = 0;
            MeasureRaw(hud, t, g_fonts[f], sc, &w, &h);
            x -= (align == 1) ? w * 0.5f : w;
        }
        const float o = std::max(1.0f, P(1.5f));
        if (shadow) DrawTextRaw(hud, t, x + o, y + o, WithAlpha(pal::kShadow, c.A), g_fonts[f], sc);
        DrawTextRaw(hud, t, x, y, c, g_fonts[f], sc);
        return w;
    }

    // A key glyph in a thin box, then its caption.  Returns the x after it.
    // key: text, or "<>" / "^v" for drawn arrow pairs.
    float KeyHint(float x, float cy, const std::string& key, const std::string& caption) {
        // The game's prompts are a filled, pale stone keycap with dark glyphs
        // ("ESC", "Q", "E" on the pause/inventory screens).
        const FLinearColor cap{0.50f, 0.48f, 0.45f, 0.95f};
        const FLinearColor ink{0.06f, 0.055f, 0.05f, 1.0f};
        const float h = P(24);
        float w;
        const float y = cy - h * 0.5f;
        if (key == "<>" || key == "^v") {
            w = P(42);
            Rect(x, y, w, h, cap);
            const int d0 = key == "<>" ? 0 : 2;
            Arrow(x + w * 0.30f, cy, P(5), d0, ink);
            Arrow(x + w * 0.70f, cy, P(5), d0 + 1, ink);
        } else {
            const float tw = Width(key, kCrimsonSemi, 17);
            w = std::max(h, tw + P(12));
            Rect(x, y, w, h, cap);
            Text(key, x + w * 0.5f, cy - P(10.5f), kCrimsonSemi, 17, ink, 1, false);
        }
        x += w + P(9);
        if (caption.empty()) return x;
        const float cw = Width(caption, kCrimson, 21);
        Text(caption, x, cy - P(13), kCrimson, 21, pal::kLabel);
        return x + cw + P(24);
    }
};

std::string Prettify(std::string t) {
    for (size_t p; (p = t.find(" -- ")) != std::string::npos;) t.replace(p, 4, " \xE2\x80\x94 ");  // em dash
    return t;
}

FLinearColor StateColor(int st) {
    switch (st) {
        case steam::kStateConnected: return pal::kGood;
        case steam::kStateError:     return pal::kBad;
        case steam::kStateIdle:      return pal::kLabel;
        default:                     return pal::kWarn;
    }
}

void HandleInput() {
    if (!IsProcessForeground()) return;
    if (KeyJustPressed(VK_F9)) {
        g_open = !g_open;
        g_editing = false;
        net::Log("menu: F9 -> %s (HUD draws seen so far: %u)", g_open ? "open" : "closed", g_drawCount);
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
    if (g_drawCount++ == 0)
        net::Log("menu: first HUD draw on %s", ue::GetFullName(hud).c_str());

    Ui ui;
    if (!ui.Begin(hud)) return;
    g_wantFonts = true;
    const int st = steam::State();
    const FLinearColor stCol = StateColor(st);

    // ---- Closed: a quiet status toast, top-left, in the game's notification style.
    if (!g_open) {
        if (st == steam::kStateIdle) return;
        const std::string msg = Prettify(steam::StatusText());
        const float x = ui.P(28), y = ui.P(26), h = ui.P(48);
        const float tw = ui.Width(msg, kCrimson, 24);
        const float headW = ui.Width("CO-OP", kTrajanBold, 21);
        const float w = ui.P(30) + headW + ui.P(18) + tw + ui.P(110);
        ui.FadeRight(x, y, w, h, FLinearColor{0.012f, 0.011f, 0.010f, 0.80f}, 0.55f);
        ui.FadedRule(x, y, w * 0.8f, pal::kRuleFaint);
        ui.FadedRule(x, y + h - 1, w * 0.8f, pal::kRuleFaint);
        const float cy = y + h * 0.5f;
        ui.Diamond(x + ui.P(14), cy, ui.P(4), stCol);
        float cx = x + ui.P(28);
        ui.Text("CO-OP", cx, cy - ui.P(12), kTrajanBold, 21, pal::kTitle);
        cx += headW + ui.P(9);
        ui.Rect(cx, cy - ui.P(8), 1, ui.P(16), pal::kRule);
        cx += ui.P(9);
        ui.Text(msg, cx, cy - ui.P(14.5f), kCrimson, 24, pal::kText);
        cx += tw + ui.P(14);
        ui.KeyHint(cx, cy, "F9", "");
        return;
    }

    // ---- Open: centered panel over a darkened scene, like the pause/inventory screens.
    const steam::Config cfg = steam::GetConfig();
    const bool lan = cfg.network == 1;
    const bool idle = st == steam::kStateIdle;

    ui.Rect(0, 0, ui.sw, ui.sh, pal::kScrim);

    const float w = ui.P(720);
    const float rowH = ui.P(50);
    const float hasFb = (!g_feedback.empty() && GetTickCount() < g_feedbackUntil) ? 1.0f : 0.0f;
    const float h = ui.P(128) + rowH * 5 + ui.P(30) + rowH * 4 + ui.P(34) + ui.P(44) + hasFb * ui.P(32) + ui.P(72);
    const float x = (ui.sw - w) * 0.5f;
    const float y = std::max(ui.P(24), (ui.sh - h) * 0.5f);

    ui.Rect(x, y, w, h, pal::kPanel);
    ui.Frame(x, y, w, h, pal::kRuleFaint);
    ui.Frame(x + ui.P(6), y + ui.P(6), w - ui.P(12), h - ui.P(12), WithAlpha(pal::kRuleFaint, 0.6f));
    // corner diamonds on the inner frame
    ui.Diamond(x + ui.P(6), y + ui.P(6), ui.P(3), pal::kRule);
    ui.Diamond(x + w - ui.P(6), y + ui.P(6), ui.P(3), pal::kRule);
    ui.Diamond(x + ui.P(6), y + h - ui.P(6), ui.P(3), pal::kRule);
    ui.Diamond(x + w - ui.P(6), y + h - ui.P(6), ui.P(3), pal::kRule);

    // Title
    const float mx = x + w * 0.5f;
    ui.Text("CO-OP", mx, y + ui.P(30), kTrajanBold, 44, pal::kTitle, 1);
    ui.Divider(x + ui.P(70), y + ui.P(100), w - ui.P(140));

    const float inX = x + ui.P(60), inW = w - ui.P(120);
    float ry = y + ui.P(128);

    // Settings rows: label left (muted), value right; cycle values get chevrons.
    struct Setting { const char* label; std::string value; bool cycle; bool edit; bool enabled; };
    const bool blink = ((GetTickCount() / 500u) & 1u) == 0;
    const Setting settings[5] = {
        {"Network",        cfg.network == 0 ? "Steam P2P" : "LAN (UDP)", true, false, true},
        {"Steam Identity", cfg.steamIdentity == 0 ? "Mortal Shell II" : "Spacewar (480)", true, false, true},
        {"Password",       cfg.password.empty() && !(g_editing && g_sel == 2) ? "None"
                                                                              : cfg.password, false, true, true},
        {"Server Address", cfg.server, false, true, lan},
        {"Port",           std::to_string(cfg.port), true, false, lan},
    };
    for (int i = 0; i < 5; ++i) {
        const Setting& r = settings[i];
        const bool sel = i == g_sel;
        const float cy = ry + rowH * 0.5f;
        if (sel) {
            ui.FadedBand(inX - ui.P(34), ry + ui.P(4), inW + ui.P(68), rowH - ui.P(8),
                         g_editing ? WithAlpha(pal::kSelBand, 1.6f) : pal::kSelBand);
            ui.FadedRule(inX - ui.P(34), ry + ui.P(4), inW + ui.P(68), pal::kRuleFaint);
            ui.FadedRule(inX - ui.P(34), ry + rowH - ui.P(5), inW + ui.P(68), pal::kRuleFaint);
            ui.Diamond(inX - ui.P(16), cy, ui.P(4), pal::kOrnament);
        }
        const float ty = cy - ui.P(16);
        const FLinearColor lc = !r.enabled ? pal::kDim : sel ? pal::kText : pal::kLabel;
        FLinearColor vc = !r.enabled ? pal::kDim : sel ? pal::kTextBright : pal::kText;
        if (i == 2 && cfg.password.empty() && !(g_editing && sel)) vc = pal::kLabel;
        ui.Text(r.label, inX, ty, kCrimson, 27, lc);

        const bool editingThis = g_editing && sel && r.edit;
        const FLinearColor ac = sel ? pal::kOrnament : WithAlpha(pal::kRule, r.enabled ? 1.0f : 0.5f);
        float vx = inX + inW;
        if (r.cycle) {
            ui.Arrow(vx - ui.P(6), cy, ui.P(6), 1, ac);
            vx -= ui.P(26);
        }
        std::string shown = r.value;
        if (editingThis) shown += blink ? "|" : " ";
        const float vw = ui.Text(shown, vx, ty, sel ? kCrimsonSemi : kCrimson, 27, vc, 2);
        if (r.cycle) ui.Arrow(vx - vw - ui.P(20), cy, ui.P(6), 0, ac);
        ry += rowH;
    }

    ry += ui.P(12);
    ui.FadedRule(x + ui.P(70), ry, w - ui.P(140), pal::kRuleFaint);
    ry += ui.P(18);

    // Action rows: the pause menu's buttons -- a dark bar fading out at both ends
    // with Trajan small caps (mixed case), brightened when selected.
    const std::string actions[4] = {
        "Host Session",
        "Join Session",
        "Disconnect",
        std::string("Restart as ") + (cfg.steamIdentity == 1 ? "Spacewar" : "Mortal Shell II"),
    };
    const float barW = ui.P(470);
    for (int a = 0; a < 4; ++a) {
        const int i = 5 + a;
        const bool sel = i == g_sel;
        const bool enabled = (a == 2) ? !idle : true;
        const float bx = mx - barW * 0.5f, by = ry + ui.P(4), bh = rowH - ui.P(8);
        ui.FadedBand(bx, by, barW, bh, FLinearColor{0.0f, 0.0f, 0.0f, 0.70f});
        ui.FadedRule(bx, by, barW, WithAlpha(pal::kRuleFaint, 0.6f));
        ui.FadedRule(bx, by + bh - 1, barW, WithAlpha(pal::kRuleFaint, 0.6f));
        if (sel) {
            ui.FadedBand(bx, by, barW, bh, WithAlpha(pal::kSelBand, 1.3f));
            ui.FadedRule(bx, by, barW, pal::kRule);
            ui.FadedRule(bx, by + bh - 1, barW, pal::kRule);
        }
        const FLinearColor c = !enabled ? pal::kDim : sel ? pal::kTextBright : pal::kLabel;
        ui.Text(actions[a], mx, ry + rowH * 0.5f - ui.P(15), kTrajan, 28, c, 1);
        ry += rowH;
    }

    ry += ui.P(14);
    ui.Divider(x + ui.P(70), ry, w - ui.P(140));
    ry += ui.P(20);

    // Status, then transient feedback (italic, like the game's flavour text).
    {
        const std::string msg = Prettify(steam::StatusText());
        const float tw = ui.Width(msg, kCrimsonItalic, 24);
        ui.Diamond(mx - tw * 0.5f - ui.P(10), ry + ui.P(15), ui.P(4), stCol);
        ui.Text(msg, mx + ui.P(8), ry, kCrimsonItalic, 24, WithAlpha(stCol, 0.95f), 1);
        ry += ui.P(44);
        if (hasFb > 0.0f) {
            ui.Text(Prettify(g_feedback), mx, ry - ui.P(8), kCrimsonItalic, 22, pal::kLabel, 1);
            ry += ui.P(32);
        }
    }

    // Footer key hints, bottom-left, as on the pause/inventory screens.
    const float fy = y + h - ui.P(36);
    ui.FadedRule(x + ui.P(24), fy - ui.P(28), w - ui.P(48), WithAlpha(pal::kRuleFaint, 0.7f));
    float hx = x + ui.P(32);
    if (g_editing) {
        hx = ui.KeyHint(hx, fy, "Enter", "Confirm");
        ui.KeyHint(hx, fy, "Bksp", "Delete");
    } else {
        hx = ui.KeyHint(hx, fy, "F9", "Close");
        hx = ui.KeyHint(hx, fy, "^v", "Navigate");
        hx = ui.KeyHint(hx, fy, "<>", "Change");
        ui.KeyHint(hx, fy, "Enter", "Select");
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


// ---------------------------------------------------------------------------
// Drawing hook: AHUD::PostRender (native vtable slot), not ReceiveDrawHUD.
//
// Measured 2026-11-27 on this build: AHUD::PostRender runs every frame, but
// HUD.ReceiveDrawHUD never reaches ProcessEvent (0 calls with bShowHUD=1).  So
// the menu is drawn from a MinHook detour on PostRender itself, right after the
// original, while the HUD's Canvas is still set.
//
// PostRender's slot is found without symbols: ADebugCameraHUD overrides only the
// destructor (slot 0) and PostRender, so the one other slot where the AHUD and
// ADebugCameraHUD vtables differ is PostRender.  If that is not exactly one
// slot, the ReceiveDrawHUD hook is the fallback.
// ---------------------------------------------------------------------------
using PostRenderFn = void (*)(UObject*);
PostRenderFn g_origPostRender = nullptr;
bool g_postRenderTried = false;
bool g_postRenderHooked = false;

bool IsExec(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    return (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE)) != 0;
}

void DrawFromPostRender(UObject* hud) {
    // Keep Photomode screenshots completely clean unless the user explicitly opened F9 menu
    if (!g_open && coop::fx_grabs::LocalPhotoModeActive()) return;
    if (g_open) {
        static int s_n = 0;
        if (s_n++ < 3) {
            UObject* canvas = nullptr; UObject* dbg = nullptr;
            ue::Get<UObject*>(hud, "Canvas", canvas);
            ue::Get<UObject*>(hud, "DebugCanvas", dbg);
            net::Log("menu: draw diag Canvas=%p DebugCanvas=%p DrawRect=%p DrawText=%p",
                     (void*)canvas, (void*)dbg, (void*)ue::FindFunction(hud, "DrawRect"),
                     (void*)ue::FindFunction(hud, "DrawText"));
        }
    }
    OnDrawHUD(hud, nullptr, nullptr);
}

int SehDraw(UObject* hud) {
    __try {
        DrawFromPostRender(hud);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void DetourPostRender(UObject* hud) {
    g_origPostRender(hud);
    static bool s_dead = false;
    if (s_dead) return;
    if (!SehDraw(hud)) {
        s_dead = true;
        net::Log("menu: drawing from PostRender faulted -- menu drawing disabled");
    }
}

void InstallPostRenderHook() {
    if (g_postRenderTried) return;
    UObject* hudCdo = ue::FindObject("/Script/Engine.Default__HUD");
    UObject* dbgCdo = ue::FindObject("/Script/Engine.Default__DebugCameraHUD");
    if (!hudCdo || !dbgCdo) return;   // try again next tick
    g_postRenderTried = true;

    void** vh = nullptr; void** vd = nullptr;
    ue::ReadMem(hudCdo, &vh, sizeof(vh));
    ue::ReadMem(dbgCdo, &vd, sizeof(vd));
    int slot = -1, diffs = 0;
    void* target = nullptr;
    for (int i = 1; i < 600; ++i) {
        void* a = nullptr; void* b = nullptr;
        if (!ue::ReadMem(vh + i, &a, sizeof(a)) || !ue::ReadMem(vd + i, &b, sizeof(b))) break;
        if (!IsExec(a) || !IsExec(b)) break;
        if (a == b) continue;
        ++diffs;
        slot = i;
        target = a;
    }
    if (diffs != 1 || !target) {
        net::Log("menu: PostRender slot not identified (%d differing slots) -- using ReceiveDrawHUD", diffs);
        return;
    }
    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&DetourPostRender),
                      reinterpret_cast<LPVOID*>(&g_origPostRender)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        net::Log("menu: PostRender hook FAILED at slot %d -- using ReceiveDrawHUD", slot);
        return;
    }
    g_postRenderHooked = true;
    net::Log("menu: drawing from AHUD::PostRender (vtable slot %d, rva 0x%llx) -- press F9 for CO-OP menu",
             slot, (unsigned long long)((uintptr_t)target - (uintptr_t)GetModuleHandleW(nullptr)));
}

void OnReceiveDrawHUD(UObject* hud, UFunction* fn, void* parms) {
    if (g_postRenderHooked) return;   // PostRender draws; never draw twice
    OnDrawHUD(hud, fn, parms);
}

}  // namespace

void Tick30() {
    if (!state().toggles.menuEnabled) return;
    HandleInput();
}

void Tick2() {
    if (!state().toggles.menuEnabled) return;
    InstallPostRenderHook();
    if (g_wantFonts && (g_open || steam::State() != steam::kStateIdle)) ScanFonts();
    if (!g_hudHooked) {
        if (ue::HookPost("HUD.ReceiveDrawHUD", &OnReceiveDrawHUD)) {
            g_hudHooked = true;
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
