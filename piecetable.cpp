#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <iostream>

#include "piecetable.h"

PieceTable::PieceTable(const std::string& fileName) : _endIt(IteratorImpl(this, _pieces.end(), 0)) {
    _originalFile = ::open(fileName.data(), O_RDONLY);
    if (_originalFile == -1) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        if (err == std::errc::no_such_file_or_directory) {
            _pieces.emplace_front(Piece::kAddBuffer, 0, 0);
            return;
        }
        throw PoundException("Error opening file {}"_format(fileName), err);
    }

    size_t originalFileSize = _seekOriginal(0, SEEK_END);
    if (originalFileSize == 0) {
        ::close(_originalFile);
        _originalFile = -1;
        return;
    }

    _seekOriginal(0, SEEK_SET);
    _originalFileMapping = reinterpret_cast<char*>(
        ::mmap(nullptr, originalFileSize, PROT_READ, MAP_PRIVATE, _originalFile, 0));
    if (_originalFileMapping == nullptr) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException("Error mapping original file {}"_format(fileName), err);
    }
    _originalFileView = stdx::string_view(_originalFileMapping, originalFileSize);
    _sizeTracker = originalFileSize;
    _pieces.emplace_back(Piece{Piece::kOriginal, 0, originalFileSize});
}

PieceTable::PieceTable() : _endIt(IteratorImpl(this, _pieces.end(), 0)) {}

PieceTable::~PieceTable() {
    if (_originalFile != -1) {
        ::close(_originalFile);
    }

    if (_originalFileMapping) {
        ::munmap(_originalFileMapping, _originalFileView.size());
        _originalFileMapping = nullptr;
        _originalFileView = {};
    }
}

size_t PieceTable::size() const {
    return _sizeTracker;
}

bool PieceTable::IteratorImpl::_isValid() const {
    return _table && _it != _table->_pieces.end();
}

PieceTable::IteratorImpl::const_reference PieceTable::IteratorImpl::dereference() const {
    if (!_isValid()) {
        throw PoundException("Cannot dereference invalid piece table iterator");
    }

    switch (_it->type) {
        case Piece::kAddBuffer:
            return _table->_addBuffer.at(_it->start + _off);
        case Piece::kOriginal:
            return _table->_originalFileView.at(_it->start + _off);
    }
}

void PieceTable::IteratorImpl::increment() {
    if (!_isValid()) {
        throw PoundException("Cannot increment invalid piece table iterator");
    }

    ++_off;
    if (_off == _it->length) {
        ++_it;
        _off = 0;
    }
}

void PieceTable::IteratorImpl::decrement() {
    if (!_table) {
        throw PoundException("Cannot decrement invalid piece table iterator");
    }
    if (_it == _table->_pieces.end() && !_table->_pieces.empty()) {
        --_it;
        _off = _it->length;
    }

    if (_off == 0) {
        if (_it != _table->_pieces.begin()) {
            --_it;
            _off = _it->length - 1;
        }
    } else {
        --_off;
    }
}

bool PieceTable::IteratorImpl::equals(const BufferStorage::Iterator::IteratorBase* otherPtr) const {
    const auto& other = *(static_cast<const IteratorImpl*>(otherPtr));
    return _table == other._table && _it == other._it && _off == other._off;
}

std::unique_ptr<BufferStorage::Iterator::IteratorBase> PieceTable::IteratorImpl::clone() const {
    return std::make_unique<IteratorImpl>(_table, _it, _off);
}

PieceTable::iterator PieceTable::begin() {
    return IteratorImpl(this, _pieces.begin(), 0);
}

const PieceTable::iterator& PieceTable::end() const {
    return _endIt;
}

const PieceTable::IteratorImpl* PieceTable::_itToImpl(const iterator& it) {
    auto ptr = static_cast<const IteratorImpl*>(it.impl());
    if (!ptr) {
        ptr = static_cast<const IteratorImpl*>(_endIt.impl());
    }
    return ptr;
}

PieceTable::iterator PieceTable::erase(const iterator& extIt) {
    _lineCache.clear();
    return _eraseImpl(extIt);
}

PieceTable::iterator PieceTable::insert(const iterator& extIt, char ch) {
    _lineCache.clear();
    return _insertImpl(extIt, ch);
}

std::pair<PieceTable::PieceIterator, PieceTable::PieceIterator> PieceTable::_splitAt(
    PieceTable::iterator it) {
    auto itImpl = _itToImpl(it);
    auto pieceIt = itImpl->_it;
    if (pieceIt == _pieces.end()) {
        return {std::prev(pieceIt), pieceIt};
    }
    auto offsetWithinPiece = itImpl->_off;
    auto splitLength = pieceIt->length - offsetWithinPiece;

    if (splitLength == 0) {
        return {pieceIt, std::next(pieceIt)};
    } else if (splitLength == pieceIt->length) {
        return {std::prev(pieceIt), pieceIt};
    }

    auto next = std::next(pieceIt);
    Piece toInsert{pieceIt->type, pieceIt->start + offsetWithinPiece, splitLength};
    next = _pieces.emplace(next, std::move(toInsert));
    pieceIt->length -= splitLength;
    return {pieceIt, next};
}

PieceTable::iterator PieceTable::_eraseImpl(const iterator& extIt) {
    auto extItImpl = _itToImpl(extIt);
    const auto& it = extItImpl->_it;
    auto offsetWithinPiece = extItImpl->_off;

    if (it == _pieces.end()) {
        return end();
    }

    auto isTrivialErase = (it->type == Piece::kAddBuffer) && (it->length > 0) &&
        (it->start + it->length == _addBuffer.size()) && (offsetWithinPiece == it->length);
    _sizeTracker--;
    _dirty = true;
    if (isTrivialErase) {
        it->length--;
        _addBuffer.pop_back();
        return IteratorImpl(this, it, offsetWithinPiece--);
    } else {
        PieceIterator before, after;
        std::tie(before, after) = _splitAt(extIt);

        if (after != _pieces.end()) {
            if (after->length == 1) {
                after = _pieces.erase(after);
            } else {
                after->start++;
                after->length--;
            }
        } else if (before->length > 1) {
            before->length--;
        } else {
            _pieces.erase(before);
        }

        return IteratorImpl(this, after, 0);
    }
}

PieceTable::iterator PieceTable::_insertImpl(const PieceTable::iterator& extIt, char ch) {
    PieceIterator retIt;
    size_t retOffset = 0;

    _addBuffer.push_back(ch);
    Piece toInsert{Piece::kAddBuffer, _addBuffer.size() - 1, 1};
    _dirty = true;

    auto prevPoint = extIt != begin() ? std::prev(extIt) : extIt;
    const auto extItImpl = _itToImpl(prevPoint);
    Piece& prevPiece = *extItImpl->_it;
    auto offsetWithinPiece = extItImpl->_off + 1;

    auto isTrivialAppend = (prevPiece.type & Piece::kAddBuffer) &&
        (prevPiece.start + prevPiece.length == _addBuffer.size() - 1) &&
        ((offsetWithinPiece == prevPiece.length || prevPiece.length == 0));
    if (isTrivialAppend) {
        prevPiece.length++;
        retIt = extItImpl->_it;
        retOffset = prevPiece.length - 1;
    } else {
        PieceIterator before, after;
        std::tie(before, after) = _splitAt(extIt);
        retIt = _pieces.emplace(after, std::move(toInsert));
    }

    _sizeTracker++;
    return IteratorImpl(this, std::move(retIt), retOffset);
}

off_t PieceTable::_seekOriginal(off_t offset, int whence) {
    auto ret = ::lseek(_originalFile, offset, whence);
    if (ret == -1) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException("Error seeking original file", err);
    }
    return ret;
}

Line PieceTable::_findEOL(PieceTable::iterator it) {
    auto begin = it;
    size_t size = 0;
    while (it != end() && !isEOL(*it)) {
        ++it;
        ++size;
    }

    auto lineEnd = it;
    while (it != end() && isEOL(*it)) {
        auto prevCh = *it;
        ++it;

        if (it == end() || *it == prevCh) {
            break;
        }
    }

    return Line{begin, lineEnd, it, size};
}

stdx::optional<Line> PieceTable::getLine(size_t lineNumber) {
    if (_pieces.empty()) {
        return stdx::nullopt;
    }

    auto lineIt = _lineCache.find(lineNumber);
    if (lineIt != _lineCache.end()) {
        return lineIt->second;
    }

    decltype(_lineCache)::iterator lowerBoundIt;
    if (_lineCache.empty()) {
        std::tie(lowerBoundIt, std::ignore) = _lineCache.emplace(0, _findEOL(begin()));
    } else {
        lowerBoundIt = _lineCache.lower_bound(lineNumber);
        if (lowerBoundIt != _lineCache.begin() && lowerBoundIt != _lineCache.end()) {
            --lowerBoundIt;
        } else {
            lowerBoundIt = _lineCache.begin();
        }
    }

    auto it = lowerBoundIt->second.nextLine();
    auto curLineNumber = lowerBoundIt->first;
    decltype(_lineCache)::iterator insertedIt = lowerBoundIt;

    while (it != end() && curLineNumber != lineNumber) {
        std::tie(insertedIt, std::ignore) = _lineCache.emplace(++curLineNumber, _findEOL(it));
        it = insertedIt->second.nextLine();
    }

    if (curLineNumber != lineNumber) {
        return stdx::nullopt;
    }

    return insertedIt->second;
}

struct FileCloser {
    explicit FileCloser(int fd_) : fd(fd_) {}

    POUND_NON_COPYABLE_NON_MOVABLE(FileCloser);

    ~FileCloser() {
        close();
    }

    void close() {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

    int fd = -1;
};

void PieceTable::save(const std::string& file) {
    const auto fullFileName = "{}.XXXXX"_format(file);
    int fd = ::mkstemp(const_cast<char*>(fullFileName.c_str()));
    FileCloser fdGuard(fd);

    if (fd == -1) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException("Error opening file {} while trying to save"_format(file));
    }

    for (const auto& piece : _pieces) {
        char* toWrite = nullptr;
        size_t count = 0;
        switch (piece.type) {
            case Piece::kAddBuffer:
                toWrite = &_addBuffer.at(piece.start);
                count = piece.length;
                break;
            case Piece::kOriginal:
                toWrite = const_cast<char*>(&_originalFileView.at(piece.start));
                count = piece.length;
                break;
        }

        auto res = ::write(fd, toWrite, count);
        if (res != count) {
            auto err = std::make_error_code(static_cast<std::errc>(errno));
            throw PoundException("Error writing to file {} while trying to save"_format(file));
        }
    }

    auto flushRes = ::fsync(fd);
    if (flushRes != 0) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException("Error flush temp file {} while trying to save"_format(fullFileName));
    }
    ::close(fd);
    fd = -1;

    auto renameRes = ::rename(fullFileName.c_str(), file.c_str());
    if (renameRes != 0) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException(
            "Error renaming temp file {} to {} while trying to save"_format(fullFileName, file));
    }

    _dirty = false;
}

bool PieceTable::dirty() const {
    return _dirty;
}