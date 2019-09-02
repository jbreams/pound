#include "prompt.h"

class OneLinePrompt::PromptStorage::IteratorImpl : public BufferStorage::Iterator::IteratorBase {
public:
    IteratorImpl(std::string::iterator it) : _it(std::move(it)) {}

    void increment() override {
        ++_it;
    }

    void decrement() override {
        --_it;
    }

    const_reference dereference() const override {
        return *_it;
    }

    bool equals(const IteratorBase* otherPtr) const override {
        const auto& other = *(static_cast<const IteratorImpl*>(otherPtr));
        return _it == other._it;
    }

    std::unique_ptr<IteratorBase> clone() const override {
        return std::make_unique<IteratorImpl>(_it);
    }

private:
    std::string::iterator _it;
};

stdx::optional<Line> OneLinePrompt::PromptStorage::getLine(size_t lineNumber) {
    if (lineNumber == 0) {
        return Line(IteratorImpl(_prompt.begin()), IteratorImpl(_prompt.end()), _prompt.size());
    } else if (lineNumber == 1) {
        return Line(IteratorImpl(_result.begin()), IteratorImpl(_result.end()), _result.size());
    } else {
        throw PoundException("Cannot get line number {} from one-line prompt"_format(lineNumber));
    }
}

OneLinePrompt::PromptStorage::PromptStorage(std::string prompt)
    : _endIt(IteratorImpl(_result.end())), _prompt(std::move(prompt)) {}

BufferStorage::Iterator OneLinePrompt::PromptStorage::begin() {
    return IteratorImpl(_result.begin());
}

stdx::optional<Line> OneLinePrompt::getLine(size_t lineNumber) {
    auto line = _storage.getLine(lineNumber);
    if (_scrollOffset) {
        auto begin = std::next(line->begin(), _scrollOffset);
        return Line(begin, line->end(), line->size() - _scrollOffset);
    } else {
        return line;
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
            if (_virtualOffset < _storage.result()->size() + 1) {
                ++_virtualOffset;
            }
            break;
        default:
            throw PoundException("One-line prompts can only move the cursor left and right");
    }

    if (_virtualOffset < _scrollOffset) {
        _scrollOffset = _virtualOffset;
    } else if (_virtualOffset >= _scrollOffset + allocation().column) {
        _scrollOffset = _virtualOffset - allocation().column + 1;
    }
}

void OneLinePrompt::insert(char ch) {
    if (_virtualOffset > _storage.result()->size()) {
        _storage.result()->push_back(ch);
    } else {
        _storage.result()->insert(_virtualOffset, 1, ch);
    }
}

void OneLinePrompt::erase() {
    _storage.result()->erase(_virtualOffset);
}

std::string& OneLinePrompt::result() {
    return *_storage.result();
}

size_t OneLinePrompt::virtualOffset() const {
    return _virtualOffset;
}

void OneLinePrompt::updatePrompt(std::string newPrompt) {
    _storage.updatePrompt(std::move(newPrompt));
}