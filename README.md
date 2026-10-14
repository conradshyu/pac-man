# Pac-Man for Windows

A small single-player maze game implemented in C++17 with the Win32 API. The game draws its maze and characters with GDI, reads arrow keys from the window message loop, and synthesizes short PCM sound effects in memory for the Windows multimedia `PlaySound` API. No third-party libraries or external art/audio files are required.

## Requirements

- Windows 10 or later
- Visual Studio 2026 Community (Visual Studio version 18) with **Desktop development with C++**
- A Windows 10 SDK installed through the Visual Studio Installer

The solution is configured for the Visual Studio 18 `v145` C++ toolset and Windows SDK `10.0`. If the Windows SDK version on your machine differs, change `WindowsTargetPlatformVersion` in `PacMan.vcxproj` to an installed SDK version. If you installed a different C++ toolset, select it in the project properties under **Configuration Properties > General > Platform Toolset**.

## Open and build in Visual Studio

1. Extract `pacman.zip` to a writable folder.
2. Open `PacMan.sln` in Visual Studio. The requested Visual Studio executable is:

   ```text
   C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe
   ```

   You can open the solution from File Explorer or run:

   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe" "C:\path\to\extracted\pacman\PacMan.sln"
   ```

3. Choose **Debug** or **Release**, and choose **x64** (recommended) or **Win32** in the toolbar.
4. Select **Build > Build Solution** or press **Ctrl+Shift+B**.
5. Start with **Debug > Start Without Debugging** (Ctrl+F5).

The executable is written to `bin\<platform>\<configuration>\PacMan.exe` beneath the solution folder.

## Build from a Visual Studio Developer PowerShell

Open **Developer PowerShell for VS 2026** from the Start menu, change to the extracted project folder, then run:

```powershell
msbuild .\PacMan.sln /m /p:Configuration=Release /p:Platform=x64
```

Run the resulting program with:

```powershell
.\bin\x64\Release\PacMan.exe
```

## Controls and rules

- **Arrow keys:** steer Pac-Man. Turns are buffered until the requested direction is available.
- Eat the small dots for 10 points and large power pellets for 50 points.
- Power pellets temporarily let Pac-Man eat ghosts for 200 points each.
- Avoid ghosts; collisions cost one of three lives.
- Clear all pellets to win. **Enter** restarts after a win or game over; **Esc** exits.
- Score and remaining lives are displayed above the maze.
- Sound effects play for eating dots, power pellets, ghosts, death, and victory. Ensure Windows audio is enabled.

## Project contents

- `main.cpp` — Win32 window, GDI rendering, game logic, keyboard input, and generated sound effects
- `PacMan.sln` — Visual Studio solution
- `PacMan.vcxproj` — Debug/Release and Win32/x64 project configurations
