#include "document.h"

stdx::optional<Line> DocumentBuffer::getLine(size_t lineNumber) {
    lineNumber += _scrollOffset.row;
    auto line = PieceTable::getLine(lineNumber);
    if (!line) {
        return stdx::nullopt;
    }

    auto begin = std::next(line->begin(), _scrollOffset.column);
    auto size = line->size() - _scrollOffset.column;
    return Line(begin, line->end(), size);
}

Position DocumentBuffer::cursorPosition() const {
    return {_virtualPosition.row - _scrollOffset.row,
            _virtualPosition.column - _scrollOffset.column};
}

void DocumentBuffer::moveVirtualPosition(Direction dir, size_t count) {
    while (count > 0) {
        switch (dir) {
            case Direction::Up:
                if (_virtualPosition.row > 0) {
                    _virtualPosition.row--;
                    auto line = PieceTable::getLine(_virtualPosition.row);
                    if (!line) {
                        throw PoundException(
                            "Could not find line at {}"_format(_virtualPosition.row));
                    }
                    _virtualPosition.column = std::min(_virtualPosition.column, line->size());
                }
                break;
            case Direction::Down: {
                auto nextLine = PieceTable::getLine(_virtualPosition.row + 1);
                if (nextLine) {
                    _virtualPosition.row++;
                    _virtualPosition.column = std::min(_virtualPosition.column, nextLine->size());
                }
                break;
            }
            case Direction::Left:
                if (_virtualPosition.column > 0) {
                    _virtualPosition.column--;
                } else if (_virtualPosition.row > 0) {
                    _virtualPosition.row--;
                    auto prevLine = PieceTable::getLine(_virtualPosition.row);
                    if (!prevLine) {
                        throw PoundException(
                            "Could not find line at {}"_format(_virtualPosition.row));
                    }
                    _virtualPosition.column = prevLine->size();
                }
                break;
            case Direction::Right: {
                auto line = PieceTable::getLine(_virtualPosition.row);
                if (!line) {
                    throw PoundException(
                        "Could not find current line at {}"_format(_virtualPosition.row));
                }

                if (_virtualPosition.column < line->size()) {
                    _virtualPosition.column++;
                    break;
                }
                auto nextLine = PieceTable::getLine(_virtualPosition.row + 1);
                if (nextLine) {
                    _virtualPosition.row++;
                    _virtualPosition.column = 0;
                }
                break;
            }
        }

        _fixScrollOffset();
        --count;
    }
}

void DocumentBuffer::setVirtualPosition(Position pos) {
    auto line = PieceTable::getLine(pos.row);
    if (!line) {
        throw PoundException("Cannot set cursor row beyond end of file");
    }
    if (line) {
        if (pos.column > line->size()) {
            throw PoundException("Cannot set cursor column beyond end of line");
        }
    }

    _virtualPosition = pos;
    _fixScrollOffset();
}

void DocumentBuffer::_fixScrollOffset() {
    if (_virtualPosition.row < _scrollOffset.row) {
        _scrollOffset.row = _virtualPosition.row;
    } else if (_virtualPosition.row >= _scrollOffset.row + allocation().row) {
        _scrollOffset.row = _virtualPosition.row - allocation().row + 1;
    }

    if (_virtualPosition.column < _scrollOffset.column) {
        _scrollOffset.column = _virtualPosition.column;
    } else if (_virtualPosition.column >= _scrollOffset.column + allocation().column) {
        _scrollOffset.column = _virtualPosition.column - allocation().column + 1;
    }
}

DocumentBuffer::Decorations::iterator DocumentBuffer::addDecoration(Position start,
                                                                    Position end,
                                                                    std::string decoration) {
    return _decorations.emplace(start, end, decoration);
}

void DocumentBuffer::eraseDecoration(DocumentBuffer::Decorations::iterator it) {
    _decorations.erase(it);
}

std::vector<std::string> DocumentBuffer::getDecorationsForTerminal(Position pos) {
    Decorations::iterator decorationBegin, decorationsEnd;
    pos.row += _scrollOffset.row;
    pos.column += _scrollOffset.column;

    std::vector<std::string> ret;
    for (const auto& decoration : _decorations) {
        if ((decoration.start < pos || decoration.start == pos) && pos < decoration.end) {
            ret.emplace_back(decoration.decoration);
        }
    }

    return ret;
}