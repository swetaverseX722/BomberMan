#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace bomberman_gfx {

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
            // Safe fallback to default empty map if file is missing.
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

} // namespace bomberman_gfx

@interface GameView : NSView
@end

@implementation GameView {
    bomberman_gfx::GameModel _game;
    NSTimer* _timer;
    CGFloat _tile;
    CGFloat _hud;
    BOOL _started;
    NSRect _hudButtonRect;
    NSRect _startButtonRect;
    NSImage* _startButtonImage;
    NSImage* _pauseButtonImage;
    NSImage* _resumeButtonImage;
    NSImage* _bombImage;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _tile = 32.0;
        _hud = 72.0;
        _started = NO;

        _timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                  target:self
                                                selector:@selector(tick:)
                                                userInfo:nil
                                                 repeats:YES];

        _startButtonImage = [self loadImageFromRelativePath:@"images/startButton.png"];
        _pauseButtonImage = [self loadImageFromRelativePath:@"images/pauseButton.png"];
        _resumeButtonImage = [self loadImageFromRelativePath:@"images/resumeButton.png"];
        _bombImage = [self loadImageFromRelativePath:@"sprites/bomb.png"];
    }
    return self;
}

- (void)dealloc {
    [_timer invalidate];
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (NSImage*)loadImageFromRelativePath:(NSString*)relativePath {
    NSString* cwd = [[NSFileManager defaultManager] currentDirectoryPath];
    NSArray<NSString*>* candidates = @[
        [cwd stringByAppendingPathComponent:[@"res/" stringByAppendingString:relativePath]],
        [cwd stringByAppendingPathComponent:[@"../res/" stringByAppendingString:relativePath]],
        [cwd stringByAppendingPathComponent:[@"../../res/" stringByAppendingString:relativePath]],
        [cwd stringByAppendingPathComponent:[@"../../../res/" stringByAppendingString:relativePath]],
    ];

    for (NSString* path in candidates) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
            if (image) {
                return image;
            }
        }
    }

    return nil;
}

- (void)updateUiRects {
    CGFloat boardW = _game.width() * _tile;
    CGFloat boardH = _game.height() * _tile;
    _hudButtonRect = NSMakeRect(boardW - 172, 12, 160, 48);
    _startButtonRect = NSMakeRect((boardW - 280) / 2.0, _hud + (boardH - 96) / 2.0, 280, 96);
}

- (void)drawButtonImage:(NSImage*)image inRect:(NSRect)rect fallback:(NSColor*)fallbackColor {
    if (image) {
        [image drawInRect:rect];
    } else {
        [fallbackColor setFill];
        NSRectFill(rect);
        [[NSColor colorWithCalibratedWhite:0.95 alpha:0.8] setStroke];
        NSBezierPath* border = [NSBezierPath bezierPathWithRect:rect];
        [border stroke];
    }
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self.window makeFirstResponder:self];
}

- (NSColor*)tileColorAtX:(int)x y:(int)y {
    using namespace bomberman_gfx;

    if (_game.isExplosionAt(x, y)) {
        return [NSColor colorWithCalibratedRed:0.95 green:0.25 blue:0.15 alpha:1.0];
    }

    const Tile tile = _game.tileAt(x, y);
    switch (tile) {
        case Tile::Wall:
            return [NSColor colorWithCalibratedRed:0.12 green:0.35 blue:0.65 alpha:1.0];
        case Tile::Brick:
        case Tile::SpeedHidden:
        case Tile::FlameHidden:
            return [NSColor colorWithCalibratedRed:0.55 green:0.33 blue:0.14 alpha:1.0];
        case Tile::SpeedVisible:
            return [NSColor colorWithCalibratedRed:0.10 green:0.80 blue:0.25 alpha:1.0];
        case Tile::FlameVisible:
            return [NSColor colorWithCalibratedRed:0.95 green:0.55 blue:0.05 alpha:1.0];
        case Tile::Empty:
            return [NSColor colorWithCalibratedRed:0.12 green:0.12 blue:0.12 alpha:1.0];
    }
}

- (void)drawEntityCircleAt:(bomberman_gfx::Vec2)pos color:(NSColor*)color inset:(CGFloat)inset {
    NSRect r = NSMakeRect(pos.x * _tile + inset,
                          _hud + pos.y * _tile + inset,
                          _tile - (2 * inset),
                          _tile - (2 * inset));
    [color setFill];
    [[NSBezierPath bezierPathWithOvalInRect:r] fill];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    [self updateUiRects];

    [[NSColor colorWithCalibratedRed:0.05 green:0.05 blue:0.06 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    NSDictionary* hudAttr = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.93 green:0.93 blue:0.95 alpha:1.0],
    };

    NSString* hud = [NSString stringWithFormat:@"Level %d   Time %d   Bombs %d   Speed %d   Flame %d   Enemies %lu",
                     _game.level(),
                     _game.timeRemaining(),
                     _game.bombsRemaining(),
                     _game.speed(),
                     _game.flamePower(),
                     (unsigned long)_game.enemyCount()];
    [hud drawAtPoint:NSMakePoint(12, 10) withAttributes:hudAttr];

    NSString* help = @"Move: Arrow/WASD   Bomb: Space   Pause: P   Restart: R   Quit: Q";
    [help drawAtPoint:NSMakePoint(12, 34)
       withAttributes:@{
           NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
           NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.70 green:0.70 blue:0.75 alpha:1.0],
       }];

    NSImage* hudButtonImage = nil;
    if (!_started) {
        hudButtonImage = _startButtonImage;
    } else if (_game.isPaused()) {
        hudButtonImage = _resumeButtonImage;
    } else {
        hudButtonImage = _pauseButtonImage;
    }
    [self drawButtonImage:hudButtonImage inRect:_hudButtonRect fallback:[NSColor colorWithCalibratedRed:0.30 green:0.30 blue:0.35 alpha:1.0]];

    for (int y = 0; y < _game.height(); ++y) {
        for (int x = 0; x < _game.width(); ++x) {
            NSRect tileRect = NSMakeRect(x * _tile, _hud + y * _tile, _tile, _tile);
            [[self tileColorAtX:x y:y] setFill];
            NSRectFill(tileRect);

            [[NSColor colorWithCalibratedWhite:0.0 alpha:0.18] setStroke];
            NSBezierPath* grid = [NSBezierPath bezierPathWithRect:tileRect];
            [grid stroke];
        }
    }

    if (_game.isPortalVisible()) {
        NSRect portalRect = NSMakeRect(_game.portal().x * _tile + 8,
                                       _hud + _game.portal().y * _tile + 8,
                                       _tile - 16,
                                       _tile - 16);
        [[NSColor colorWithCalibratedRed:0.35 green:0.85 blue:1.0 alpha:1.0] setFill];
        NSBezierPath* diamond = [NSBezierPath bezierPath];
        [diamond moveToPoint:NSMakePoint(NSMidX(portalRect), NSMinY(portalRect))];
        [diamond lineToPoint:NSMakePoint(NSMaxX(portalRect), NSMidY(portalRect))];
        [diamond lineToPoint:NSMakePoint(NSMidX(portalRect), NSMaxY(portalRect))];
        [diamond lineToPoint:NSMakePoint(NSMinX(portalRect), NSMidY(portalRect))];
        [diamond closePath];
        [diamond fill];
    }

    if (_game.hasBomb()) {
        NSRect bombRect = NSMakeRect(_game.bombPosition().x * _tile + 4,
                                     _hud + _game.bombPosition().y * _tile + 4,
                                     _tile - 8,
                                     _tile - 8);
        if (_bombImage) {
            [_bombImage drawInRect:bombRect];
        } else {
            [self drawEntityCircleAt:_game.bombPosition()
                               color:[NSColor colorWithCalibratedRed:0.95 green:0.15 blue:0.25 alpha:1.0]
                               inset:7.0];
        }
    }

    for (const bomberman_gfx::Enemy& enemy : _game.enemies()) {
        NSColor* color = [NSColor colorWithCalibratedRed:0.88 green:0.22 blue:0.30 alpha:1.0];
        switch (enemy.type) {
            case bomberman_gfx::EnemyType::Ballom:
                color = [NSColor colorWithCalibratedRed:0.86 green:0.16 blue:0.55 alpha:1.0];
                break;
            case bomberman_gfx::EnemyType::Oneal:
                color = [NSColor colorWithCalibratedRed:0.90 green:0.20 blue:0.18 alpha:1.0];
                break;
            case bomberman_gfx::EnemyType::Kondoria:
                color = [NSColor colorWithCalibratedRed:0.87 green:0.46 blue:0.15 alpha:1.0];
                break;
            case bomberman_gfx::EnemyType::Doll:
                color = [NSColor colorWithCalibratedRed:0.62 green:0.22 blue:0.90 alpha:1.0];
                break;
        }
        [self drawEntityCircleAt:enemy.pos color:color inset:6.0];
    }

    [self drawEntityCircleAt:_game.player()
                       color:_game.isPlayerAlive()
                                 ? [NSColor colorWithCalibratedRed:0.10 green:0.85 blue:0.28 alpha:1.0]
                                 : [NSColor colorWithCalibratedRed:0.70 green:0.70 blue:0.70 alpha:1.0]
                       inset:6.0];

    if (!_started) {
        NSRect overlay = NSMakeRect(0, _hud, _game.width() * _tile, _game.height() * _tile);
        [[NSColor colorWithCalibratedWhite:0.0 alpha:0.55] setFill];
        NSRectFill(overlay);
        [self drawButtonImage:_startButtonImage inRect:_startButtonRect fallback:[NSColor colorWithCalibratedRed:0.26 green:0.55 blue:0.85 alpha:1.0]];
    }

    if (_started && (_game.isPaused() || !_game.isPlayerAlive())) {
        NSRect overlay = NSMakeRect(0, _hud, _game.width() * _tile, _game.height() * _tile);
        [[NSColor colorWithCalibratedWhite:0.0 alpha:0.45] setFill];
        NSRectFill(overlay);

        NSString* message = _game.isPaused()
                                ? @"PAUSED"
                                : @"GAME OVER - Press R to restart";
        NSDictionary* msgAttr = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:30],
            NSForegroundColorAttributeName: [NSColor whiteColor],
        };

        NSSize size = [message sizeWithAttributes:msgAttr];
        NSPoint p = NSMakePoint((overlay.size.width - size.width) / 2.0,
                                _hud + (overlay.size.height - size.height) / 2.0);
        [message drawAtPoint:p withAttributes:msgAttr];
    }
}

- (void)keyDown:(NSEvent*)event {
    NSString* chars = [[event charactersIgnoringModifiers] lowercaseString];
    const unichar ch = chars.length > 0 ? [chars characterAtIndex:0] : 0;

    if (!_started) {
        if (ch == 'q') {
            [NSApp terminate:nil];
            return;
        }
        if (ch == 's' || ch == ' ' || ch == '\r' || event.keyCode == 36) {
            _started = YES;
            [self setNeedsDisplay:YES];
        }
        return;
    }

    switch (event.keyCode) {
        case 123: // left
            _game.move(bomberman_gfx::Direction::Left);
            [self setNeedsDisplay:YES];
            return;
        case 124: // right
            _game.move(bomberman_gfx::Direction::Right);
            [self setNeedsDisplay:YES];
            return;
        case 125: // down
            _game.move(bomberman_gfx::Direction::Down);
            [self setNeedsDisplay:YES];
            return;
        case 126: // up
            _game.move(bomberman_gfx::Direction::Up);
            [self setNeedsDisplay:YES];
            return;
        default:
            break;
    }

    if (ch == 'a') {
        _game.move(bomberman_gfx::Direction::Left);
    } else if (ch == 'd') {
        _game.move(bomberman_gfx::Direction::Right);
    } else if (ch == 'w') {
        _game.move(bomberman_gfx::Direction::Up);
    } else if (ch == 's') {
        _game.move(bomberman_gfx::Direction::Down);
    } else if (ch == ' ') {
        _game.placeBomb();
    } else if (ch == 'p') {
        _game.togglePause();
    } else if (ch == 'r') {
        _game.restartLevel();
    } else if (ch == 'q') {
        [NSApp terminate:nil];
    }

    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (!_started) {
        if (NSPointInRect(point, _startButtonRect) || NSPointInRect(point, _hudButtonRect)) {
            _started = YES;
        }
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, _hudButtonRect)) {
        _game.togglePause();
        [self setNeedsDisplay:YES];
    }
}

- (void)tick:(NSTimer*)timer {
    (void)timer;
    if (_started) {
        _game.update();
    }
    [self setNeedsDisplay:YES];
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* _window;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    const CGFloat tile = 32.0;
    const CGFloat hud = 72.0;
    const CGFloat width = 25.0 * tile;
    const CGFloat height = hud + 15.0 * tile;

    NSRect rect = NSMakeRect(0, 0, width, height);

    _window = [[NSWindow alloc] initWithContentRect:rect
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];

    _window.title = @"Bomberman C++ Graphics";
    [_window center];

    GameView* view = [[GameView alloc] initWithFrame:rect];
    [_window setContentView:view];
    [_window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app setDelegate:delegate];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }

    return 0;
}
