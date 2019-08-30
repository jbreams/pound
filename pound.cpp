#include <cctype>
#include <regex>
#include <string>

#include "document.h"
#include "piecetable.h"
#include "pound.h"
#include "prompt.h"
#include "terminal.h"

struct PromptGuard {
    PromptGuard(Terminal* term) : _term(term) {}
    ~PromptGuard() {
        if (_term) {
            _term->endPrompt();
        }
    }

    Terminal* _term = nullptr;
};

std::string doSaveAs(Terminal* term, DocumentBuffer* buffer) {
    auto prompt = std::make_unique<OneLinePrompt>("Save file as:");
    term->startPrompt(prompt.get());
    PromptGuard guard(term);
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

    buffer->save(prompt->result());
    term->setStatusMessage("Successfully saved {}"_format(prompt->result()));
    return prompt->result();
}

void doFind(Terminal* term, DocumentBuffer* buffer) {
    const auto defaultPrompt = "Find: (Press ENTER to begin find)"_sv;
    auto prompt = std::make_unique<OneLinePrompt>(defaultPrompt.to_string());
    term->startPrompt(prompt.get());
    PromptGuard guard(term);
    term->refresh();

    std::regex searchRegex;
    bool regexError = false;
    const auto updateRegex = [&] {
        try {
            searchRegex = std::regex(prompt->result());
            if (regexError) {
                prompt->updatePrompt(defaultPrompt.to_string());
                regexError = false;
            }
        } catch (const std::regex_error& e) {
            auto errorPrompt = "Find (error in regex: {})"_format(e.what());
            prompt->updatePrompt(std::move(errorPrompt));
            regexError = true;
        }
    };

    KeyCodes c;
    for (c = term->readKeyCode(); c != KeyCodes::kNewLine; c = term->readKeyCode()) {
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
                updateRegex();
            }
        } else if (c < KeyCodes::kSpecialKeyCodes && std::isprint(static_cast<char>(c))) {
            prompt->movePosition(Direction::Right);
            prompt->insert(static_cast<char>(c));
            updateRegex();
        }
        term->refresh();
    }

    if (regexError) {
        term->setStatusMessage("Error compiling regex for find");
        term->endPrompt();
        return;
    }

    size_t lineNumber = buffer->virtualPosition().row;
    for (; c == KeyCodes::kNewLine; c = term->readKeyCode()) {
        using RegexIterator = std::regex_iterator<PieceTable::iterator>;
        const auto findEnd = RegexIterator();
        RegexIterator findIt = findEnd;

        while (findIt == findEnd) {
            auto line = buffer->getLine(++lineNumber);
            if (!line) {
                prompt->updatePrompt(
                    "Reached end of file. Press ENTER to start find from beginning.");
                lineNumber = 0;
                term->refresh();
                break;
            }
            findIt = RegexIterator(line->begin(), line->end(), searchRegex);
        }

        for (; findIt != findEnd; ++findIt) {
            Position matchPos(lineNumber, findIt->position());
            prompt->updatePrompt(
                "Found match at row {} column {}"_format(matchPos.row, matchPos.column));
            buffer->setVirtualPosition(matchPos);
            term->refresh();
        }
    }
}

int main(int argc, char** argv) try {
    std::unique_ptr<DocumentBuffer> buffer;
    stdx::optional<std::string> fileName;
    if (argc == 1) {
        buffer = std::make_unique<DocumentBuffer>();
    } else {
        fileName = argv[1];
        buffer = std::make_unique<DocumentBuffer>(*fileName);
    }

    Terminal term(buffer.get());
    term.refresh();

    auto terminalPosToBufferPos = [&] {
        auto pos = buffer->virtualPosition();
        auto line = buffer->getLine(pos.row);
        if (!line) {
            if (pos.row != 0 && pos.column != 0) {
                throw PoundException("Trying to access row {} that does not exist"_format(pos.row));
            }
            return buffer->end();
        }

        auto it = line->begin();
        it += pos.column;
        return it;
    };

    for (auto c = term.readKeyCode(); c != modCtrlKey('q'); c = term.readKeyCode()) {
        if (c == KeyCodes::kArrowUp) {
            buffer->moveVirtualPosition(Direction::Up);
        } else if (c == KeyCodes::kArrowDown) {
            buffer->moveVirtualPosition(Direction::Down);
        } else if (c == KeyCodes::kArrowRight) {
            buffer->moveVirtualPosition(Direction::Right);
        } else if (c == KeyCodes::kArrowLeft) {
            buffer->moveVirtualPosition(Direction::Left);
        } else if (c == KeyCodes::kHome) {
            auto pos = buffer->virtualPosition();
            buffer->moveVirtualPosition(Direction::Left, pos.column);
        } else if (c == KeyCodes::kEnd) {
            auto pos = buffer->virtualPosition();
            auto line = buffer->getLine(pos.row);
            buffer->moveVirtualPosition(Direction::Right, line->size() - pos.column);
        } else if (c == KeyCodes::kPageDown) {
            buffer->moveVirtualPosition(Direction::Down, buffer->allocation().row);
        } else if (c == KeyCodes::kPageUp) {
            buffer->moveVirtualPosition(Direction::Up, buffer->allocation().row);
        } else if (c == KeyCodes::kDelete) {
            buffer->erase(terminalPosToBufferPos());
        } else if (c == KeyCodes::kBackspace) {
            auto pos = buffer->virtualPosition();
            if (pos.column == 0) {
                if (pos.row == 0) {
                    continue;
                }

                auto line = buffer->getLine(pos.row - 1);
                auto it = line->end();
                char prevCh = (it == buffer->end()) ? '\0' : *it;
                while (it != buffer->end()) {
                    buffer->moveVirtualPosition(Direction::Left);
                    it = buffer->erase(it);
                    char curCh = *it;
                    if (!isEOL(curCh) || curCh != prevCh) {
                        break;
                    }
                }
            } else {
                buffer->moveVirtualPosition(Direction::Left);
                buffer->erase(terminalPosToBufferPos());
            }
        } else if (c == modCtrlKey('f')) {
            doFind(&term, buffer.get());
        } else if (c == modCtrlKey('s')) {
            if (fileName) {
                buffer->save(*fileName);
            } else {
                fileName = doSaveAs(&term, buffer.get());
            }
        } else if (c == modCtrlKey('w')) {
            doSaveAs(&term, buffer.get());
        } else if (c < KeyCodes::kSpecialKeyCodes &&
                   (std::isprint(static_cast<char>(c)) || c == KeyCodes::kNewLine)) {
            buffer->insert(terminalPosToBufferPos(), static_cast<char>(c));
            buffer->moveVirtualPosition(Direction::Right);
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