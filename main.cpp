#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace
{
    constexpr int kColumns = 21;
    constexpr int kRows = 23;
    constexpr int kTile = 24;
    constexpr int kTop = 66;
    constexpr int kLeft = 52;
    constexpr UINT_PTR kTimerId = 1;

    const std::array<const wchar_t*, kRows> kLevel = {
        L"#####################",
        L"#.........#.........#",
        L"#.###.###.#.###.###.#",
        L"#o#...#...#...#...#o#",
        L"#.###.#.#####.#.###.#",
        L"#.....#...#...#.....#",
        L"#####.###.#.###.#####",
        L"#.....#.......#.....#",
        L"#.###.#.#####.#.###.#",
        L"#...#.#...#...#.#...#",
        L"###.#.###.#.###.#.###",
        L"#...#...........#...#",
        L"#.###.###.#.###.###.#",
        L"#o....#.......#....o#",
        L"#.###.#.#####.#.###.#",
        L"#.....#...#...#.....#",
        L"#####.###.#.###.#####",
        L"#.....#.......#.....#",
        L"#.###.#.#####.#.###.#",
        L"#...#.#...#...#.#...#",
        L"###.#.###...###.#.###",
        L"#......... .........#",
        L"#####################"
    };

    enum class Direction { None, Up, Down, Left, Right };

    struct Actor
    {
        int x = 0;
        int y = 0;
        Direction direction = Direction::None;
    };

    struct Sound
    {
        std::vector<char> wav;
    };

    HWND g_window = nullptr;
    std::array<std::array<wchar_t, kColumns>, kRows> g_map{};
    Actor g_player{ 10, 21, Direction::Left };
    Direction g_requested = Direction::Left;
    std::array<Actor, 4> g_ghosts{};
    int g_score = 0;
    int g_lives = 3;
    int g_pellets = 0;
    int g_powerTicks = 0;
    bool g_gameOver = false;
    bool g_won = false;
    ULONGLONG g_lastPlayerStep = 0;
    ULONGLONG g_lastGhostStep = 0;
    std::mt19937 g_random{ std::random_device{}() };
    Sound g_chompSound, g_powerSound, g_ghostSound, g_deathSound, g_winSound;

    Sound MakeTone(double frequency, int milliseconds, double volume = 0.22)
    {
        constexpr int sampleRate = 22050;
        const int sampleCount = sampleRate * milliseconds / 1000;
        const int dataBytes = sampleCount * 2;
        Sound sound;
        sound.wav.resize(44 + dataBytes);
        auto put16 = [&sound](size_t offset, uint16_t value) {
            sound.wav[offset] = static_cast<char>(value & 0xff);
            sound.wav[offset + 1] = static_cast<char>((value >> 8) & 0xff);
        };
        auto put32 = [&sound](size_t offset, uint32_t value) {
            for (int i = 0; i < 4; ++i)
                sound.wav[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
        };
        auto tag = [&sound](size_t offset, const char* value) {
            std::copy(value, value + 4, sound.wav.begin() + offset);
        };

        tag(0, "RIFF"); put32(4, 36 + dataBytes); tag(8, "WAVE");
        tag(12, "fmt "); put32(16, 16); put16(20, 1); put16(22, 1);
        put32(24, sampleRate); put32(28, sampleRate * 2); put16(32, 2);
        put16(34, 16); tag(36, "data"); put32(40, dataBytes);

        for (int i = 0; i < sampleCount; ++i)
        {
            const double fade = std::min(1.0, std::min(i / 180.0, (sampleCount - i) / 500.0));
            const auto sample = static_cast<int16_t>(
                std::sin(2.0 * 3.141592653589793 * frequency * i / sampleRate) * volume * fade * 32767);
            put16(44 + static_cast<size_t>(i) * 2, static_cast<uint16_t>(sample));
        }

        return sound;
    }

    void Play(const Sound& sound)
    {
        if (!sound.wav.empty())
            PlaySoundW(reinterpret_cast<LPCWSTR>(sound.wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    bool IsOpposite(Direction a, Direction b)
    {
        return (a == Direction::Up && b == Direction::Down) ||
            (a == Direction::Down && b == Direction::Up) ||
            (a == Direction::Left && b == Direction::Right) ||
            (a == Direction::Right && b == Direction::Left);
    }

    void Offset(Direction direction, int& dx, int& dy)
    {
        dx = dy = 0;

        switch (direction)
        {
        case Direction::Up: dy = -1; break;
        case Direction::Down: dy = 1; break;
        case Direction::Left: dx = -1; break;
        case Direction::Right: dx = 1; break;
        default: break;
        }
    }

    bool CanMove(int x, int y, Direction direction)
    {
        int dx, dy;
        Offset(direction, dx, dy);
        const int nx = x + dx, ny = y + dy;
        return nx >= 0 && nx < kColumns && ny >= 0 && ny < kRows && g_map[ny][nx] != L'#';
    }

    void ResetActors()
    {
        g_player = { 10, 21, Direction::Left };
        g_requested = Direction::Left;
        g_ghosts = { Actor{ 10, 11, Direction::Left }, Actor{ 9, 11, Direction::Left },
                     Actor{ 11, 11, Direction::Right }, Actor{ 10, 12, Direction::Up } };
    }

    void ResetLevel()
    {
        g_pellets = 0;

        for (int y = 0; y < kRows; ++y)
        {
            for (int x = 0; x < kColumns; ++x)
            {
                g_map[y][x] = kLevel[y][x];

                if (g_map[y][x] == L'.' || g_map[y][x] == L'o')
                    ++g_pellets;
            }
        }

        ResetActors();
        g_powerTicks = 0;
        g_gameOver = false;
        g_won = false;
        g_lastPlayerStep = g_lastGhostStep = GetTickCount64();
    }

    void MoveActor(Actor& actor, Direction direction)
    {
        int dx, dy;
        Offset(direction, dx, dy);
        actor.x += dx;
        actor.y += dy;
        actor.direction = direction;
    }

    void UpdatePlayer()
    {
        if (CanMove(g_player.x, g_player.y, g_requested))
            MoveActor(g_player, g_requested);
        else if (CanMove(g_player.x, g_player.y, g_player.direction))
            MoveActor(g_player, g_player.direction);

        wchar_t& tile = g_map[g_player.y][g_player.x];

        if (tile == L'.')
        {
            tile = L' ';
            g_score += 10;
            --g_pellets;
            Play(g_chompSound);
        }
        else if (tile == L'o')
        {
            tile = L' ';
            g_score += 50;
            --g_pellets;
            g_powerTicks = 65;
            Play(g_powerSound);
        }
        if (g_pellets == 0)
        {
            g_won = true;
            g_gameOver = true;
            Play(g_winSound);
        }
    }

    void UpdateGhosts()
    {
        for (auto& ghost : g_ghosts)
        {
            std::array<Direction, 4> options{ Direction::Up, Direction::Down, Direction::Left, Direction::Right };
            std::shuffle(options.begin(), options.end(), g_random);
            Direction best = Direction::None;
            int bestDistance = (std::numeric_limits<int>::max)();
            const bool hasNonReverse = std::any_of(options.begin(), options.end(), [&](Direction d) {
                return CanMove(ghost.x, ghost.y, d) && !IsOpposite(d, ghost.direction);
            });

            for (Direction direction : options)
            {
                if (!CanMove(ghost.x, ghost.y, direction) ||
                    (hasNonReverse && IsOpposite(direction, ghost.direction)))
                    continue;

                int dx, dy;
                Offset(direction, dx, dy);
                const int distance = std::abs(ghost.x + dx - g_player.x) + std::abs(ghost.y + dy - g_player.y);
                const int adjusted = g_powerTicks > 0 ? -distance : distance;

                if (adjusted < bestDistance)
                {
                    bestDistance = adjusted;
                    best = direction;
                }
            }
            if (best != Direction::None)
                MoveActor(ghost, best);
        }
    }

    void CheckCollisions()
    {
        for (auto& ghost : g_ghosts)
        {
            if (ghost.x != g_player.x || ghost.y != g_player.y)
                continue;
            if (g_powerTicks > 0)
            {
                g_score += 200;
                Play(g_ghostSound);
                ghost = { 10, 11, Direction::Left };
            }
            else
            {
                --g_lives;
                Play(g_deathSound);
                if (g_lives <= 0)
                    g_gameOver = true;
                else
                    ResetActors();
                return;
            }
        }
    }

    void UpdateGame()
    {
        if (g_gameOver)
            return;

        const ULONGLONG now = GetTickCount64();

        if (now - g_lastPlayerStep >= 125)
        {
            g_lastPlayerStep = now;
            UpdatePlayer();
        }
        if (!g_gameOver && now - g_lastGhostStep >= 190)
        {
            g_lastGhostStep = now;
            UpdateGhosts();
            CheckCollisions();
        }
        if (g_powerTicks > 0)
            --g_powerTicks;

        InvalidateRect(g_window, nullptr, FALSE);
    }

    COLORREF GhostColor(size_t index)
    {
        constexpr COLORREF colors[] = { RGB(255, 60, 70), RGB(255, 150, 220), RGB(40, 220, 245), RGB(255, 165, 45) };
        return g_powerTicks > 0 && (g_powerTicks > 15 || (g_powerTicks / 4) % 2 == 0)
            ? RGB(35, 65, 230) : colors[index % 4];
    }

    void DrawTextCentered(HDC dc, const wchar_t* text, int y, int height, COLORREF color, int fontSize)
    {
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT old = static_cast<HFONT>(SelectObject(dc, font));
        RECT rect{ 0, y, kLeft * 2 + kColumns * kTile, y + height };
        DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(font);
    }

    void DrawPacman(HDC dc, int cx, int cy)
    {
        const int radius = kTile / 2 - 2;
        HBRUSH brush = CreateSolidBrush(RGB(255, 225, 0));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
        HPEN pen = static_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
        int mouth = 20;
        switch (g_player.direction)
        {
        case Direction::Left: mouth = 200; break;
        case Direction::Up: mouth = 290; break;
        case Direction::Down: mouth = 110; break;
        default: mouth = 20; break;
        }
        const int gap = (g_lastPlayerStep / 100) % 2 == 0 ? 22 : 4;
        Pie(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1,
            cx + static_cast<int>(std::cos((mouth - gap) * 3.141592653589793 / 180) * radius),
            cy - static_cast<int>(std::sin((mouth - gap) * 3.141592653589793 / 180) * radius),
            cx + static_cast<int>(std::cos((mouth + gap) * 3.141592653589793 / 180) * radius),
            cy - static_cast<int>(std::sin((mouth + gap) * 3.141592653589793 / 180) * radius));
        SelectObject(dc, pen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }

    void DrawGhost(HDC dc, const Actor& ghost, COLORREF color)
    {
        const int x = kLeft + ghost.x * kTile + 3;
        const int y = kTop + ghost.y * kTile + 3;
        HBRUSH brush = CreateSolidBrush(color);
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
        HPEN pen = static_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
        Ellipse(dc, x, y, x + kTile - 6, y + kTile - 6);
        RECT lower{ x, y + kTile / 2 - 2, x + kTile - 6, y + kTile - 6 };
        Rectangle(dc, lower.left, lower.top, lower.right, lower.bottom);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        Ellipse(dc, x + 4, y + 7, x + 10, y + 15);
        Ellipse(dc, x + 11, y + 7, x + 17, y + 15);
        SelectObject(dc, GetStockObject(BLACK_BRUSH));
        Ellipse(dc, x + 6, y + 10, x + 9, y + 14);
        Ellipse(dc, x + 13, y + 10, x + 16, y + 14);
        SelectObject(dc, pen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }

    void PaintGame(HDC dc)
    {
        RECT client;
        GetClientRect(g_window, &client);
        HBRUSH background = CreateSolidBrush(RGB(4, 4, 18));
        FillRect(dc, &client, background);
        DeleteObject(background);

        wchar_t status[160];
        swprintf_s(status, L"SCORE  %06d          LIVES  %d", g_score, g_lives);
        DrawTextCentered(dc, status, 12, 34, RGB(255, 255, 255), 21);

        for (int y = 0; y < kRows; ++y)
        {
            for (int x = 0; x < kColumns; ++x)
            {
                const int left = kLeft + x * kTile, top = kTop + y * kTile;
                if (g_map[y][x] == L'#')
                {
                    HBRUSH wall = CreateSolidBrush(RGB(20, 40, 210));
                    RECT r{ left + 1, top + 1, left + kTile - 1, top + kTile - 1 };
                    FillRect(dc, &r, wall);
                    DeleteObject(wall);
                }
                else if (g_map[y][x] == L'.' || g_map[y][x] == L'o')
                {
                    const int radius = g_map[y][x] == L'o' ? 6 : 2;
                    HBRUSH dot = CreateSolidBrush(RGB(255, 220, 170));
                    HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, dot));
                    const int cx = left + kTile / 2, cy = top + kTile / 2;
                    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
                    SelectObject(dc, old);
                    DeleteObject(dot);
                }
            }
        }

        for (size_t i = 0; i < g_ghosts.size(); ++i)
            DrawGhost(dc, g_ghosts[i], GhostColor(i));

        DrawPacman(dc, kLeft + g_player.x * kTile + kTile / 2, kTop + g_player.y * kTile + kTile / 2);

        if (g_powerTicks > 0)
            DrawTextCentered(dc, L"POWER UP!", kTop + kRows * kTile + 5, 24, RGB(100, 170, 255), 16);

        if (g_gameOver)
        {
            RECT overlay{ 0, kTop + 8 * kTile, kLeft * 2 + kColumns * kTile, kTop + 14 * kTile };
            HBRUSH shade = CreateSolidBrush(RGB(4, 4, 18));
            FillRect(dc, &overlay, shade);
            DeleteObject(shade);
            DrawTextCentered(dc, g_won ? L"YOU CLEARED THE MAZE!" : L"GAME OVER", overlay.top + 10, 38,
                g_won ? RGB(255, 225, 0) : RGB(255, 70, 70), 28);
            DrawTextCentered(dc, L"Press ENTER to play again", overlay.top + 52, 30, RGB(255, 255, 255), 17);
        }
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            SetTimer(window, kTimerId, 500, nullptr);
            return 0;
        case WM_TIMER:
            UpdateGame();
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_UP) g_requested = Direction::Up;
            else if (wParam == VK_DOWN) g_requested = Direction::Down;
            else if (wParam == VK_LEFT) g_requested = Direction::Left;
            else if (wParam == VK_RIGHT) g_requested = Direction::Right;
            else if (wParam == VK_RETURN && g_gameOver)
            {
                g_score = 0;
                g_lives = 3;
                ResetLevel();
            }
            else if (wParam == VK_ESCAPE) DestroyWindow(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, kLeft * 2 + kColumns * kTile, kTop + kRows * kTile + 40);
            HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
            PaintGame(buffer);
            BitBlt(dc, 0, 0, kLeft * 2 + kColumns * kTile, kTop + kRows * kTile + 40, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(buffer);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, kTimerId);
            PlaySoundW(nullptr, nullptr, 0);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    g_chompSound = MakeTone(620, 70);
    g_powerSound = MakeTone(880, 240, 0.28);
    g_ghostSound = MakeTone(1040, 180, 0.26);
    g_deathSound = MakeTone(190, 520, 0.3);
    g_winSound = MakeTone(740, 420, 0.26);
    ResetLevel();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"Win32PacManWindow";

    if (!RegisterClassExW(&windowClass))
        return 1;

    RECT desired{ 0, 0, kLeft * 2 + kColumns * kTile, kTop + kRows * kTile + 40 };
    AdjustWindowRect(&desired, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_window = CreateWindowExW(0, windowClass.lpszClassName, L"Pac-Man - Win32",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
        nullptr, nullptr, instance, nullptr);

    if (!g_window)
        return 1;

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);
    MSG message{};

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
