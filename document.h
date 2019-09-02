#pragma once

#include <set>

#include "pound.h"

#include "piecetable.h"

class DocumentBuffer : public Buffer, public PieceTable {
public:
    using PieceTable::PieceTable;
    struct Decoration {
        Decoration(Position start_, Position end_, std::string decoration)
            : start(start_), end(end_), decoration(std::move(decoration)) {}

        Decoration(Position start_) : start(start_), end(start_), decoration() {}

        bool operator<(const Decoration& other) const {
            return start < other.start;
        }

        Position start;
        Position end;
        std::string decoration;
    };

    using Decorations = std::multiset<Decoration>;

    DocumentBuffer(const std::string fileName)
        : PieceTable(fileName), _fileName(std::move(fileName)) {}

    bool showCursor() const override {
        return true;
    }

    // By default documents requests the absolute maximum number of rows/cols
    Position allocationRequest() const override {
        return {std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};
    }

    void setFileName(std::string filename);
    bool hasFileName() const;
    void save();

    stdx::optional<Line> getLine(size_t line) override;
    Position cursorPosition() const override;
    Position virtualPosition() const override {
        return _virtualPosition;
    }


    void fixVirtualPosition();

    void moveVirtualPosition(Direction dir, size_t count = 1);
    void setVirtualPosition(Position pos);

    Decorations::iterator addDecoration(Position start, Position end, std::string decoration);
    void eraseDecoration(Decorations::iterator it);
    std::vector<std::string> getDecorationsForTerminal(Position pos) override;

private:
    void _fixScrollOffset();

    Position _scrollOffset;
    Position _virtualPosition;

    Decorations _decorations;
    std::string _fileName;
};