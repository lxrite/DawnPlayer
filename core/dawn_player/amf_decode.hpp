/*
 *    amf_decode.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_AMF_DECODE_HPP
#define DAWN_PLAYER_AMF_DECODE_HPP

#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>

#include "amf_types.hpp"

namespace dawn_player {
namespace amf {

class decode_amf_error : public std::exception {
    const char* _msg;
public:
    explicit decode_amf_error(const char* msg) : _msg(msg) {}
    const char* what() const throw() {
        return this->_msg;
    }
    ~decode_amf_error() throw() {
    }
};

template <typename RandomeAccessIterator>
std::shared_ptr<amf_base> decode_amf(RandomeAccessIterator begin, RandomeAccessIterator end)
{
    return std::get<0>(decode_amf_and_return_iterator(begin, end));
}

template <typename RandomeAccessIterator>
std::pair<std::shared_ptr<amf_base>, RandomeAccessIterator> decode_amf_and_return_iterator(RandomeAccessIterator begin, RandomeAccessIterator end)
{
    assert(begin <= end);
    if(begin == end){
        throw decode_amf_error("Failed to decode.");
    }
    auto value_type_marker = static_cast<std::uint8_t>(*begin);
    RandomeAccessIterator next_iter;
    if (value_type_marker == 0x00) {
        // amf_number
        auto value_ptr = std::make_shared<amf_number>(0.00);
        std::tie(*value_ptr, next_iter) = decode_amf_number_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x01) {
        // amf_boolean
        auto value_ptr = std::make_shared<amf_boolean>(false);
        std::tie(*value_ptr, next_iter) = decode_amf_boolean_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x02) {
        // amf_string
        auto value_ptr = std::make_shared<amf_string>();
        std::tie(*value_ptr, next_iter) = decode_amf_string_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x03) {
        // amf_object
        auto value_ptr = std::make_shared<amf_object>();
        std::tie(*value_ptr, next_iter) = decode_amf_object_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x08) {
        // amf_ecma_array
        auto value_ptr = std::make_shared<amf_ecma_array>();
        std::tie(*value_ptr, next_iter) = decode_amf_ecma_array_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x09) {
        // amf_object_end
        auto value_ptr = std::make_shared<amf_object_end>();
        std::tie(*value_ptr, next_iter) = decode_amf_object_end_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x0a) {
        // amf_strict_array
        auto value_ptr = std::make_shared<amf_strict_array>();
        std::tie(*value_ptr, next_iter) = decode_amf_strict_array_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else if (value_type_marker == 0x0b) {
        // amf_date
        auto value_ptr = std::make_shared<amf_date>(0.00);
        std::tie(*value_ptr, next_iter) = decode_amf_date_and_return_iterator(begin, end);
        return std::make_pair(value_ptr, next_iter);
    }
    else {
        throw decode_amf_error("Failed to decode, meet unsupported value type.");
    }
}

template <typename RandomAccessIterator>
std::pair<amf_number, RandomAccessIterator> decode_amf_number_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end) < 9) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x00) {
        throw decode_amf_error("Failed to decode.");
    }

    // IEEE-754
    union {
        unsigned char from[8];
        double to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[7] = *iter++;
    cvt.from[6] = *iter++;
    cvt.from[5] = *iter++;
    cvt.from[4] = *iter++;
    cvt.from[3] = *iter++;
    cvt.from[2] = *iter++;
    cvt.from[1] = *iter++;
    cvt.from[0] = *iter++;
    return std::make_pair(amf_number(cvt.to), iter);
}

template <typename RandomAccessIterator>
amf_number decode_amf_number(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_number_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_boolean, RandomAccessIterator> decode_amf_boolean_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end) < 2) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x01) {
        throw decode_amf_error("Failed to decode.");
    }
    if (*iter++ == 0) {
        return std::make_pair(amf_boolean(false), iter);
    }
    else {
        return std::make_pair(amf_boolean(true), iter);
    }
}

template <typename RandomAccessIterator>
amf_boolean decode_amf_boolean(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_boolean_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_string, RandomAccessIterator> decode_amf_string_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end) < 3) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x02) {
        throw decode_amf_error("Failed to decode.");
    }
    return impl::decode_amf_string_without_marker_and_return_iterator(iter, end);
}

template <typename RandomAccessIterator>
amf_string decode_amf_string(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_string_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_object, RandomAccessIterator> decode_amf_object_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (begin == end) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x03) {
        throw decode_amf_error("Failed to decode.");
    }
    amf_object obj;
    for (;;) {
        amf_string property_name;
        std::tie(property_name, iter) = impl::decode_amf_string_without_marker_and_return_iterator(iter, end);
        std::shared_ptr<amf_base> value_ptr;
        std::tie(value_ptr, iter) = decode_amf_and_return_iterator(iter, end);
        if (property_name.empty() || value_ptr->get_type() == amf_type::object_end) {
            break;
        }
        obj.push_back(std::make_pair(std::move(property_name), value_ptr));
    };
    return std::make_pair(obj, iter);
}

template <typename RandomAccessIterator>
std::pair<amf_object, RandomAccessIterator> decode_amf_object(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_object_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_ecma_array, RandomAccessIterator> decode_amf_ecma_array_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end) < 5) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x08) {
        throw decode_amf_error("Failed to decode.");
    }
    union {
        char from[4];
        std::uint32_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[3] = *iter++;
    cvt.from[2] = *iter++;
    cvt.from[1] = *iter++;
    cvt.from[0] = *iter++;

    amf_ecma_array ecma_array;
    auto cnt = cvt.to;
    for (auto i = 0u; i < cnt; ++i) {
        amf_string key;
        std::tie(key, iter) = impl::decode_amf_string_without_marker_and_return_iterator(iter, end);
        std::shared_ptr<amf_base> value_ptr;
        std::tie(value_ptr, iter) = decode_amf_and_return_iterator(iter, end);
        if (value_ptr->get_type() == amf_type::object_end) {
            break;
        }
        ecma_array.push_back(std::make_pair(std::move(key), value_ptr));
    }
    return std::make_pair(std::move(ecma_array), iter);
}

template <typename RandomAccessIterator>
amf_ecma_array decode_amf_ecma_array(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_ecma_array_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_object_end, RandomAccessIterator> decode_amf_object_end_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (begin == end) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x09) {
        throw decode_amf_error("Failed to decode.");
    }
    return std::make_pair(amf_object_end(), iter);
}

template <typename RandomAccessIterator>
amf_object_end decode_amf_object_end(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_object_end_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_strict_array, RandomAccessIterator> decode_amf_strict_array_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end)  < 5) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x0a) {
        throw decode_amf_error("Failed to decode.");
    }
    union {
        char from[4];
        std::uint32_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[3] = *iter++;
    cvt.from[2] = *iter++;
    cvt.from[1] = *iter++;
    cvt.from[0] = *iter++;
    auto cnt = cvt.to;
    amf_strict_array strict_array;
    for (auto i = 0u; i < cnt; ++i) {
        std::shared_ptr<amf_base> value_ptr;
        std::tie(value_ptr, iter) = decode_amf_and_return_iterator(iter, end);
        strict_array.push_back(value_ptr);
    }
    return std::make_pair(strict_array, iter);
}

template <typename RandomAccessIterator>
amf_strict_array decode_amf_strict_array(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_strict_array_and_return_iterator(begin, end));
}

template <typename RandomAccessIterator>
std::pair<amf_date, RandomAccessIterator> decode_amf_date_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(begin <= end);
    if (std::distance(begin, end) < 11) {
        throw decode_amf_error("Failed to decode.");
    }
    auto iter = begin;
    if (*iter++ != 0x0b) {
        throw decode_amf_error("Failed to decode.");
    }
    // IEEE-754
    union {
        unsigned char from[8];
        double to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[7] = *iter++;
    cvt.from[6] = *iter++;
    cvt.from[5] = *iter++;
    cvt.from[4] = *iter++;
    cvt.from[3] = *iter++;
    cvt.from[2] = *iter++;
    cvt.from[1] = *iter++;
    cvt.from[0] = *iter++;
    // Ignore time-zone (reserved S16)
    iter += 2;
    return std::make_pair(amf_date(cvt.to), iter);
}

template <typename RandomAccessIterator>
amf_date decode_amf_date(RandomAccessIterator begin, RandomAccessIterator end)
{
    return std::get<0>(decode_amf_date_and_return_iterator(begin, end));
}

namespace impl {

template <typename RandomAccessIterator>
std::pair<amf_string, RandomAccessIterator> decode_amf_string_without_marker_and_return_iterator(RandomAccessIterator begin, RandomAccessIterator end)
{
    assert(std::distance(begin, end) >= 2);
    auto iter = begin;
    union {
        char from[2];
        std::uint16_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    std::uint16_t length = 0;
    cvt.from[1] = *iter++;
    cvt.from[0] = *iter++;
    length = cvt.to;
    if (length == 0) {
        return std::make_pair(amf_string(std::string()), iter);
    }
    else if (std::distance(iter, end) >= length) {
        std::string tmp;
        std::copy(iter, iter + length, std::back_inserter(tmp));
        iter += length;
        return std::make_pair(amf_string(tmp), iter);
    }
    else {
        throw decode_amf_error("Failed to decode.");
    }
}

} // namespace impl
} // namespace amf
} // namespace dawn_player

#endif
