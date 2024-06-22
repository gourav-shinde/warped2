#include <iostream>
#include <vector>
#include <iterator>
#include <algorithm>

template <typename T>
class CircularVectorIterator {
private:
    std::array<T,2000>* vec;
    typename std::array<T,2000>::iterator current;

public:
    typedef typename std::array<T,2000>::iterator iterator_type;
    typedef typename std::iterator_traits<iterator_type>::iterator_category iterator_category;
    typedef typename std::iterator_traits<iterator_type>::difference_type difference_type;
    typedef typename std::iterator_traits<iterator_type>::value_type value_type;
    typedef typename std::iterator_traits<iterator_type>::pointer pointer;
    typedef typename std::iterator_traits<iterator_type>::reference reference;

    CircularVectorIterator(std::array<T,2000>& v, int current_) : vec(&v), current(v.begin() + current_) {}

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
        return *(*this + n);
    }

    CircularVectorIterator& operator+=(difference_type n) {
        auto sz = vec->size();
        auto dist = std::distance(vec->begin(), current);
        current = vec->begin() + ((dist + n + sz) % sz) % sz;
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
        // std::cerr<<"this called2\n";
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
CircularVectorIterator<T> make_circular_iterator(std::array<T, 2000>& vec, int start) {
    return CircularVectorIterator<T>(vec, start);
}


/*
Description : QuickSort in Iterator format
Created     : 2019/03/04 
Author      : Knight-金 (https://stackoverflow.com/users/3547485)
Link        : https://stackoverflow.com/a/54976413/3547485

Ref: http://www.cs.fsu.edu/~lacher/courses/COP4531/lectures/sorts/slide09.html
*/

template <typename RandomIt, typename Compare>
void QuickSort(RandomIt first, RandomIt last, Compare compare)
{
    if (std::distance(first, last)>1){
        RandomIt bound = Partition(first, last, compare);
        QuickSort(first, bound, compare);
        QuickSort(bound+1, last, compare);
    }
}

template <typename RandomIt, typename Compare>
RandomIt Partition(RandomIt first, RandomIt last, Compare compare)
{
    auto pivot = std::prev(last, 1);
    auto i = first;
    for (auto j = first; j != pivot; ++j){
        // bool format 
        if (compare(*j, *pivot)){
            std::swap(*i++, *j);
        }
    }
    std::swap(*i, *pivot);
    return i;
}
