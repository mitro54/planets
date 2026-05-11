#include "terminal.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>

Terminal::Terminal() {}

Terminal::~Terminal() {
    if (rawMode_) disableRawMode();
}

void Terminal::enableRawMode() {
    tcgetattr(STDIN_FILENO, &originalTermios_);
    struct termios raw = originalTermios_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // Hide cursor + clear screen + enable SGR mouse mode (for scroll wheel)
    const char* seq = "\033[?25l\033[2J\033[?1000h\033[?1006h";
    if (write(STDOUT_FILENO, seq, strlen(seq))) {}
    rawMode_ = true;
}

void Terminal::disableRawMode() {
    // Disable mouse + show cursor + reset colors + clear
    const char* seq = "\033[?1006l\033[?1000l\033[?25h\033[0m\033[2J\033[H";
    if (write(STDOUT_FILENO, seq, strlen(seq))) {}
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios_);
    rawMode_ = false;
}

std::pair<int, int> Terminal::getSize() const {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return {80, 24};
    return {ws.ws_col, ws.ws_row};
}

int Terminal::readKey() const {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_NONE;

    if (c != '\033') return c;

    // ESC sequence
    char s1;
    if (read(STDIN_FILENO, &s1, 1) != 1) return '\033';
    if (s1 != '[') return '\033';

    char s2;
    if (read(STDIN_FILENO, &s2, 1) != 1) return '\033';

    // Arrow keys: \033[A .. \033[D
    switch (s2) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
    }

    // SGR mouse event: \033[<btn;x;y M/m
    if (s2 == '<') {
        char buf[32];
        int len = 0;
        char ch;
        while (len < 30 && read(STDIN_FILENO, &ch, 1) == 1) {
            buf[len++] = ch;
            if (ch == 'M' || ch == 'm') break;
        }
        buf[len] = '\0';

        // Parse button number (digits before first ';')
        int btn = 0;
        for (int i = 0; i < len && buf[i] != ';'; ++i)
            if (buf[i] >= '0' && buf[i] <= '9')
                btn = btn * 10 + (buf[i] - '0');

        if (btn == 64) return KEY_SCROLL_UP;
        if (btn == 65) return KEY_SCROLL_DN;
        return KEY_NONE;
    }

    // Consume remaining bytes of unknown CSI sequences
    if (s2 >= '0' && s2 <= '9') {
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1)
            if (ch == '~' || (ch >= 'A' && ch <= 'Z')) break;
    }

    return KEY_NONE;
}
