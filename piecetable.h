#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>

#include "pound.h"

#include "buffer.h"

inline constexpr bool isEOL(char ch) {
    return ch == '\r' || ch == '\n';
}

class PieceTable : public BufferStorage {
public:
    struct Piece {
        enum Type { kOriginal, kAddBuffer };
        Piece(Type type_, size_t start_, size_t length_)
            : type(type_), start(start_), length(length_) {}

        Type type;
        size_t start;
        size_t length;
    };

    using PieceSet = std::list<Piece>;
    using PieceIterator = PieceSet::iterator;

    class IteratorImpl : public Iterator::IteratorBase {
    public:
        IteratorImpl(const PieceTable* table, PieceIterator it, size_t off)
            : _table(table), _it(std::move(it)), _off(off) {}

    protected:
        using const_reference = IteratorBase::const_reference;

        void increment() override;
        void decrement() override;
        const_reference dereference() const override;
        bool equals(const IteratorBase* other) const override;
        std::unique_ptr<IteratorBase> clone() const override;

    private:
        friend class PieceTable;

        bool _isValid() const;

        const PieceTable* _table = nullptr;
        PieceIterator _it;
        size_t _off = 0;
    };

    using iterator = BufferStorage::Iterator;

    explicit PieceTable(const std::string& fileName);
    PieceTable();
    ~PieceTable();

    POUND_NON_COPYABLE_NON_MOVABLE(PieceTable);

    bool dirty() const;
    void save(const std::string& name);

    stdx::optional<Line> getLine(size_t lineNumber) override;

    iterator begin() override;
    const iterator& end() const override;

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
    const IteratorImpl* _itToImpl(const iterator& it);
    off_t _seekOriginal(off_t offset, int whence);
    Line _findEOL(iterator begin);
    iterator _eraseImpl(const iterator& it);
    iterator _insertImpl(const iterator& it, char ch);
    std::pair<PieceIterator, PieceIterator> _splitAt(iterator it);

    bool _dirty = false;
    size_t _sizeTracker = 0;
    PieceSet _pieces;

    const iterator _endIt;
    int _originalFile = -1;
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