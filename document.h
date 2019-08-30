#pragma once

#include "pound.h"

#include "piecetable.h"

class DocumentBuffer : public Buffer, public PieceTable {
public:
    using PieceTable::PieceTable;

    bool showCursor() const override {
        return true;
    }

    // By default documents requests the absolute maximum number of rows/cols
    Position allocationRequest() const override {
        return {std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};
    }

    stdx::optional<Line> getLine(size_t line) override;
    Position cursorPosition() const override;
    Position virtualPosition() const override {
        return _virtualPosition;
    }

    void moveVirtualPosition(Direction dir, size_t count = 1);
    void setVirtualPosition(Position pos);

private:
    void _fixScrollOffset();

    Position _scrollOffset;
    Position _virtualPosition;
};