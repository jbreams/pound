#pragma once

#include "pound.h"

#include "terminal.h"

class OneLinePrompt : public Buffer {
public:
    explicit OneLinePrompt(Terminal* term, std::string prompt)
        : _term(term), _prompt(std::move(prompt)) {
        _term->startPrompt(this);
    }

    ~OneLinePrompt() {
        _term->endPrompt();
    }

    size_t lineAllocation() const override;
    stdx::string_view getLine(size_t lineNumber) const override;
    Position cursorPosition() const override;
    bool showCursor() const override;

    void movePosition(Direction dir);
    void insert(char ch);
    void erase();
    const std::string& result() const;
    size_t virtualOffset() const;
    void updatePrompt(std::string prompt);

private:
    Terminal* _term;
    std::string _prompt;
    std::string _result;
    size_t _virtualOffset = 0;
    size_t _scrollOffset = 0;
};