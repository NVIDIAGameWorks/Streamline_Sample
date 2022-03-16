#pragma once

#include <vector>
#include <deque>
#include <forward_list>
#include <list>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <string>
#include <array>
#include <initializer_list>

#include <assert.h>

#include <nvrhi/common/alloc.h>

// defines common std container and string types that use
// NVRHI's HeapContainerAllocator to allocate/free memory

namespace nvrhi {

template <class T>
using vector = std::vector<T, HeapContainerAllocator<T>>;

template <class T>
using deque = std::deque<T, HeapContainerAllocator<T>>;

template <class T>
using forward_list = std::forward_list<T, HeapContainerAllocator<T>>;

template <class T>
using list = std::list<T, HeapContainerAllocator<T>>;

template <class Key,
          class Compare = std::less<Key>>
using set = std::set<Key, Compare, HeapContainerAllocator<Key>>;

template <class Key,
          class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
using unordered_set = std::unordered_set<Key, Hash, KeyEqual, HeapContainerAllocator<Key>>;

template <class Key,
          class Compare = std::less<Key>>
using multiset = std::multiset<Key, Compare, HeapContainerAllocator<Key>>;

template <class Key,
          class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
using unordered_multiset = std::unordered_multiset<Key, Hash, KeyEqual, HeapContainerAllocator<Key>>;

template <class Key,
          class T,
          class Compare = std::less<Key>>
using map = std::map<Key, T, Compare, HeapContainerAllocator<std::pair<const Key, T>>>;

template <class Key,
          class T,
          class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
using unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, HeapContainerAllocator<std::pair<const Key, T>>>;

template <class Key,
          class T,
          class Compare = std::less<Key>>
using multimap = std::multimap<Key, T, Compare, HeapContainerAllocator<std::pair<const Key, T>>>;

template <class Key,
          class T,
          class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
using unordered_multimap = std::unordered_multimap<Key, T, Hash, KeyEqual, HeapContainerAllocator<std::pair<const Key, T>>>;


// defines basic string types that allocate through HeapContainerAllocator
template <class CharT,
          class Traits = std::char_traits<CharT>>
using basic_string = std::basic_string<CharT, Traits, HeapContainerAllocator<CharT>>;

using string = basic_string<char>;
using wstring = basic_string<wchar_t>;
using u16string = basic_string<char16_t>;
using u32string = basic_string<char32_t>;

// a static vector is a vector with a capacity defined at compile-time
template <typename T, uint32_t _max_elements>
struct static_vector : private std::array<T, _max_elements>
{
    typedef std::array<T, _max_elements> base;
    enum { max_elements = _max_elements };

    using typename base::value_type;
    using typename base::size_type;
    using typename base::difference_type;
    using typename base::reference;
    using typename base::const_reference;
    using typename base::pointer;
    using typename base::const_pointer;
    using typename base::iterator;
    using typename base::const_iterator;
    // xxxnsubtil: reverse iterators not implemented

    static_vector()
        : base()
        , current_size(0)
    { }

    static_vector(size_t size)
        : base()
        , current_size(size)
    {
        assert(size < max_elements);
    }

    static_vector(const static_vector& other) = default;

    static_vector(std::initializer_list<T> il)
        : current_size(0)
    {
        for(auto i : il)
            push_back(i);
    }

    using base::at;

    reference operator[] (size_type pos)
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    const_reference operator[] (size_type pos) const
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    using base::front;

    reference back() noexcept                   { auto tmp =  end(); --tmp; return *tmp; }
    const_reference back() const noexcept       { auto tmp = cend(); --tmp; return *tmp; }

    using base::data;
    using base::begin;
    using base::cbegin;

    iterator end() noexcept                     { return iterator(begin()) + current_size; }
    const_iterator end() const noexcept         { return cend(); }
    const_iterator cend() const noexcept        { return const_iterator(cbegin()) + current_size; }

    bool empty() const noexcept                 { return current_size == 0; }
    size_t size() const noexcept                { return current_size; }
    constexpr size_t max_size() const noexcept  { return max_elements; }

    void fill(const T& value) noexcept
    {
        base::fill(value);
        current_size = max_elements;
    }

    void swap(static_vector& other) noexcept
    {
        base::swap(*this);
        std::swap(current_size, other.current_size);
    }

    void push_back(const T& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = value;
        current_size++;
    }

    void push_back(T&& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = std::move(value);
        current_size++;
    }

    void pop_back() noexcept
    {
        assert(current_size > 0);
        current_size--;
    }

    void resize(size_type new_size) noexcept
    {
        assert(new_size <= max_elements);

        if (current_size > new_size)
        {
            for (size_type i = new_size; i < current_size; i++)
                (data() + i)->~T();
        }
        else
        {
            for (size_type i = current_size; i < new_size; i++)
                new (data() + i) T();
        }

        current_size = new_size;
    }

    reference emplace_back() noexcept
    {
        resize(current_size + 1);
        return back();
    }

private:
    size_type current_size = 0;
};

} // namespace nvrhi
