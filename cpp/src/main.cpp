#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <ncurses.h>

namespace bomberman {

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

class Game {
public:
    void run() {
        initTerminal();
        resetLevel(1);

        while (running_) {
            const int key = getch();
            handleInput(key);
            update();
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        shutdownTerminal();
    }

private:
    bool running_ = true;
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

    std::mt19937 rng_{std::random_device{}()};

    void initTerminal() {
        initscr();
        cbreak();
        noecho();
        curs_set(0);
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);

        if (has_colors()) {
            start_color();
            use_default_colors();
            init_pair(1, COLOR_WHITE, -1);   // default
            init_pair(2, COLOR_CYAN, -1);    // walls
            init_pair(3, COLOR_YELLOW, -1);  // bricks
            init_pair(4, COLOR_GREEN, -1);   // player
            init_pair(5, COLOR_RED, -1);     // enemies
            init_pair(6, COLOR_MAGENTA, -1); // bomb
            init_pair(7, COLOR_YELLOW, COLOR_RED); // explosion
            init_pair(8, COLOR_BLUE, -1);    // portal
            init_pair(9, COLOR_GREEN, -1);   // items
        }
    }

    void shutdownTerminal() {
        nodelay(stdscr, FALSE);
        endwin();
    }

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

    bool inExplosion(const Vec2& p) const {
        if (!bomb_.active || !bomb_.exploding) {
            return false;
        }

        for (const Vec2& cell : bomb_.explosionCells) {
            if (cell == p) {
                return true;
            }
        }
        return false;
    }

    std::optional<std::string> resolveLevelPath(int level) const {
        const std::string levelFile = "Level" + std::to_string(level) + ".txt";
        const std::array<std::string, 2> candidates = {
            "res/Levels/" + levelFile,
            "../res/Levels/" + levelFile,
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
            running_ = false;
            return;
        }

        spawnEnemiesForLevel(level_);

        player_ = {1, 1};
        playerAlive_ = true;
        portalVisible_ = false;
        paused_ = false;

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

    void handleInput(int key) {
        if (key == ERR) {
            return;
        }

        if (key == 'q' || key == 'Q') {
            running_ = false;
            return;
        }

        if (key == 'p' || key == 'P') {
            paused_ = !paused_;
            return;
        }

        if (!playerAlive_) {
            if (key == 'r' || key == 'R') {
                resetLevel(level_);
            }
            return;
        }

        if (paused_) {
            return;
        }

        if (key == ' ' && !bomb_.active && bombsRemaining_ > 0) {
            bomb_.active = true;
            bomb_.exploding = false;
            bomb_.pos = player_;
            bomb_.placedAt = Clock::now();
            bomb_.explosionCells.clear();
            bombsRemaining_--;
            return;
        }

        if (key == KEY_UP || key == 'w' || key == 'W') {
            tryMovePlayer(Direction::Up);
        } else if (key == KEY_DOWN || key == 's' || key == 'S') {
            tryMovePlayer(Direction::Down);
        } else if (key == KEY_LEFT || key == 'a' || key == 'A') {
            tryMovePlayer(Direction::Left);
        } else if (key == KEY_RIGHT || key == 'd' || key == 'D') {
            tryMovePlayer(Direction::Right);
        }
    }

    void tryMovePlayer(Direction direction) {
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

        bool found = false;
        while (!q.empty() && !found) {
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
                    found = true;
                    break;
                }

                q.push(next);
            }
        }

        if (!visited[index(goal)]) {
            return std::nullopt;
        }

        Vec2 cur = goal;
        while (!(parent[index(cur)] == start)) {
            cur = parent[index(cur)];
            if (cur.x == -1 && cur.y == -1) {
                return std::nullopt;
            }
        }

        return cur;
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

    void drawTile(int y, int x, char glyph, int color) const {
        if (has_colors()) {
            mvaddch(y, x, glyph | COLOR_PAIR(color));
        } else {
            mvaddch(y, x, glyph);
        }
    }

    void draw() const {
        erase();

        mvprintw(0, 0, "C++ Bomberman (Terminal Port) | Level: %d | Time: %d | Bombs: %d | Speed: %d | Flame: %d | Enemies: %zu",
                 level_,
                 std::max(0, timeRemaining_),
                 bombsRemaining_,
                 playerSpeed_,
                 flamePower_,
                 enemies_.size());
        mvprintw(1, 0, "Controls: Arrow/WASD move, Space bomb, P pause, R restart(after death), Q quit");

        const int offsetY = 2;

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const Vec2 p{x, y};
                char glyph = ' ';
                int color = 1;

                const Tile tile = map_[y][x];
                if (tile == Tile::Wall) {
                    glyph = '#';
                    color = 2;
                } else if (tile == Tile::Brick || tile == Tile::SpeedHidden || tile == Tile::FlameHidden) {
                    glyph = '+';
                    color = 3;
                } else if (tile == Tile::SpeedVisible) {
                    glyph = 'S';
                    color = 9;
                } else if (tile == Tile::FlameVisible) {
                    glyph = 'F';
                    color = 9;
                }

                if (portalVisible_ && p == portal_) {
                    glyph = '@';
                    color = 8;
                }

                if (bomb_.active && !bomb_.exploding && p == bomb_.pos) {
                    glyph = 'o';
                    color = 6;
                }

                if (inExplosion(p)) {
                    glyph = '*';
                    color = 7;
                }

                for (const Enemy& enemy : enemies_) {
                    if (!enemy.alive || !(enemy.pos == p)) {
                        continue;
                    }

                    if (enemy.type == EnemyType::Ballom) {
                        glyph = 'B';
                    } else if (enemy.type == EnemyType::Oneal) {
                        glyph = 'N';
                    } else if (enemy.type == EnemyType::Kondoria) {
                        glyph = 'K';
                    } else if (enemy.type == EnemyType::Doll) {
                        glyph = 'D';
                    }
                    color = 5;
                }

                if (player_ == p) {
                    glyph = playerAlive_ ? 'P' : 'X';
                    color = 4;
                }

                drawTile(offsetY + y, x, glyph, color);
            }
        }

        if (paused_) {
            mvprintw(offsetY + height_ + 1, 0, "[PAUSED]");
        } else if (!playerAlive_) {
            mvprintw(offsetY + height_ + 1, 0, "[GAME OVER] Press R to restart this level or Q to quit.");
        } else if (portalVisible_) {
            mvprintw(offsetY + height_ + 1, 0, "Portal is open at (%d, %d). Step on '@' to go to next level.", portal_.x, portal_.y);
        }

        refresh();
    }
};

} // namespace bomberman

int main() {
    bomberman::Game game;
    game.run();
    return 0;
}
