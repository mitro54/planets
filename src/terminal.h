#pragma once
#include <termios.h>
#include <utility>

class Terminal {
public:
    Terminal();
    ~Terminal();

    void enableRawMode();
    void disableRawMode();

    std::pair<int, int> getSize() const; // {cols, rows}
    int readKey() const;

    static constexpr int KEY_NONE       = -1;
    static constexpr int KEY_UP         = 1000;
    static constexpr int KEY_DOWN       = 1001;
    static constexpr int KEY_LEFT       = 1002;
    static constexpr int KEY_RIGHT      = 1003;
    static constexpr int KEY_SCROLL_UP  = 1004;
    static constexpr int KEY_SCROLL_DN  = 1005;

private:
    struct termios originalTermios_{};
    bool rawMode_ = false;
};
