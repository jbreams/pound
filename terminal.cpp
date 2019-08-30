#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <sstream>

#include "pound.h"

#include "piecetable.h"
#include "terminal.h"

Terminal::Terminal(Buffer* buffer) : _buffer(buffer) {
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

    Position bufferAllocation = getTerminalSize();
    bufferAllocation.row -= 1;
    _buffer->setAllocation(bufferAllocation);
}

Terminal::~Terminal() {
    write(escape::kSwitchToMainScreen);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &_oldMode);
}

Position Terminal::getTerminalSize(bool refreshFromTerminal) {
    if (!refreshFromTerminal && _terminalSize) {
        auto termSize = *_terminalSize;
        termSize.row -= _prompt ? _prompt->allocation().row : 1;
        return termSize;
    }

    winsize ws = {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        write(escape::kMoveCursorTo999x999);
        _terminalSize = _getCursorPositionFromTerminal();
    } else {
        _terminalSize = {ws.ws_row, ws.ws_col};
    }

    auto termSize = *_terminalSize;
    termSize.row -= _prompt ? _prompt->allocation().row : 1;
    return termSize;
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
    bufToWrite << escape::kMoveCursorTo1x1;

    auto terminalSize = getTerminalSize();
    size_t lineNumber = 0;

    for (lineNumber = 0; lineNumber <= terminalSize.row; ++lineNumber) {
        auto line = _buffer->getLine(lineNumber);
        if (line) {
            auto it = line->begin();
            size_t colCount = 0;
            for (; it != line->end() && colCount < terminalSize.column; ++it, ++colCount) {
                auto ch = *it;
                if (!isEOL(ch)) {
                    bufToWrite << ch;
                }
            }
        } else {
            bufToWrite << "~"_sv;
        }

        bufToWrite << escape::kEraseRestOfLine << "\r\n"_sv;
    }

    if (!_prompt) {
        auto messageSize = std::min(_statusMessage.size(), terminalSize.column);
        bufToWrite << _statusMessage.substr(0, messageSize);
        bufToWrite << escape::kEraseRestOfLine;

        auto virtualPosition = _buffer->virtualPosition();
        const auto lineStatus =
            " Row: {} Col: {} "_format(virtualPosition.row + 1, virtualPosition.column + 1);

        if (terminalSize.column - messageSize > lineStatus.size()) {
            bufToWrite << fmt::format(escape::kMoveCursorFmt,
                                      terminalSize.row + 2,
                                      terminalSize.column - lineStatus.size());
            bufToWrite << lineStatus;
        }

        auto cursorPosition = _buffer->cursorPosition();
        bufToWrite << fmt::format(
            escape::kMoveCursorFmt, cursorPosition.row + 1, cursorPosition.column + 1);

        bufToWrite << escape::kShowCursor;
    } else {
        for (size_t lineNumber = 0; lineNumber != _prompt->allocation().row; ++lineNumber) {
            auto line = _prompt->getLine(lineNumber);
            auto it = line->begin();
            for (size_t col = 0; col < terminalSize.column && it != line->end(); ++col, ++it) {
                bufToWrite << *it;
            }
            bufToWrite << escape::kEraseRestOfLine;
            if (lineNumber < _prompt->allocation().row - 1) {
                bufToWrite << "\r\n";
            }
        }

        auto cursorPos = _prompt->cursorPosition();
        cursorPos.row += terminalSize.row + 2;

        bufToWrite << fmt::format(escape::kMoveCursorFmt, cursorPos.row, cursorPos.column + 1);
        if (_prompt->showCursor()) {
            bufToWrite << escape::kShowCursor;
        }
    }

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

void Terminal::setStatusMessage(std::string message) {
    _statusMessage = std::move(message);
}

void Terminal::startPrompt(Buffer* prompt) {
    _prompt = prompt;
    auto lineRequest = _prompt->allocationRequest().row;
    _prompt->setAllocation({lineRequest, std::numeric_limits<size_t>::max()});
    auto bufferAllocation = getTerminalSize();
    bufferAllocation.row -= lineRequest;
    _buffer->setAllocation(bufferAllocation);
}

void Terminal::endPrompt() {
    _prompt = nullptr;
    auto bufferAllocation = getTerminalSize();
    bufferAllocation.row -= 1;
    _buffer->setAllocation(bufferAllocation);
}
