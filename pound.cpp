#include <cctype>
#include <string>

#include "piecetable.h"
#include "pound.h"
#include "terminal.h"

class OneLinePrompt : public Prompt {
public:
    explicit OneLinePrompt(Terminal* term, std::string prompt)
        : _term(term), _prompt(std::move(prompt)) {}

    size_t lines() const override {
        return 2;
    }

    stdx::string_view getLine(size_t lineNumber) const override {
        if (lineNumber == 0) {
            return _prompt;
        } else if (lineNumber == 1) {
            stdx::string_view res(_result);
            return res.substr(_scrollOffset);
        } else {
            throw PoundException("Tried to read line {} from a one-line prompt"_format(lineNumber));
        }
    }

    Position cursorPosition() const override {
        return {1, _virtualOffset - _scrollOffset};
    }

    bool showCursor() const override {
        return true;
    }

    void movePosition(Direction dir) {
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

    void insert(char ch) {
        if (_virtualOffset > _result.size()) {
            _result.push_back(ch);
        } else {
            _result.insert(_virtualOffset, 1, ch);
        }
    }

    void erase() {
        _result.erase(_virtualOffset);
    }

    const std::string& result() const {
        return _result;
    }

    size_t virtualOffset() const {
        return _virtualOffset;
    }

private:
    Terminal* _term;
    std::string _prompt;
    std::string _result;
    size_t _virtualOffset = 0;
    size_t _scrollOffset = 0;
};

std::string doSaveAs(Terminal* term, PieceTable* buffer) {
    auto prompt = std::make_unique<OneLinePrompt>(term, "Save file as:");
    term->startPrompt(prompt.get());
    term->refresh();
    for (auto c = term->readKeyCode(); c != KeyCodes::kNewLine; c = term->readKeyCode()) {
        if (c == KeyCodes::kArrowLeft) {
            prompt->movePosition(Direction::Left);
        } else if (c == KeyCodes::kArrowRight) {
            prompt->movePosition(Direction::Right);
        } else if (c == KeyCodes::kDelete) {
            prompt->erase();
        } else if (c == KeyCodes::kBackspace) {
            if (prompt->virtualOffset() > 0) {
                prompt->movePosition(Direction::Left);
                prompt->erase();
            }
        } else if (c < KeyCodes::kSpecialKeyCodes && std::isprint(static_cast<char>(c))) {
            prompt->movePosition(Direction::Right);
            prompt->insert(static_cast<char>(c));
        }
        term->refresh();
    }

    term->endPrompt();
    buffer->save(prompt->result());
    term->setStatusMessage("Successfully saved {}"_format(prompt->result()));
    return prompt->result();
}

int main(int argc, char** argv) try {
    std::unique_ptr<PieceTable> pieceTable;
    stdx::optional<std::string> fileName;
    if (argc == 1) {
        pieceTable = std::make_unique<PieceTable>();
    } else {
        fileName = argv[1];
        pieceTable = std::make_unique<PieceTable>(*fileName);
    }

    Terminal term(pieceTable.get());
    term.refresh();

    auto terminalPosToBufferPos = [&] {
        auto pos = term.getVirtualPosition();
        auto line = pieceTable->getLine(pos.row);
        if (!line) {
            if (pos.row != 0 && pos.column != 0) {
                throw PoundException("Trying to access row {} that does not exist"_format(pos.row));
            }
            return pieceTable->end();
        }

        auto it = line->begin();
        it += pos.column;
        return it;
    };

    for (auto c = term.readKeyCode(); c != modCtrlKey('q'); c = term.readKeyCode()) {
        if (c == KeyCodes::kArrowUp) {
            term.moveCursor(Direction::Up);
        } else if (c == KeyCodes::kArrowDown) {
            term.moveCursor(Direction::Down);
        } else if (c == KeyCodes::kArrowRight) {
            term.moveCursor(Direction::Right);
        } else if (c == KeyCodes::kArrowLeft) {
            term.moveCursor(Direction::Left);
        } else if (c == KeyCodes::kHome) {
            auto pos = term.getVirtualPosition();
            term.moveCursor(Direction::Left, pos.column);
        } else if (c == KeyCodes::kEnd) {
            auto pos = term.getVirtualPosition();
            auto line = pieceTable->getLine(pos.row);
            term.moveCursor(Direction::Right, line->size() - pos.column);
        } else if (c == KeyCodes::kPageDown) {
            term.moveCursor(Direction::Down, term.getTerminalSize().row);
        } else if (c == KeyCodes::kPageUp) {
            term.moveCursor(Direction::Up, term.getTerminalSize().row);
        } else if (c == KeyCodes::kDelete) {
            pieceTable->erase(terminalPosToBufferPos());
        } else if (c == KeyCodes::kBackspace) {
            auto pos = term.getVirtualPosition();
            if (pos.column == 0) {
                if (pos.row == 0) {
                    continue;
                }

                auto line = pieceTable->getLine(pos.row - 1);
                auto it = line->end();
                char prevCh = (it == pieceTable->end()) ? '\0' : *it;
                while (it != pieceTable->end()) {
                    term.moveCursor(Direction::Left);
                    it = pieceTable->erase(it);
                    char curCh = *it;
                    if (!isEOL(curCh) || curCh != prevCh) {
                        break;
                    }
                }
            } else {
                term.moveCursor(Direction::Left);
                pieceTable->erase(terminalPosToBufferPos());
            }
        } else if (c == modCtrlKey('s')) {
            if (fileName) {
                pieceTable->save(*fileName);
            } else {
                fileName = doSaveAs(&term, pieceTable.get());
            }
        } else if (c == modCtrlKey('w')) {
            doSaveAs(&term, pieceTable.get());
        } else if (c < KeyCodes::kSpecialKeyCodes &&
                   (std::isprint(static_cast<char>(c)) || c == KeyCodes::kNewLine)) {
            pieceTable->insert(terminalPosToBufferPos(), static_cast<char>(c));
            term.moveCursor(Direction::Right);
        } else {
            throw PoundException("Unknown character {}"_format(static_cast<int16_t>(c)));
        }

        term.refresh();
    }

    return 0;
} catch (const PoundException& e) {
    fmt::print(stderr, "Error: {}", e.what());
    if (e.systemError()) {
        fmt::print(stderr, ": {} ({})", e.systemError()->value(), e.systemError()->message());
    }

    fmt::print(stderr, "\n");
    return 1;
}