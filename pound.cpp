#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "piecetable.h"
#include "pound.h"
#include "terminal.h"

int main(int argc, char** argv) try {
    std::unique_ptr<PieceTable> pieceTable;
    if (argc == 1) {
        pieceTable = std::make_unique<PieceTable>();
    } else {
        stdx::string_view file(argv[1]);
        pieceTable = std::make_unique<PieceTable>(file);
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
            pieceTable->save("newfile");
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