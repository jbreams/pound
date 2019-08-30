#pragma once

#include "pound.h"

#include "terminal.h"

class OneLinePrompt : public Buffer {
public:
    explicit OneLinePrompt(std::string prompt) : _storage(std::move(prompt)) {}

    // By default documents requests the absolute maximum number of rows/cols
    Position allocationRequest() const override {
        return {2, std::numeric_limits<size_t>::max()};
    }

    stdx::optional<Line> getLine(size_t lineNumber) override;
    Position cursorPosition() const override;
    Position virtualPosition() const override {
        return {1, virtualOffset()};
    }
    bool showCursor() const override;

    void movePosition(Direction dir);
    void insert(char ch);
    void erase();
    std::string& result();
    size_t virtualOffset() const;
    void updatePrompt(std::string prompt);

private:
    struct PromptStorage : public BufferStorage {
    public:
        PromptStorage(std::string prompt);

        class IteratorImpl;

        stdx::optional<Line> getLine(size_t lineNumber) override;

        std::string* result() {
            return &_result;
        }

        Iterator begin() override;
        const Iterator& end() const override {
            return _endIt;
        }

        void updatePrompt(std::string prompt) {
            _prompt = std::move(prompt);
        }

    private:
        std::string _prompt;
        std::string _result;
        Iterator _endIt;
    };

    PromptStorage _storage;
    size_t _virtualOffset = 0;
    size_t _scrollOffset = 0;
};