#pragma once

#include <termios.h>

#include <map>

#include "piecetable.h"
#include "pound.h"

enum class KeyCodes : uint16_t {
    kNewLine = 10,
    kBackspace = 127,
    kSpecialKeyCodes = 256,
    kArrowUp = 256,
    kArrowDown,
    kArrowRight,
    kArrowLeft,
    kPageUp,
    kPageDown,
    kHome,
    kEnd,
    kDelete,
    kEscape,
};

template <typename CharT, typename std::enable_if_t<std::is_integral<CharT>::value, int> = 0>
inline bool operator==(const KeyCodes keyCode, const CharT other) {
    return (keyCode < KeyCodes::kArrowUp) && (static_cast<CharT>(keyCode) == other);
}

template <typename CharT, typename std::enable_if_t<std::is_integral<CharT>::value, int> = 0>
inline bool operator!=(const KeyCodes keyCode, const CharT other) {
    return (keyCode >= KeyCodes::kArrowUp) || (static_cast<CharT>(keyCode) != other);
}

constexpr inline KeyCodes modCtrlKey(char ch) {
    return static_cast<KeyCodes>(ch & 0x1f);
}

namespace fmt {
template <>
struct formatter<KeyCodes> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const KeyCodes& p, FormatContext& ctx) {
        if (p < KeyCodes::kArrowUp) {
            return format_to(ctx.begin(), "{0:d}", static_cast<uint16_t>(p));
        } else {
            return format_to(ctx.begin(), "{0:d} ('{0:c}')", static_cast<uint16_t>(p));
        }
    }
};
}  // namespace fmt

namespace escape {
constexpr auto kEscapePrefix = "\x1b["_sv;
constexpr auto kEraseScreen = "\x1b[2J"_sv;
constexpr auto kEraseRestOfLine = "\x1b[K"_sv;
constexpr auto kMoveCursorTo1x1 = "\x1b[H"_sv;
constexpr auto kMoveCursorTo999x999 = "\x1b[999C\x1b[999B"_sv;
constexpr auto kGetCursorPosition = "\x1b[6n"_sv;
constexpr auto kHideCursor = "\x1b[?25l"_sv;
constexpr auto kShowCursor = "\x1b[?25h"_sv;
constexpr auto kSwitchToAlternateScreen = "\x1b[?1049h"_sv;
constexpr auto kSwitchToMainScreen = "\x1b[?1049l"_sv;

// This formats an escape sequence to move the cursor. The arguments are row,
// column
constexpr auto kMoveCursorFmt = "\x1b[{0:d};{1:d}H"_sv;
}  // namespace escape

class Terminal {
public:
    explicit Terminal(PieceTable* buffer);

    POUND_NON_COPYABLE_NON_MOVABLE(Terminal);

    ~Terminal();

    void setLineFactory(PieceTable* buffer) {
        _buffer = buffer;
    }

    KeyCodes readKeyCode();

    template <typename T = char>
    void write(T val) {
        _write(&val, sizeof(val));
    }

    void write(stdx::string_view val) {
        _write(val.data(), val.size());
    }

    void write(const std::string& val) {
        return write(stdx::string_view(val));
    }

    Position getCursorPosition() const;
    Position getVirtualPosition() const;
    void moveCursor(Direction dir, size_t count = 1);
    Position getTerminalSize(bool refreshFromTerminal = false);

    void refresh();

private:
    Position _getCursorPositionFromTerminal();
    template <typename T>
    void _write(const T* ptr, size_t size);

    template <typename T = char>
    T _read();

    PieceTable* _buffer;
    termios _oldMode;
    stdx::optional<Position> _terminalSize;
    Position _scrollOffset;
    Position _virtualPosition;
};