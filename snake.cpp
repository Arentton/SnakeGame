#include <iostream>
#include <string>
#include <deque>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

constexpr int WIDTH = 30;
constexpr int HEIGHT = 20;
constexpr int TICK_MS = 120;

volatile std::sig_atomic_t g_interrupted = 0;

void onSignal(int) { g_interrupted = 1; }

enum class Direction { Up, Down, Left, Right };

struct Point {
    int x;
    int y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

class RawTerminal {
public:
    RawTerminal() {
        tcgetattr(STDIN_FILENO, &saved_);
        termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        flags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags_ | O_NONBLOCK);
        std::cout << "\033[2J\033[?25l" << std::flush;
    }

    ~RawTerminal() {
        std::cout << "\033[?25h" << std::flush;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        fcntl(STDIN_FILENO, F_SETFL, flags_);
    }

    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

private:
    termios saved_;
    int flags_;
};

int readByte() {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return -1;
}

class Game {
public:
    Game() : rng_(std::random_device{}()) {
        snake_.push_back({WIDTH / 2, HEIGHT / 2});
        snake_.push_back({WIDTH / 2 - 1, HEIGHT / 2});
        snake_.push_back({WIDTH / 2 - 2, HEIGHT / 2});
        spawnFood();
    }

    int run() {
        while (!over_ && !g_interrupted) {
            handleInput();
            update();
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
        }
        return score_;
    }

private:
    std::deque<Point> snake_;
    Direction dir_ = Direction::Right;
    Direction nextDir_ = Direction::Right;
    Point food_{0, 0};
    int score_ = 0;
    bool over_ = false;
    std::mt19937 rng_;

    void spawnFood() {
        std::uniform_int_distribution<int> dx(0, WIDTH - 1);
        std::uniform_int_distribution<int> dy(0, HEIGHT - 1);
        while (true) {
            Point p{dx(rng_), dy(rng_)};
            bool onSnake = false;
            for (const auto& s : snake_) {
                if (s == p) {
                    onSnake = true;
                    break;
                }
            }
            if (!onSnake) {
                food_ = p;
                return;
            }
        }
    }

    void turn(Direction d) {
        switch (d) {
            case Direction::Up:    if (dir_ != Direction::Down)  nextDir_ = d; break;
            case Direction::Down:  if (dir_ != Direction::Up)    nextDir_ = d; break;
            case Direction::Left:  if (dir_ != Direction::Right) nextDir_ = d; break;
            case Direction::Right: if (dir_ != Direction::Left)  nextDir_ = d; break;
        }
    }

    void handleInput() {
        int key;
        while ((key = readByte()) != -1) {
            switch (key) {
                case 'w': case 'W': turn(Direction::Up); break;
                case 's': case 'S': turn(Direction::Down); break;
                case 'a': case 'A': turn(Direction::Left); break;
                case 'd': case 'D': turn(Direction::Right); break;
                case 'q': case 'Q': over_ = true; break;
                case 27:
                    if (readByte() == '[') {
                        switch (readByte()) {
                            case 'A': turn(Direction::Up); break;
                            case 'B': turn(Direction::Down); break;
                            case 'C': turn(Direction::Right); break;
                            case 'D': turn(Direction::Left); break;
                            default: break;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void update() {
        if (over_) return;
        dir_ = nextDir_;
        Point head = snake_.front();
        switch (dir_) {
            case Direction::Up:    --head.y; break;
            case Direction::Down:  ++head.y; break;
            case Direction::Left:  --head.x; break;
            case Direction::Right: ++head.x; break;
        }

        if (head.x < 0 || head.x >= WIDTH || head.y < 0 || head.y >= HEIGHT) {
            over_ = true;
            return;
        }

        bool eating = (head == food_);
        auto end = eating ? snake_.end() : snake_.end() - 1;
        for (auto it = snake_.begin(); it != end; ++it) {
            if (*it == head) {
                over_ = true;
                return;
            }
        }

        snake_.push_front(head);
        if (eating) {
            ++score_;
            spawnFood();
        } else {
            snake_.pop_back();
        }
    }

    void draw() const {
        std::string out = "\033[H";
        out += '+';
        out.append(WIDTH, '-');
        out += "+\n";
        for (int y = 0; y < HEIGHT; ++y) {
            out += '|';
            for (int x = 0; x < WIDTH; ++x) {
                Point p{x, y};
                char c = ' ';
                if (p == food_) {
                    c = '*';
                } else if (p == snake_.front()) {
                    c = 'O';
                } else {
                    for (const auto& s : snake_) {
                        if (s == p) {
                            c = 'o';
                            break;
                        }
                    }
                }
                out += c;
            }
            out += "|\n";
        }
        out += '+';
        out.append(WIDTH, '-');
        out += "+\n";
        out += "Score: " + std::to_string(score_) + "   WASD/arrows - move, Q - quit\n";
        std::cout << out << std::flush;
    }
};

}

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    int score = 0;
    {
        RawTerminal terminal;
        Game game;
        score = game.run();
    }

    std::cout << "\nGame over. Score: " << score << "\n";
    return 0;
}