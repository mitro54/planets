#include "renderer.h"
#include <unistd.h>

void Renderer::resize(int cols, int rows) {
    cols_ = cols;
    rows_ = rows;
    buffer_.assign(rows, std::vector<Cell>(cols));
    output_.reserve(static_cast<size_t>(cols * rows * 8));
}

void Renderer::clear() {
    for (auto& row : buffer_)
        for (auto& cell : row) {
            cell.ch = " ";
            cell.color.clear();
        }
}

void Renderer::putChar(int col, int row, char c, const std::string& color) {
    if (row >= 0 && row < rows_ && col >= 0 && col < cols_) {
        buffer_[row][col].ch = std::string(1, c);
        buffer_[row][col].color = color;
    }
}

void Renderer::putGlyph(int col, int row, const std::string& glyph, const std::string& color) {
    if (row >= 0 && row < rows_ && col >= 0 && col < cols_) {
        buffer_[row][col].ch = glyph;
        buffer_[row][col].color = color;
    }
}

void Renderer::putString(int col, int row, const std::string& s, const std::string& color) {
    for (size_t i = 0; i < s.size(); ++i)
        putChar(col + static_cast<int>(i), row, s[i], color);
}

void Renderer::flush() {
    output_.clear();
    output_ += "\033[H";
    std::string curColor;

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const Cell& cell = buffer_[r][c];
            if (cell.color != curColor) {
                output_ += cell.color.empty() ? "\033[0m" : cell.color;
                curColor = cell.color;
            }
            output_ += cell.ch;  // works for both single-byte and multi-byte
        }
        if (r < rows_ - 1) output_ += "\r\n";
    }

    if (!curColor.empty()) output_ += "\033[0m";
    if (write(STDOUT_FILENO, output_.c_str(), output_.size())) {}
}
