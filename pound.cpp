#include <cctype>
#include <regex>
#include <string>

#include "piecetable.h"
#include "pound.h"
#include "prompt.h"
#include "terminal.h"

std::string doSaveAs(Terminal* term, PieceTable* buffer) {
    auto prompt = std::make_unique<OneLinePrompt>(term, "Save file as:");
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

void doFind(Terminal* term, PieceTable* buffer, Position start) {
    const auto defaultPrompt = "Find: (Press ENTER to begin find)"_sv;
    auto prompt = std::make_unique<OneLinePrompt>(term, defaultPrompt.to_string());
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

    size_t lineNumber = start.row;
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
            term->setVirtualPosition(matchPos);
            term->refresh();
        }
    }
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
        } else if (c == modCtrlKey('f')) {
            doFind(&term, pieceTable.get(), term.getVirtualPosition());
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