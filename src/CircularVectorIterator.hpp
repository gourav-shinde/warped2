#define CIRCULAR_VECTOR_ITERATOR_HPP

#include <vector>
#include <iterator>

#include <iostream>
#include <vector>
#include <iterator>
#include <algorithm>

template <typename T>
class CircularVectorIterator {
private:
    std::vector<T>* vec;
    typename std::vector<T>::iterator current;

public:
    typedef typename std::vector<T>::iterator iterator_type;
    typedef typename std::iterator_traits<iterator_type>::iterator_category iterator_category;
    typedef typename std::iterator_traits<iterator_type>::difference_type difference_type;
    typedef typename std::iterator_traits<iterator_type>::value_type value_type;
    typedef typename std::iterator_traits<iterator_type>::pointer pointer;
    typedef typename std::iterator_traits<iterator_type>::reference reference;

    CircularVectorIterator(std::vector<T>& v, int current_) : vec(&v), current(v.begin() + current_) {}

    CircularVectorIterator& operator++() {
        ++current;
        if (current == vec->end()) {
            current = vec->begin();
        }
        return *this;
    }

    CircularVectorIterator operator++(int) {
        CircularVectorIterator temp = *this;
        ++(*this);
        return temp;
    }

    T& operator*() {
        return *current;
    }

    bool operator==(const CircularVectorIterator& other) const {
        return current == other.current;
    }

    bool operator!=(const CircularVectorIterator& other) const {
        return !(*this == other);
    }

    reference operator[](difference_type n) {
        auto dist = std::distance(vec->begin(), current);
        return *(*this + ((dist + n) % vec->size()));
    }

    CircularVectorIterator& operator+=(difference_type n) {
        auto sz = vec->size();
        auto dist = std::distance(vec->begin(), current);
        current = vec->begin() + ((dist + n) % sz);
        return *this;
    }

    CircularVectorIterator operator+(difference_type n) const {
        CircularVectorIterator temp = *this;
        return temp += n;
    }

    CircularVectorIterator& operator--() {
        if (current == vec->begin()) {
            current = vec->end();
        }
        --current;
        return *this;
    }

    CircularVectorIterator operator--(int) {
        CircularVectorIterator temp = *this;
        --(*this);
        return temp;
    }

    CircularVectorIterator& operator-=(difference_type n) {
        return (*this += -n);
    }

    CircularVectorIterator operator-(difference_type n) const {
        CircularVectorIterator temp = *this;
        return temp -= n;
    }

    difference_type operator-(const CircularVectorIterator& other) const {
        auto sz = vec->size();
        auto dist1 = std::distance(vec->begin(), current);
        auto dist2 = std::distance(vec->begin(), other.current);
        return (dist1 - dist2 + sz) % sz;
    }

    bool operator<(const CircularVectorIterator& other) const {
        return current < other.current;
    }

    bool operator>(const CircularVectorIterator& other) const {
        return current > other.current;
    }

    bool operator<=(const CircularVectorIterator& other) const {
        return current <= other.current;
    }

    bool operator>=(const CircularVectorIterator& other) const {
        return current >= other.current;
    }
};

template <typename T>
CircularVectorIterator<T> make_circular_iterator(std::vector<T>& vec, int start) {
    return CircularVectorIterator<T>(vec, start);
}