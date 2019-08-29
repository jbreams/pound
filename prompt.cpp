#include "prompt.h"

size_t OneLinePrompt::lineAllocation() const {
    return 2;
}

stdx::string_view OneLinePrompt::getLine(size_t lineNumber) const {
    if (lineNumber == 0) {
        return _prompt;
    } else if (lineNumber == 1) {
        stdx::string_view res(_result);
        return res.substr(_scrollOffset);
    } else {
        throw PoundException("Tried to read line {} from a one-line prompt"_format(lineNumber));
    }
}

Position OneLinePrompt::cursorPosition() const {
    return {1, _virtualOffset - _scrollOffset};
}

bool OneLinePrompt::showCursor() const {
    return true;
}

void OneLinePrompt::movePosition(Direction dir) {
    switch (dir) {
        case Direction::Left:
            if (_virtualOffset > 0) {
                --_virtualOffset;
            }
            break;
        case Direction::Right:
            if (_virtualOffset < _result.size() + 1) {
                ++_virtualOffset;
            }
            break;
        default:
            throw PoundException("One-line prompts can only move the cursor left and right");
    }

    auto termSize = _term->getTerminalSize();
    if (_virtualOffset < _scrollOffset) {
        _scrollOffset = _virtualOffset;
    } else if (_virtualOffset >= _scrollOffset + termSize.column) {
        _scrollOffset = _virtualOffset - termSize.column + 1;
    }
}

void OneLinePrompt::insert(char ch) {
    if (_virtualOffset > _result.size()) {
        _result.push_back(ch);
    } else {
        _result.insert(_virtualOffset, 1, ch);
    }
}

void OneLinePrompt::erase() {
    _result.erase(_virtualOffset);
}

const std::string& OneLinePrompt::result() const {
    return _result;
}

size_t OneLinePrompt::virtualOffset() const {
    return _virtualOffset;
}

void OneLinePrompt::updatePrompt(std::string newPrompt) {
    _prompt = std::move(newPrompt);
}