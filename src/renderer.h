#pragma once
#include <string>
#include <vector>

struct Cell {
    std::string ch = " ";  // single char or multi-byte UTF-8 glyph
    std::string color;
};

class Renderer {
public:
    Renderer() = default;

    void resize(int cols, int rows);
    void clear();
    void putChar(int col, int row, char c, const std::string& color = "");
    void putGlyph(int col, int row, const std::string& glyph, const std::string& color = "");
    void putString(int col, int row, const std::string& s, const std::string& color = "");
    void flush();

    int getCols() const { return cols_; }
    int getRows() const { return rows_; }

private:
    int cols_ = 0, rows_ = 0;
    std::vector<std::vector<Cell>> buffer_;
    std::string output_;
};
