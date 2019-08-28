#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <sstream>

#include "pound.h"

#include "piecetable.h"
#include "terminal.h"

Terminal::Terminal(PieceTable* buffer) : _buffer(buffer) {
    if (!::isatty(STDIN_FILENO)) {
        throw PoundException("stdin is not a terminal");
    }
    termios raw = {};
    tcgetattr(STDIN_FILENO, &raw);
    _oldMode = raw;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    _terminalSize = getTerminalSize();
    write(escape::kSwitchToAlternateScreen);
}

Terminal::~Terminal() {
    write(escape::kSwitchToMainScreen);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &_oldMode);
}

Position Terminal::getTerminalSize(bool refreshFromTerminal) {
    if (!refreshFromTerminal && _terminalSize) {
        return *_terminalSize;
    }

    winsize ws = {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        write(escape::kMoveCursorTo999x999);
        _terminalSize = _getCursorPositionFromTerminal();
    } else {
        _terminalSize = {ws.ws_row, ws.ws_col};
    }

    return *_terminalSize;
}

Position Terminal::_getCursorPositionFromTerminal() {
    std::array<char, 32> buf = {};
    off_t i = 0;
    write(escape::kGetCursorPosition);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = _read();
        if (buf[i] == 'R') {
            break;
        }
    }

    stdx::string_view view(buf.data(), i);
    if (!view.starts_with(escape::kEscapePrefix)) {
        throw PoundException("Invalid terminal position result");
    }

    view.remove_prefix(2);
    size_t rows = 0, columns = 0;
    if (std::sscanf(view.data(), "%lu;%lu", &rows, &columns) == -1) {
        throw PoundException("Invalid terminal position result");
    }

    return {rows, columns};
}

template <typename T>
void Terminal::_write(const T* ptr, size_t size) {
    ssize_t ret = ::write(STDERR_FILENO, ptr, size);
    if (ret != static_cast<ssize_t>(size)) {
        throw std::system_error(errno, std::generic_category());
    }
}

template <typename T>
T Terminal::_read() {
    T val;
    int ret;
    do {
        ret = ::read(STDIN_FILENO, &val, sizeof(val));
    } while (ret != sizeof(val) && errno == EINTR);

    if (ret != sizeof(val)) {
        throw std::system_error(errno, std::generic_category());
    }

    return val;
}

void Terminal::refresh() {
    std::ostringstream bufToWrite;
    bufToWrite << escape::kHideCursor;
    bufToWrite << escape::kHideCursor;
    bufToWrite << escape::kMoveCursorTo1x1;

    auto terminalSize = getTerminalSize();
    size_t lineNumber = 0;
    auto line = _buffer->getLine(_scrollOffset.row);

    for (lineNumber = 0; lineNumber <= terminalSize.row && line;
         lineNumber++, line = _buffer->getLine(_scrollOffset.row + lineNumber)) {

        auto it = line->begin();
        size_t colCount = 0;
        for (; colCount < _scrollOffset.column && it != line->end(); colCount++) {
            ++it;
        }

        for (; it != line->end() && colCount < terminalSize.column; ++it, ++colCount) {
            auto ch = *it;
            if (!isEOL(ch)) {
                bufToWrite << ch;
            }
        }

        bufToWrite << escape::kEraseRestOfLine;
        if (lineNumber < terminalSize.row - 1) {
            bufToWrite << "\r\n";
        }
    }

    for (; lineNumber <= terminalSize.row; lineNumber++) {
        bufToWrite << "~" << escape::kEraseRestOfLine;
        if (lineNumber < terminalSize.row - 1) {
            bufToWrite << "\r\n";
        }
    }

    auto cursorPosition = getCursorPosition();
    bufToWrite << fmt::format(
        escape::kMoveCursorFmt, cursorPosition.row + 1, cursorPosition.column + 1);
    bufToWrite << escape::kShowCursor;
    write(bufToWrite.str());
}

KeyCodes Terminal::readKeyCode() {
    char ch = _read();
    if (ch != '\x1b') {
        if (ch == '\r') {
            return KeyCodes::kNewLine;
        }
        return static_cast<KeyCodes>(ch);
    }

    char seqFirst = _read();
    char seqSecond = _read();
    if (seqFirst == '[') {
        if (seqSecond >= '0' && seqSecond <= '9') {
            char seqThird = _read();
            if (seqThird == '~') {
                switch (seqSecond) {
                    case '1':
                    case '7':
                        return KeyCodes::kHome;
                    case '3':
                        return KeyCodes::kDelete;
                    case '4':
                    case '8':
                        return KeyCodes::kEnd;
                    case '5':
                        return KeyCodes::kPageUp;
                    case '6':
                        return KeyCodes::kPageDown;
                }
            }
        } else {
            switch (seqSecond) {
                case 'A':
                    return KeyCodes::kArrowUp;
                case 'B':
                    return KeyCodes::kArrowDown;
                case 'C':
                    return KeyCodes::kArrowRight;
                case 'D':
                    return KeyCodes::kArrowLeft;
                case 'H':
                    return KeyCodes::kHome;
                case 'F':
                    return KeyCodes::kEnd;
            }
        }
    } else if (seqFirst == 'O') {
        switch (seqSecond) {
            case 'H':
                return KeyCodes::kHome;
            case 'F':
                return KeyCodes::kEnd;
        }
    }

    return KeyCodes::kEscape;
}

Position Terminal::getCursorPosition() const {
    return {_virtualPosition.row - _scrollOffset.row,
            _virtualPosition.column - _scrollOffset.column};
}

Position Terminal::getVirtualPosition() const {
    return _virtualPosition;
}

void Terminal::moveCursor(Direction dir, size_t count) {
    auto termSize = getTerminalSize();
    while (count > 0) {
        switch (dir) {
            case Direction::Up:
                if (_virtualPosition.row > 0) {
                    _virtualPosition.row--;
                    auto line = _buffer->getLine(_virtualPosition.row);
                    if (!line) {
                        throw PoundException(
                            "Could not find line at {}"_format(_virtualPosition.row));
                    }
                    _virtualPosition.column = std::min(_virtualPosition.column, line->size());
                }
                break;
            case Direction::Down: {
                auto nextLine = _buffer->getLine(_virtualPosition.row + 1);
                if (nextLine) {
                    _virtualPosition.row++;
                    _virtualPosition.column = std::min(_virtualPosition.column, nextLine->size());
                }
                break;
            }
            case Direction::Left:
                if (_virtualPosition.column > 0) {
                    _virtualPosition.column--;
                } else if (_virtualPosition.row > 0) {
                    _virtualPosition.row--;
                    auto prevLine = _buffer->getLine(_virtualPosition.row);
                    if (!prevLine) {
                        throw PoundException(
                            "Could not find line at {}"_format(_virtualPosition.row));
                    }
                    _virtualPosition.column = prevLine->size();
                }
                break;
            case Direction::Right: {
                auto line = _buffer->getLine(_virtualPosition.row);
                if (!line) {
                    throw PoundException(
                        "Could not find current line at {}"_format(_virtualPosition.row));
                }

                if (_virtualPosition.column < line->size()) {
                    _virtualPosition.column++;
                    break;
                }
                auto nextLine = _buffer->getLine(_virtualPosition.row + 1);
                if (nextLine) {
                    _virtualPosition.row++;
                    _virtualPosition.column = 0;
                }
                break;
            }
        }

        if (_virtualPosition.row < _scrollOffset.row) {
            _scrollOffset.row = _virtualPosition.row;
        } else if (_virtualPosition.row >= _scrollOffset.row + termSize.row) {
            _scrollOffset.row = _virtualPosition.row - termSize.row + 1;
        }

        if (_virtualPosition.column < _scrollOffset.column) {
            _scrollOffset.column = _virtualPosition.column;
        } else if (_virtualPosition.column >= _scrollOffset.column + termSize.column) {
            _scrollOffset.column = _virtualPosition.column - termSize.column + 1;
        }

        --count;
    }
}