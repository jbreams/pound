#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <iostream>

#include "piecetable.h"

PieceTable::PieceTable(const std::string& fileName) : _endIt(this, _pieces.end(), 0) {
    _originalFile = ::open(fileName.data(), O_RDONLY);
    if (_originalFile == -1) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        if (err == std::errc::no_such_file_or_directory) {
            _pieces.emplace_front(Piece::AddBuffer, 0, 0);
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
    _pieces.emplace_back(Piece{Piece::Original, 0, originalFileSize});
}

PieceTable::PieceTable() : _endIt(this, _pieces.end(), 0) {}

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

bool PieceTable::iterator::_isValid() const {
    return _table && _it != _table->_pieces.end();
}

char PieceTable::iterator::operator*() const {
    if (!_isValid()) {
        throw PoundException("Cannot dereference invalid piece table iterator");
    }

    switch (_it->type) {
        case Piece::AddBuffer:
            return _table->_addBuffer.at(_it->start + _off);
        case Piece::Original:
            return _table->_originalFileView.at(_it->start + _off);
    }
}

PieceTable::iterator& PieceTable::iterator::operator++() {
    if (!_isValid()) {
        throw PoundException("Cannot advance invalid piece table iterator");
    }

    _off++;
    if (_off == _it->length) {
        _it++;
        _off = 0;
    }

    return *this;
}

PieceTable::iterator& PieceTable::iterator::operator--() {
    if (_it == _table->_pieces.end() && !_table->_pieces.empty()) {
        --_it;
        _off = _it->length;
    }

    if (!_isValid()) {
        throw PoundException("Cannot decrement invalid piece table iterator");
    }

    if (_off == 0) {
        if (_it != _table->_pieces.begin()) {
            --_it;
            _off = _it->length - 1;
        }
    } else {
        _off--;
    }

    return *this;
}

PieceTable::iterator PieceTable::begin() {
    return iterator(this, _pieces.begin(), 0);
}

const PieceTable::iterator& PieceTable::end() const {
    return _endIt;
}

PieceTable::iterator PieceTable::insert(const PieceTable::iterator& extIt, char ch) {
    auto it = extIt._it;
    auto offsetWithinPiece = extIt._off;
    if (it == _pieces.end()) {
        --it;
        if (it == _pieces.end()) {
            it = _pieces.emplace(_pieces.end(), Piece{Piece::AddBuffer, 0, 0});
        }
        offsetWithinPiece = it->length;
    } else if (!extIt._isValid()) {
        throw PoundException("Trying to insert character with invalid iterator");
    }

    PieceIterator retIt;
    size_t retOffset = 0;

    _addBuffer.push_back(ch);
    Piece toInsert{Piece::AddBuffer, _addBuffer.size() - 1, 1};
    _lineCache.clear();

    auto isTrivialAppend = (!isEOL(ch)) && (it->type == Piece::AddBuffer) &&
        (it->start + it->length == _addBuffer.size() - 1) &&
        ((offsetWithinPiece == it->length || it->length == 0));
    if (isTrivialAppend) {
        it->length++;
        retIt = it;
        retOffset = it->length - 1;
    } else {
        auto splitLength = it->length - offsetWithinPiece;
        auto next = it;
        ++next;
        if (splitLength == 0) {
            retIt = _pieces.emplace(next, toInsert);
        } else if (splitLength < it->length) {
            next =
                _pieces.emplace(next, Piece{it->type, it->start + offsetWithinPiece, splitLength});
            it->length -= splitLength;
            retIt = _pieces.emplace(next, toInsert);
        } else if (splitLength == it->length) {
            retIt = _pieces.emplace(it, toInsert);
        }
    }

    _sizeTracker++;
    return iterator(this, std::move(retIt), retOffset);
}

PieceTable::iterator PieceTable::erase(const iterator& extIt) {
    _lineCache.clear();
    return _eraseImpl(extIt);
}

PieceTable::iterator PieceTable::_eraseImpl(const iterator& extIt) {
    const auto& it = extIt._it;
    auto offsetWithinPiece = extIt._off;
    if (it == _pieces.end()) {
        return end();
    }

    auto isTrivialErase = (it->type == Piece::AddBuffer) && (it->length > 0) &&
        (it->start + it->length == _addBuffer.size()) && (offsetWithinPiece == it->length);
    _sizeTracker--;
    if (isTrivialErase) {
        it->length--;
        _addBuffer.pop_back();
        return iterator(this, it, offsetWithinPiece--);
    } else {
        PieceIterator next = it;
        ++next;
        auto splitLength = it->length - offsetWithinPiece;

        if (splitLength > 1) {
            next = _pieces.emplace(
                next, Piece{it->type, it->start + offsetWithinPiece + 1, splitLength - 1});
        }
        it->length -= splitLength;
        if (it->length == 0) {
            _pieces.erase(it);
        }

        if (next != _pieces.end()) {
            return iterator(this, next, 0);
        }
    }

    return begin();
}

off_t PieceTable::_seekOriginal(off_t offset, int whence) {
    auto ret = ::lseek(_originalFile, offset, whence);
    if (ret == -1) {
        auto err = std::make_error_code(static_cast<std::errc>(errno));
        throw PoundException("Error seeking original file", err);
    }
    return ret;
}

PieceTable::Line PieceTable::_findEOL(PieceTable::iterator it) {
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

stdx::optional<PieceTable::Line> PieceTable::getLine(size_t lineNumber) {
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
            case Piece::AddBuffer:
                toWrite = &_addBuffer.at(piece.start);
                count = piece.length;
                break;
            case Piece::Original:
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
}