#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>

#include "pound.h"

inline constexpr bool isEOL(char ch) {
    return ch == '\r' || ch == '\n';
}

class PieceTable {
public:
    struct Piece {
        enum Type { Original, AddBuffer };
        Piece(Type type_, size_t start_, size_t length_)
            : type(type_), start(start_), length(length_) {}

        Type type;
        size_t start;
        size_t length;
    };

    using PieceSet = std::list<Piece>;
    using PieceIterator = PieceSet::iterator;

    class Line;
    class iterator {
    public:
        iterator() = default;
        char operator*() const;
        iterator& operator++();
        iterator& operator--();

        friend bool operator==(const iterator& a, const iterator& b) {
            if (!a._isValid() && !b._isValid()) {
                return true;
            }

            return (a._table == b._table) && (a._it == b._it) && (a._off == b._off);
        }

        friend bool operator!=(const iterator& a, const iterator& b) {
            return !(a == b);
        }

    private:
        friend class PieceTable;
        friend class PieceTable::Line;
        iterator(const PieceTable* table, PieceIterator it, size_t off)
            : _table(table), _it(std::move(it)), _off(off) {}

        bool _isValid() const;

        const PieceTable* _table = nullptr;
        PieceIterator _it;
        size_t _off = 0;
    };

    explicit PieceTable(const std::string& fileName);
    PieceTable();
    ~PieceTable();

    POUND_NON_COPYABLE_NON_MOVABLE(PieceTable);

    void save(const std::string& name);

    class Line {
    public:
        const iterator& begin() const {
            return _begin;
        }

        const iterator& end() const {
            return _end;
        }

        const iterator& nextLine() const {
            return _nextLine;
        }

        size_t size() const {
            return _size;
        }

    private:
        friend class PieceTable;
        Line(iterator begin, iterator end, iterator nextLine, size_t size)
            : _begin(std::move(begin)),
              _end(std::move(end)),
              _nextLine(std::move(nextLine)),
              _size(size) {}

        iterator _begin;
        iterator _end;
        iterator _nextLine;
        size_t _size;
    };

    stdx::optional<Line> getLine(size_t lineNumber);

    iterator begin();
    const iterator& end() const;

    size_t size() const;

    iterator insert(const iterator& it, char ch);
    iterator erase(const iterator& it);

    // These methods are available for testing.
    const PieceSet& table() const {
        return _pieces;
    }

    const std::string& addBuffer() const {
        return _addBuffer;
    }

    stdx::string_view originalFileView() const {
        return _originalFileView;
    }

private:
    off_t _seekOriginal(off_t offset, int whence);
    Line _makeLine(const Line& start);
    Line _findEOL(iterator begin);
    iterator _eraseImpl(const iterator& it);

    size_t _sizeTracker = 0;
    PieceSet _pieces;

    const iterator _endIt;
    int _originalFile;
    char* _originalFileMapping = nullptr;
    stdx::string_view _originalFileView;

    std::string _addBuffer;

    std::map<size_t, Line> _lineCache;
};

template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
PieceTable::iterator& operator+=(PieceTable::iterator& it, T off) {
    while (off-- > 0) {
        ++it;
    }

    return it;
}

template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
PieceTable::iterator& operator-=(PieceTable::iterator& it, T off) {
    while (off-- > 0) {
        --it;
    }

    return it;
}