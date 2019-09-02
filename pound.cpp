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

struct MatchFinder {
    using MatchVector = std::vector<std::pair<size_t, size_t>>;
    MatchFinder(DocumentBuffer* buffer_, MatchVector matches_)
        : buffer(buffer_),
          matches(std::move(matches_)),
          bufferIt(buffer->begin()),
          matchIt(matches.begin()),
          curLine(findLineStartingAt(bufferIt, buffer->end())) {}

    std::pair<Position, Position> next();
    bool more() const;
    void reset();

    DocumentBuffer* buffer;
    MatchVector matches;
    DocumentBuffer::iterator bufferIt;
    MatchVector::iterator matchIt;
    size_t bufferOffset = 0;
    size_t curLineNumber = 0;
    Line curLine;
};

bool MatchFinder::more() const {
    return matchIt != matches.end();
}

void MatchFinder::reset() {
    bufferOffset = 0;
    curLineNumber = 0;
    matchIt = matches.begin();
    bufferIt = buffer->begin();
    curLine = findLineStartingAt(bufferIt, buffer->end());
}

std::pair<Position, Position> MatchFinder::next() {
    stdx::optional<Position> first;
    stdx::optional<Position> last;
    const auto endOffset = matchIt->first + matchIt->second;
    while (bufferIt != buffer->end()) {
        auto advanced = bufferOffset + curLine.size();
        auto curLineStart = bufferOffset;
        if (advanced >= matchIt->first && !first) {
            first = Position(curLineNumber, matchIt->first - curLineStart);
        }

        if (advanced >= endOffset) {
            last = Position(curLineNumber, endOffset - curLineStart);
        }

        if (!first || !last) {
            bufferIt = curLine.end();
            bufferOffset = advanced;
            curLineNumber++;
            curLine = findLineStartingAt(bufferIt, buffer->end());
        } else {
            break;
        }
    }

    if (!first || !last) {
        throw PoundException("Overran document while searching");
    }

    ++matchIt;
    return {*first, *last};
}

void doFind(Terminal* term, DocumentBuffer* buffer) {
    const auto defaultPrompt = "Find: (Press ENTER to begin find)"_sv;
    auto prompt = std::make_unique<OneLinePrompt>(defaultPrompt.to_string());
    term->startPrompt(prompt.get());
    PromptGuard guard(term);
    term->refresh();

    std::regex searchRegex;
    bool regexError = false;
    std::vector<std::pair<size_t, size_t>> matches;

    const auto updateRegex = [&] {
        try {
            searchRegex = std::regex(prompt->result());
            if (regexError) {
                prompt->updatePrompt(defaultPrompt.to_string());
                regexError = false;
            }

            matches.clear();
            using RegexIterator = std::regex_iterator<PieceTable::iterator>;
            const auto findEnd = RegexIterator();
            RegexIterator findIt(buffer->begin(), buffer->end(), searchRegex);
            for (; findIt != findEnd; ++findIt) {
                matches.push_back({findIt->position(), findIt->length()});
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
        return;
    }

    stdx::optional<DocumentBuffer::Decorations::iterator> decorationIt;
    if (matches.empty()) {
        term->setStatusMessage("No results found");
        return;
    }

    MatchFinder finder(buffer, std::move(matches));
    for (; c == KeyCodes::kNewLine; c = term->readKeyCode()) {
        if (decorationIt) {
            buffer->eraseDecoration(*decorationIt);
            decorationIt = stdx::nullopt;
        }
        auto matchRange = finder.next();
        auto matchPos = matchRange.first;
        prompt->updatePrompt(
            "Found match at row {} column {}"_format(matchPos.row + 1, matchPos.column + 1));
        buffer->setVirtualPosition(matchPos);
        decorationIt =
            buffer->addDecoration(matchPos, matchRange.second, escape::kBold.to_string());
        term->refresh();

        if (!finder.more()) {
            finder.reset();
        }
    }

    if (decorationIt) {
        buffer->eraseDecoration(*decorationIt);
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

        auto it = std::next(line->begin(), pos.column);
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
            auto it = terminalPosToBufferPos();
            if (pos.column == 0) {
                if (pos.row == 0) {
                    continue;
                }

                auto prevLine = buffer->getLine(pos.row - 1);
                buffer->setVirtualPosition(
                    Position(pos.row - 1, prevLine->size() - prevLine->lineEndingCount()));
                buffer->erase(std::prev(it));
            } else {
                auto toErase = std::prev(it);
                buffer->moveVirtualPosition(Direction::Left);
                buffer->erase(toErase);
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
        } else if (c == modCtrlKey('z')) {
            buffer->undo();
            buffer->fixVirtualPosition();
        } else if (c < KeyCodes::kSpecialKeyCodes &&
                   (std::isprint(static_cast<char>(c)) || c == KeyCodes::kNewLine)) {
            buffer->insert(terminalPosToBufferPos(), static_cast<char>(c));
            buffer->moveVirtualPosition(Direction::Right);
        } else {
            term.setStatusMessage("Unknown character {}"_format(static_cast<int16_t>(c)));
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