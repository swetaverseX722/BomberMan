#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
#error "SDL2 headers not found. Install SDL2 and ensure include path is configured."
#endif

#if BOMBERMAN_USE_SDL_IMAGE
#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#elif __has_include(<SDL_image.h>)
#include <SDL_image.h>
#else
#error "BOMBERMAN_USE_SDL_IMAGE is enabled but SDL_image headers are missing."
#endif
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace bomberman_sdl {

using Clock = std::chrono::steady_clock;

struct Vec2 {
    int x = 0;
    int y = 0;

    bool operator==(const Vec2& other) const {
        return x == other.x && y == other.y;
    }
};

enum class Tile {
    Empty,
    Wall,
    Brick,
    SpeedHidden,
    FlameHidden,
    SpeedVisible,
    FlameVisible,
};

enum class EnemyType {
    Ballom,
    Oneal,
    Kondoria,
    Doll,
};

enum class Direction {
    Up,
    Down,
    Left,
    Right,
};

struct Enemy {
    EnemyType type = EnemyType::Ballom;
    Vec2 pos{0, 0};
    bool alive = true;
    bool moveLeft = true;
};

struct BombState {
    bool active = false;
    bool exploding = false;
    Vec2 pos{0, 0};
    Clock::time_point placedAt{};
    Clock::time_point explosionAt{};
    std::vector<Vec2> explosionCells;
};

class GameModel {
public:
    GameModel() : rng_(std::random_device{}()) {
        resetLevel(1);
    }

    void update() {
        if (paused_) {
            return;
        }

        updateTimer();
        updateBomb();
        updateEnemies();
        killEntitiesInsideExplosion();
        checkPlayerEnemyCollision();
        checkLevelState();
    }

    void togglePause() {
        paused_ = !paused_;
    }

    void restartLevel() {
        resetLevel(level_);
    }

    void placeBomb() {
        if (!playerAlive_ || paused_) {
            return;
        }
        if (bomb_.active || bombsRemaining_ <= 0) {
            return;
        }

        bomb_.active = true;
        bomb_.exploding = false;
        bomb_.pos = player_;
        bomb_.placedAt = Clock::now();
        bomb_.explosionCells.clear();
        bombsRemaining_--;
    }

    void move(Direction direction) {
        if (!playerAlive_ || paused_) {
            return;
        }

        const Clock::time_point now = Clock::now();
        const int moveCooldownMs = (playerSpeed_ >= 2) ? 80 : 160;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayerMoveTick_).count() < moveCooldownMs) {
            return;
        }

        const Vec2 d = delta(direction);
        const Vec2 target{player_.x + d.x, player_.y + d.y};
        if (!isWalkable(target)) {
            return;
        }

        player_ = target;
        lastPlayerMoveTick_ = now;

        Tile& tile = map_[player_.y][player_.x];
        if (tile == Tile::SpeedVisible) {
            playerSpeed_ = 2;
            tile = Tile::Empty;
        } else if (tile == Tile::FlameVisible) {
            flamePower_ += 2;
            tile = Tile::Empty;
        }

        if (portalVisible_ && player_ == portal_) {
            goToNextLevel();
        }
    }

    bool isPaused() const {
        return paused_;
    }

    bool isPlayerAlive() const {
        return playerAlive_;
    }

    int level() const {
        return level_;
    }

    int width() const {
        return width_;
    }

    int height() const {
        return height_;
    }

    int timeRemaining() const {
        return std::max(0, timeRemaining_);
    }

    int bombsRemaining() const {
        return bombsRemaining_;
    }

    int speed() const {
        return playerSpeed_;
    }

    int flamePower() const {
        return flamePower_;
    }

    std::size_t enemyCount() const {
        return enemies_.size();
    }

    const Vec2& player() const {
        return player_;
    }

    const Vec2& portal() const {
        return portal_;
    }

    bool isPortalVisible() const {
        return portalVisible_;
    }

    const std::vector<Enemy>& enemies() const {
        return enemies_;
    }

    Tile tileAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return Tile::Wall;
        }
        return map_[y][x];
    }

    bool hasBomb() const {
        return bomb_.active && !bomb_.exploding;
    }

    const Vec2& bombPosition() const {
        return bomb_.pos;
    }

    bool isExplosionAt(int x, int y) const {
        if (!bomb_.active || !bomb_.exploding) {
            return false;
        }

        const Vec2 p{x, y};
        for (const Vec2& cell : bomb_.explosionCells) {
            if (cell == p) {
                return true;
            }
        }
        return false;
    }

private:
    bool paused_ = false;
    bool playerAlive_ = true;

    int level_ = 1;
    int width_ = 0;
    int height_ = 0;

    int timeRemaining_ = 120;
    int bombsRemaining_ = 20;
    int playerSpeed_ = 1;
    int flamePower_ = 0;

    Vec2 player_{1, 1};
    Vec2 portal_{0, 0};
    bool portalVisible_ = false;

    std::vector<std::vector<Tile>> map_;
    std::vector<Enemy> enemies_;
    BombState bomb_;

    Clock::time_point lastSecondTick_ = Clock::now();
    Clock::time_point lastPlayerMoveTick_ = Clock::now();
    Clock::time_point lastEnemyMoveTick_ = Clock::now();

    std::mt19937 rng_;

    bool inBounds(const Vec2& p) const {
        return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
    }

    int index(const Vec2& p) const {
        return p.y * width_ + p.x;
    }

    Vec2 delta(Direction dir) const {
        switch (dir) {
            case Direction::Up:
                return {0, -1};
            case Direction::Down:
                return {0, 1};
            case Direction::Left:
                return {-1, 0};
            case Direction::Right:
                return {1, 0};
        }
        return {0, 0};
    }

    bool isSolidForMovement(Tile tile) const {
        return tile == Tile::Wall || tile == Tile::Brick || tile == Tile::SpeedHidden || tile == Tile::FlameHidden;
    }

    bool isWalkable(const Vec2& p) const {
        if (!inBounds(p)) {
            return false;
        }

        if (isSolidForMovement(map_[p.y][p.x])) {
            return false;
        }

        if (bomb_.active && !bomb_.exploding && bomb_.pos == p) {
            return false;
        }

        return true;
    }

    std::optional<std::string> resolveLevelPath(int level) const {
        const std::string levelFile = "Level" + std::to_string(level) + ".txt";
        const std::array<std::string, 4> candidates = {
            "res/Levels/" + levelFile,
            "../res/Levels/" + levelFile,
            "../../res/Levels/" + levelFile,
            "../../../res/Levels/" + levelFile,
        };

        for (const std::string& candidate : candidates) {
            std::ifstream test(candidate);
            if (test.is_open()) {
                return candidate;
            }
        }

        return std::nullopt;
    }

    bool loadMap(int level) {
        const std::optional<std::string> resolvedPath = resolveLevelPath(level);
        if (!resolvedPath.has_value()) {
            return false;
        }

        std::ifstream input(*resolvedPath);
        int fileLevel = 0;
        input >> fileLevel >> height_ >> width_;

        map_.assign(height_, std::vector<Tile>(width_, Tile::Empty));
        bool portalFound = false;

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                int token = 0;
                input >> token;

                switch (token) {
                    case 1:
                        map_[y][x] = Tile::Empty;
                        portal_ = {x, y};
                        portalFound = true;
                        break;
                    case 2:
                        map_[y][x] = Tile::Wall;
                        break;
                    case 3:
                        map_[y][x] = Tile::Brick;
                        break;
                    case 6:
                        map_[y][x] = Tile::SpeedHidden;
                        break;
                    case 7:
                        map_[y][x] = Tile::FlameHidden;
                        break;
                    default:
                        map_[y][x] = Tile::Empty;
                        break;
                }
            }
        }

        if (!portalFound) {
            portal_ = {width_ - 2, height_ - 2};
        }

        return true;
    }

    void spawnEnemiesForLevel(int level) {
        enemies_.clear();

        auto addEnemy = [this](EnemyType type, int x, int y) {
            enemies_.push_back(Enemy{type, {x, y}, true, true});
        };

        if (level == 1) {
            addEnemy(EnemyType::Ballom, 4, 4);
            addEnemy(EnemyType::Ballom, 9, 9);
            addEnemy(EnemyType::Ballom, 22, 6);
            addEnemy(EnemyType::Oneal, 7, 6);
            addEnemy(EnemyType::Oneal, 13, 8);
        } else if (level == 2) {
            addEnemy(EnemyType::Ballom, 5, 5);
            addEnemy(EnemyType::Ballom, 11, 9);
            addEnemy(EnemyType::Kondoria, 1, 3);
            addEnemy(EnemyType::Kondoria, 1, 7);
            addEnemy(EnemyType::Kondoria, 1, 11);
            addEnemy(EnemyType::Oneal, 7, 5);
            addEnemy(EnemyType::Oneal, 19, 7);
        } else {
            addEnemy(EnemyType::Ballom, 5, 5);
            addEnemy(EnemyType::Ballom, 11, 9);
            addEnemy(EnemyType::Doll, 7, 5);
        }
    }

    int bombsForLevel(int level) const {
        if (level == 1) {
            return 20;
        }
        if (level == 2) {
            return 30;
        }
        return 40;
    }

    void resetLevel(int level) {
        level_ = level;

        if (!loadMap(level_)) {
            // Safe fallback map.
            width_ = 25;
            height_ = 15;
            map_.assign(height_, std::vector<Tile>(width_, Tile::Empty));
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    if (x == 0 || y == 0 || x == width_ - 1 || y == height_ - 1) {
                        map_[y][x] = Tile::Wall;
                    }
                }
            }
            portal_ = {width_ - 2, height_ - 2};
        }

        spawnEnemiesForLevel(level_);

        player_ = {1, 1};
        playerAlive_ = true;
        paused_ = false;
        portalVisible_ = false;

        timeRemaining_ = 120;
        bombsRemaining_ = bombsForLevel(level_);
        playerSpeed_ = 1;
        flamePower_ = 0;

        bomb_ = BombState{};

        const Clock::time_point now = Clock::now();
        lastSecondTick_ = now;
        lastPlayerMoveTick_ = now;
        lastEnemyMoveTick_ = now;
    }

    void goToNextLevel() {
        int next = level_ + 1;
        if (next > 3) {
            next = 1;
        }
        resetLevel(next);
    }

    std::vector<Vec2> computeExplosionCells() const {
        std::vector<Vec2> cells;
        if (!bomb_.active) {
            return cells;
        }

        cells.push_back(bomb_.pos);

        const int range = 1 + flamePower_;
        const std::array<Direction, 4> dirs = {
            Direction::Up,
            Direction::Down,
            Direction::Left,
            Direction::Right,
        };

        for (Direction dir : dirs) {
            const Vec2 d = delta(dir);
            for (int step = 1; step <= range; ++step) {
                const Vec2 next{bomb_.pos.x + d.x * step, bomb_.pos.y + d.y * step};
                if (!inBounds(next)) {
                    break;
                }

                const Tile tile = map_[next.y][next.x];
                if (tile == Tile::Wall) {
                    break;
                }

                cells.push_back(next);

                if (tile == Tile::Brick || tile == Tile::SpeedHidden || tile == Tile::FlameHidden) {
                    break;
                }
            }
        }

        return cells;
    }

    void applyExplosionEffects() {
        for (const Vec2& cell : bomb_.explosionCells) {
            Tile& tile = map_[cell.y][cell.x];
            if (tile == Tile::Brick) {
                tile = Tile::Empty;
            } else if (tile == Tile::SpeedHidden) {
                tile = Tile::SpeedVisible;
            } else if (tile == Tile::FlameHidden) {
                tile = Tile::FlameVisible;
            }
        }
    }

    void killEntitiesInsideExplosion() {
        if (!bomb_.active || !bomb_.exploding) {
            return;
        }

        const auto inExplosion = [this](const Vec2& p) {
            for (const Vec2& cell : bomb_.explosionCells) {
                if (cell == p) {
                    return true;
                }
            }
            return false;
        };

        if (playerAlive_ && inExplosion(player_)) {
            playerAlive_ = false;
        }

        for (Enemy& enemy : enemies_) {
            if (enemy.alive && inExplosion(enemy.pos)) {
                enemy.alive = false;
            }
        }

        enemies_.erase(
            std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& enemy) { return !enemy.alive; }),
            enemies_.end());
    }

    void updateBomb() {
        if (!bomb_.active) {
            return;
        }

        const Clock::time_point now = Clock::now();
        const long long sincePlacementMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - bomb_.placedAt).count();

        if (!bomb_.exploding && sincePlacementMs >= 2000) {
            bomb_.exploding = true;
            bomb_.explosionAt = now;
            bomb_.explosionCells = computeExplosionCells();
            applyExplosionEffects();
        }

        if (bomb_.exploding) {
            killEntitiesInsideExplosion();
            const long long sinceExplosionMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - bomb_.explosionAt).count();
            if (sinceExplosionMs >= 1000) {
                bomb_ = BombState{};
            }
        }
    }

    std::optional<Vec2> bfsNextStep(const Vec2& start, const Vec2& goal) const {
        if (start == goal) {
            return std::nullopt;
        }

        std::vector<bool> visited(width_ * height_, false);
        std::vector<Vec2> parent(width_ * height_, Vec2{-1, -1});
        std::queue<Vec2> q;

        q.push(start);
        visited[index(start)] = true;

        const std::array<Direction, 4> dirs = {
            Direction::Up,
            Direction::Down,
            Direction::Left,
            Direction::Right,
        };

        while (!q.empty()) {
            const Vec2 current = q.front();
            q.pop();

            for (Direction dir : dirs) {
                const Vec2 d = delta(dir);
                const Vec2 next{current.x + d.x, current.y + d.y};
                if (!inBounds(next)) {
                    continue;
                }
                if (visited[index(next)]) {
                    continue;
                }
                if (!isWalkable(next) && !(next == goal)) {
                    continue;
                }

                visited[index(next)] = true;
                parent[index(next)] = current;

                if (next == goal) {
                    Vec2 cur = goal;
                    while (!(parent[index(cur)] == start)) {
                        cur = parent[index(cur)];
                        if (cur.x == -1) {
                            return std::nullopt;
                        }
                    }
                    return cur;
                }

                q.push(next);
            }
        }

        return std::nullopt;
    }

    void moveBallom(Enemy& enemy) {
        std::array<Direction, 4> dirs = {
            Direction::Up,
            Direction::Down,
            Direction::Left,
            Direction::Right,
        };
        std::shuffle(dirs.begin(), dirs.end(), rng_);

        for (Direction dir : dirs) {
            const Vec2 d = delta(dir);
            const Vec2 next{enemy.pos.x + d.x, enemy.pos.y + d.y};
            if (isWalkable(next)) {
                enemy.pos = next;
                return;
            }
        }
    }

    void moveOneal(Enemy& enemy) {
        int bestDistance = 1000000;
        std::vector<Vec2> candidates;

        const std::array<Direction, 4> dirs = {
            Direction::Up,
            Direction::Down,
            Direction::Left,
            Direction::Right,
        };

        for (Direction dir : dirs) {
            const Vec2 d = delta(dir);
            const Vec2 next{enemy.pos.x + d.x, enemy.pos.y + d.y};
            if (!isWalkable(next)) {
                continue;
            }

            const int distance = std::abs(player_.x - next.x) + std::abs(player_.y - next.y);
            if (distance < bestDistance) {
                bestDistance = distance;
                candidates.clear();
                candidates.push_back(next);
            } else if (distance == bestDistance) {
                candidates.push_back(next);
            }
        }

        if (!candidates.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
            enemy.pos = candidates[pick(rng_)];
        }
    }

    void moveKondoria(Enemy& enemy) {
        Vec2 next = enemy.pos;
        next.x += enemy.moveLeft ? -1 : 1;

        if (isWalkable(next)) {
            enemy.pos = next;
            return;
        }

        enemy.moveLeft = !enemy.moveLeft;
        next = enemy.pos;
        next.x += enemy.moveLeft ? -1 : 1;
        if (isWalkable(next)) {
            enemy.pos = next;
        }
    }

    void moveDoll(Enemy& enemy) {
        const std::optional<Vec2> next = bfsNextStep(enemy.pos, player_);
        if (next.has_value() && isWalkable(*next)) {
            enemy.pos = *next;
        }
    }

    void updateEnemies() {
        const Clock::time_point now = Clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEnemyMoveTick_).count() < 260) {
            return;
        }

        lastEnemyMoveTick_ = now;

        for (Enemy& enemy : enemies_) {
            if (!enemy.alive) {
                continue;
            }

            switch (enemy.type) {
                case EnemyType::Ballom:
                    moveBallom(enemy);
                    break;
                case EnemyType::Oneal:
                    moveOneal(enemy);
                    break;
                case EnemyType::Kondoria:
                    moveKondoria(enemy);
                    break;
                case EnemyType::Doll:
                    moveDoll(enemy);
                    break;
            }
        }
    }

    void updateTimer() {
        const Clock::time_point now = Clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(now - lastSecondTick_).count() >= 1) {
            timeRemaining_--;
            lastSecondTick_ += std::chrono::seconds(1);
        }

        if (timeRemaining_ < 0) {
            playerAlive_ = false;
        }
    }

    void checkPlayerEnemyCollision() {
        if (!playerAlive_) {
            return;
        }

        for (const Enemy& enemy : enemies_) {
            const int distance = std::abs(enemy.pos.x - player_.x) + std::abs(enemy.pos.y - player_.y);
            if (distance <= 1) {
                playerAlive_ = false;
                return;
            }
        }
    }

    void checkLevelState() {
        if (enemies_.empty()) {
            portalVisible_ = true;
        }

        if (portalVisible_ && playerAlive_ && player_ == portal_) {
            goToNextLevel();
        }
    }
};

class SdlRenderer {
public:
    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            return false;
        }

        const int windowWidth = model_.width() * kTile;
        const int windowHeight = (model_.height() * kTile) + kHud;

        window_ = SDL_CreateWindow(
            "Bomberman SDL",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            windowWidth,
            windowHeight,
            SDL_WINDOW_SHOWN);

        if (!window_) {
            SDL_Quit();
            return false;
        }

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }

        if (!renderer_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            SDL_Quit();
            return false;
        }

#if BOMBERMAN_USE_SDL_IMAGE
        const int imgFlags = IMG_INIT_PNG;
        hasImageSupport_ = ((IMG_Init(imgFlags) & imgFlags) == imgFlags);
        if (hasImageSupport_) {
            loadUiTextures();
        }
#endif

        return true;
    }

    void run() {
        bool running = true;

        while (running) {
            const std::uint32_t frameStart = SDL_GetTicks();

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                    running = handleKey(event.key.keysym.sym);
                } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    running = handleMouse(event.button.x, event.button.y);
                }
            }

            if (started_) {
                model_.update();
            }
            updateWindowTitle();
            render();

            const std::uint32_t frameTime = SDL_GetTicks() - frameStart;
            if (frameTime < 16) {
                SDL_Delay(16 - frameTime);
            }
        }
    }

    void shutdown() {
        destroyTexture(texBomb_);
        destroyTexture(texStart_);
        destroyTexture(texPause_);
        destroyTexture(texResume_);

#if BOMBERMAN_USE_SDL_IMAGE
        if (hasImageSupport_) {
            IMG_Quit();
        }
#endif

        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

private:
    static constexpr int kTile = 32;
    static constexpr int kHud = 72;
    static constexpr int kHudButtonWidth = 160;
    static constexpr int kHudButtonHeight = 48;

    GameModel model_;
    bool started_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texBomb_ = nullptr;
    SDL_Texture* texStart_ = nullptr;
    SDL_Texture* texPause_ = nullptr;
    SDL_Texture* texResume_ = nullptr;
    SDL_Rect hudButtonRect_{0, 0, 0, 0};
    SDL_Rect startButtonRect_{0, 0, 0, 0};
#if BOMBERMAN_USE_SDL_IMAGE
    bool hasImageSupport_ = false;
#else
    bool hasImageSupport_ = false;
#endif

    bool handleKey(SDL_Keycode key) {
        if (key == SDLK_q) {
            return false;
        }

        if (!started_) {
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_s) {
                started_ = true;
            }
            return true;
        }

        switch (key) {
            case SDLK_p:
                model_.togglePause();
                break;
            case SDLK_r:
                model_.restartLevel();
                break;
            case SDLK_SPACE:
                model_.placeBomb();
                break;
            case SDLK_UP:
            case SDLK_w:
                model_.move(Direction::Up);
                break;
            case SDLK_DOWN:
            case SDLK_s:
                model_.move(Direction::Down);
                break;
            case SDLK_LEFT:
            case SDLK_a:
                model_.move(Direction::Left);
                break;
            case SDLK_RIGHT:
            case SDLK_d:
                model_.move(Direction::Right);
                break;
            default:
                break;
        }

        return true;
    }

    bool handleMouse(int mouseX, int mouseY) {
        SDL_Point point{mouseX, mouseY};

        if (!started_) {
            if (SDL_PointInRect(&point, &startButtonRect_) || SDL_PointInRect(&point, &hudButtonRect_)) {
                started_ = true;
            }
            return true;
        }

        if (SDL_PointInRect(&point, &hudButtonRect_)) {
            model_.togglePause();
        }

        return true;
    }

    void setColor(SDL_Color c) {
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    }

    SDL_Color tileColor(Tile tile, bool explosion) {
        if (explosion) {
            return SDL_Color{242, 64, 40, 255};
        }

        switch (tile) {
            case Tile::Wall:
                return SDL_Color{35, 95, 175, 255};
            case Tile::Brick:
            case Tile::SpeedHidden:
            case Tile::FlameHidden:
                return SDL_Color{145, 84, 36, 255};
            case Tile::SpeedVisible:
                return SDL_Color{40, 215, 70, 255};
            case Tile::FlameVisible:
                return SDL_Color{240, 155, 35, 255};
            case Tile::Empty:
                return SDL_Color{28, 28, 30, 255};
        }

        return SDL_Color{28, 28, 30, 255};
    }

    void updateWindowTitle() {
        std::ostringstream oss;
        oss << "Bomberman SDL | Lv " << model_.level() << " | Time " << model_.timeRemaining() << " | Bombs "
            << model_.bombsRemaining() << " | Speed " << model_.speed() << " | Flame " << model_.flamePower()
            << " | Enemies " << model_.enemyCount();
        if (!started_) {
            oss << " | PRESS START";
        } else if (model_.isPaused()) {
            oss << " | PAUSED";
        } else if (!model_.isPlayerAlive()) {
            oss << " | GAME OVER";
        }
        SDL_SetWindowTitle(window_, oss.str().c_str());
    }

    void fillRect(int x, int y, int w, int h, SDL_Color color) {
        setColor(color);
        SDL_Rect rect{x, y, w, h};
        SDL_RenderFillRect(renderer_, &rect);
    }

    void drawRectBorder(int x, int y, int w, int h, SDL_Color color) {
        setColor(color);
        SDL_Rect rect{x, y, w, h};
        SDL_RenderDrawRect(renderer_, &rect);
    }

    void destroyTexture(SDL_Texture*& texture) {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    std::vector<std::string> assetCandidates(const std::string& relativePath) const {
        return {
            "res/" + relativePath,
            "../res/" + relativePath,
            "../../res/" + relativePath,
            "../../../res/" + relativePath,
        };
    }

    SDL_Texture* loadTextureCandidates(const std::vector<std::string>& candidates) {
#if BOMBERMAN_USE_SDL_IMAGE
        if (!hasImageSupport_) {
            return nullptr;
        }
        for (const std::string& path : candidates) {
            SDL_Texture* texture = IMG_LoadTexture(renderer_, path.c_str());
            if (texture) {
                return texture;
            }
        }
#else
        (void)candidates;
#endif
        return nullptr;
    }

    void loadUiTextures() {
        texBomb_ = loadTextureCandidates(assetCandidates("sprites/bomb.png"));
        texStart_ = loadTextureCandidates(assetCandidates("images/startButton.png"));
        texPause_ = loadTextureCandidates(assetCandidates("images/pauseButton.png"));
        texResume_ = loadTextureCandidates(assetCandidates("images/resumeButton.png"));
    }

    SDL_Rect fitTextureInRect(SDL_Texture* texture, const SDL_Rect& bounds) const {
        if (!texture) {
            return bounds;
        }

        int texW = 0;
        int texH = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &texW, &texH) != 0 || texW <= 0 || texH <= 0) {
            return bounds;
        }

        const float sx = static_cast<float>(bounds.w) / static_cast<float>(texW);
        const float sy = static_cast<float>(bounds.h) / static_cast<float>(texH);
        const float scale = std::min(sx, sy);
        const int outW = std::max(1, static_cast<int>(texW * scale));
        const int outH = std::max(1, static_cast<int>(texH * scale));

        SDL_Rect out = bounds;
        out.x += (bounds.w - outW) / 2;
        out.y += (bounds.h - outH) / 2;
        out.w = outW;
        out.h = outH;
        return out;
    }

    void drawTextureFitted(SDL_Texture* texture, const SDL_Rect& bounds) {
        if (!texture) {
            return;
        }
        const SDL_Rect fitted = fitTextureInRect(texture, bounds);
        SDL_RenderCopy(renderer_, texture, nullptr, &fitted);
    }

    void render() {
        const int boardWidth = model_.width() * kTile;
        const int boardHeight = model_.height() * kTile;

        setColor(SDL_Color{14, 14, 16, 255});
        SDL_RenderClear(renderer_);

        fillRect(0, 0, boardWidth, kHud, SDL_Color{18, 18, 22, 255});

        hudButtonRect_ = SDL_Rect{boardWidth - kHudButtonWidth - 12, 12, kHudButtonWidth, kHudButtonHeight};
        startButtonRect_ = SDL_Rect{(boardWidth - 280) / 2, kHud + (boardHeight - 96) / 2, 280, 96};

        for (int y = 0; y < model_.height(); ++y) {
            for (int x = 0; x < model_.width(); ++x) {
                const bool explosion = model_.isExplosionAt(x, y);
                SDL_Color base = tileColor(model_.tileAt(x, y), explosion);
                fillRect(x * kTile, kHud + y * kTile, kTile, kTile, base);
                drawRectBorder(x * kTile, kHud + y * kTile, kTile, kTile, SDL_Color{0, 0, 0, 45});
            }
        }

        if (model_.isPortalVisible()) {
            const Vec2& p = model_.portal();
            fillRect(p.x * kTile + 8, kHud + p.y * kTile + 8, kTile - 16, kTile - 16, SDL_Color{45, 220, 255, 255});
        }

        if (model_.hasBomb()) {
            const Vec2& b = model_.bombPosition();
            const SDL_Rect bombRect{b.x * kTile + 4, kHud + b.y * kTile + 4, kTile - 8, kTile - 8};
            if (texBomb_) {
                drawTextureFitted(texBomb_, bombRect);
            } else {
                fillRect(b.x * kTile + 7, kHud + b.y * kTile + 7, kTile - 14, kTile - 14, SDL_Color{240, 50, 70, 255});
            }
        }

        for (const Enemy& enemy : model_.enemies()) {
            SDL_Color color{220, 50, 62, 255};
            switch (enemy.type) {
                case EnemyType::Ballom:
                    color = SDL_Color{215, 45, 145, 255};
                    break;
                case EnemyType::Oneal:
                    color = SDL_Color{230, 65, 55, 255};
                    break;
                case EnemyType::Kondoria:
                    color = SDL_Color{220, 130, 45, 255};
                    break;
                case EnemyType::Doll:
                    color = SDL_Color{150, 65, 235, 255};
                    break;
            }
            fillRect(enemy.pos.x * kTile + 6, kHud + enemy.pos.y * kTile + 6, kTile - 12, kTile - 12, color);
        }

        const Vec2& player = model_.player();
        SDL_Color playerColor = model_.isPlayerAlive() ? SDL_Color{40, 225, 70, 255} : SDL_Color{135, 135, 135, 255};
        fillRect(player.x * kTile + 6, kHud + player.y * kTile + 6, kTile - 12, kTile - 12, playerColor);

        if (!started_) {
            fillRect(0, kHud, boardWidth, boardHeight, SDL_Color{0, 0, 0, 150});
            if (texStart_) {
                drawTextureFitted(texStart_, startButtonRect_);
            } else {
                fillRect(startButtonRect_.x, startButtonRect_.y, startButtonRect_.w, startButtonRect_.h, SDL_Color{58, 160, 225, 255});
                drawRectBorder(startButtonRect_.x, startButtonRect_.y, startButtonRect_.w, startButtonRect_.h, SDL_Color{255, 255, 255, 200});
            }
        }

        SDL_Texture* hudTexture = nullptr;
        if (!started_) {
            hudTexture = texStart_;
        } else if (model_.isPaused()) {
            hudTexture = texResume_;
        } else {
            hudTexture = texPause_;
        }

        if (hudTexture) {
            drawTextureFitted(hudTexture, hudButtonRect_);
        } else {
            fillRect(hudButtonRect_.x, hudButtonRect_.y, hudButtonRect_.w, hudButtonRect_.h, SDL_Color{70, 70, 82, 255});
            drawRectBorder(hudButtonRect_.x, hudButtonRect_.y, hudButtonRect_.w, hudButtonRect_.h, SDL_Color{220, 220, 230, 255});
        }

        if (started_ && (model_.isPaused() || !model_.isPlayerAlive())) {
            fillRect(0, kHud, boardWidth, boardHeight, SDL_Color{0, 0, 0, 120});
        }

        SDL_RenderPresent(renderer_);
    }
};

} // namespace bomberman_sdl

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    bomberman_sdl::SdlRenderer app;
    if (!app.init()) {
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
