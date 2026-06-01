# Bomberman C++ Port (Terminal + Graphics)

This is a C++ port of the original Java Bomberman project.

Standard cross-platform graphics tool recommendation: `SDL2`.
- It is widely used, stable, and works on Windows/macOS/Linux.
- This repo now includes an SDL2 graphics frontend for cross-platform builds.
- Graphics frontend also uses existing project assets:
  - `res/sprites/bomb.png`
  - `res/images/startButton.png`
  - `res/images/pauseButton.png`
  - `res/images/resumeButton.png`

## Features Ported
- Level loading from `res/Levels/Level1.txt` to `Level3.txt`
- 3 levels with loop-back progression (`1 -> 2 -> 3 -> 1`)
- Player movement, bombs, timed explosions
- Explosion power-up (`FlameItem`) and speed-up (`SpeedItem`)
- Enemy behaviors:
  - `Ballom` (random movement)
  - `Oneal` (chases player)
  - `Kondoria` (horizontal patrol)
  - `Doll` (grid pathfinding)
- Portal opens after all enemies are defeated
- Pause/restart/quit controls

## Build (No CMake Needed)
```bash
cd /Users/swsweta/Desktop/oopsgame/OOP-based-Bomberman-game/cpp
clang++ -std=c++17 -O2 src/main.cpp -lncurses -o bomberman
```

## Run
```bash
cd /Users/swsweta/Desktop/oopsgame/OOP-based-Bomberman-game/cpp
./bomberman
```

## Graphics Build (Cross-platform SDL2)
```bash
cd /Users/swsweta/Desktop/oopsgame/OOP-based-Bomberman-game
cmake -S cpp -B cpp/build
cmake --build cpp/build --target bomberman_graphics_sdl
./cpp/build/bomberman_graphics_sdl
```

If PNG asset loading is missing, install SDL2 image support:
```bash
brew install sdl2_image
```

On Windows (PowerShell):
```powershell
cd C:\path\to\OOP-based-Bomberman-game
cmake -S cpp -B cpp\build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build cpp\build --config Release --target bomberman_graphics_sdl
.\cpp\build\Release\bomberman_graphics_sdl.exe
```

If SDL2 is not installed yet on Windows:
```powershell
vcpkg install sdl2:x64-windows
vcpkg install sdl2-image:x64-windows
```

## Graphics Build (macOS, Cocoa)
```bash
cd /Users/swsweta/Desktop/oopsgame/OOP-based-Bomberman-game/cpp
clang++ -std=c++17 -fobjc-arc src/graphics_main.mm -framework Cocoa -o bomberman_graphics
./bomberman_graphics
```

## Build (CMake Optional)
```bash
cd /Users/swsweta/Desktop/oopsgame/OOP-based-Bomberman-game/cpp
cmake -S . -B build
cmake --build build
./build/bomberman
```

## Controls
- Move: Arrow keys or `W A S D`
- Place bomb: `Space`
- Pause: `P`
- Restart level (after death): `R`
- Quit: `Q`
