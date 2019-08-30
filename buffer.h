#pragma once

#include "pound.h"

class Line;
class BufferStorage {
public:
    class Iterator {
    public:
        typedef ssize_t difference_type;
        typedef char value_type;
        typedef value_type* pointer;
        typedef value_type& reference;
        typedef const value_type& const_reference;
        typedef std::bidirectional_iterator_tag iterator_category;

        class IteratorBase {
        public:
            using const_reference = const char&;
            virtual ~IteratorBase() = default;
            virtual void increment() = 0;
            virtual void decrement() = 0;
            virtual const_reference dereference() const = 0;
            virtual bool equals(const IteratorBase* other) const = 0;
            virtual std::unique_ptr<IteratorBase> clone() const = 0;
        };

        template <typename T, std::enable_if_t<std::is_base_of<IteratorBase, T>::value, int> = 0>
        Iterator(T impl) {
            _impl = std::make_unique<T>(std::move(impl));
        }

        Iterator() = default;
        Iterator(const Iterator& other) {
            if (other.impl()) {
                _impl = other.impl()->clone();
            } else {
                _impl.reset();
            }
        }

        Iterator& operator=(const Iterator& other) {
            if (other.impl()) {
                _impl = other.impl()->clone();
            } else {
                _impl.reset();
            }
            return *this;
        }

        const_reference& operator*() const {
            return _impl->dereference();
        }

        Iterator& operator++() {
            _impl->increment();
            return *this;
        }

        Iterator& operator--() {
            _impl->decrement();
            return *this;
        }

        Iterator operator--(int) {
            auto ret = *this;
            _impl->decrement();
            return ret;
        }

        Iterator operator++(int) {
            auto ret = *this;
            _impl->increment();
            return ret;
        }

        bool operator==(const Iterator& other) const {
            if (_impl && other._impl) {
                return _impl->equals(other._impl.get());
            }
            return false;
        }

        bool operator!=(const Iterator& other) const {
            if (_impl && other._impl) {
                return !_impl->equals(other._impl.get());
            }
            return false;
        }

        const IteratorBase* impl() const {
            return _impl.get();
        }

        IteratorBase* impl() {
            return _impl.get();
        }

    private:
        std::unique_ptr<IteratorBase> _impl;
    };

    virtual ~BufferStorage() = default;
    virtual Iterator begin() = 0;
    virtual const Iterator& end() const = 0;
    virtual stdx::optional<Line> getLine(size_t lineNumber) = 0;
};

class Line {
public:
    using iterator = BufferStorage::Iterator;

    Line(iterator begin, iterator end, iterator nextLine, size_t size)
        : _begin(std::move(begin)),
          _end(std::move(end)),
          _nextLine(std::move(nextLine)),
          _size(size) {}

    iterator begin() const {
        return _begin;
    }

    iterator end() const {
        return _end;
    }

    iterator nextLine() const {
        return _nextLine;
    }

    size_t size() const {
        return _size;
    }

private:
    iterator _begin;
    iterator _end;
    iterator _nextLine;
    size_t _size;
};

class Buffer {
public:
    POUND_NON_COPYABLE_NON_MOVABLE(Buffer);
    Buffer() = default;
    virtual ~Buffer() = default;

    virtual Position allocationRequest() const = 0;
    void setAllocation(Position allocation) {
        _allocation = allocation;
    }
    Position allocation() const {
        return _allocation;
    };

    virtual stdx::optional<Line> getLine(size_t line) = 0;
    virtual Position cursorPosition() const = 0;
    virtual Position virtualPosition() const = 0;
    virtual bool showCursor() const = 0;

private:
    Position _allocation;
};