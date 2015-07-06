/*
 *    amf_types.cpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "pch.h"

#include <algorithm>
#include <cassert>

#include "amf_types.hpp"

namespace dawn_player {
namespace amf {

amf_base::amf_base()
{
}

amf_base::~amf_base()
{
}

amf_number::amf_number(double value)
    : _value(value)
{
}

amf_number::~amf_number()
{
}

amf_type amf_number::get_type() const
{
    return amf_type::number;
}

double amf_number::get_value() const
{
    return this->_value;
}

amf_boolean::amf_boolean(bool value)
    : _value(value)
{
}

amf_boolean::~amf_boolean()
{
}

amf_type amf_boolean::get_type() const
{
    return amf_type::boolean;
}

bool amf_boolean::get_value() const
{
    return this->_value;
}

amf_string::amf_string()
{
}

amf_string::amf_string(const std::string& value)
{
    if (value.size() > 0xffff) {
        throw std::invalid_argument("string is too long");
    }
    this->_value = value;
}

amf_string::amf_string(std::string&& value)
{
    if (value.size() > 0xffff) {
        throw std::invalid_argument("string is too long");
    }
    this->_value = std::move(value);
}

amf_string::~amf_string()
{
}

amf_type amf_string::get_type() const
{
    return amf_type::string;
}

const std::string& amf_string::get_value() const
{
    return this->_value;
}

bool amf_string::empty() const
{
    return this->_value.empty();
}

amf_object::amf_object()
{
}

amf_object::~amf_object()
{
}

amf_type amf_object::get_type() const
{
    return amf_type::object;
}

void amf_object::push_back(const value_type& value)
{
    assert(!std::get<0>(value).empty());
    this->_inner_vector.push_back(value);
}

void amf_object::push_back(value_type&& value)
{
    assert(!std::get<0>(value).empty());
    this->_inner_vector.push_back(std::move(value));
}

amf_object::mapped_value_type amf_object::get_attribute_value(const std::string& attribute_name) const
{
    auto iter = std::find_if(this->_inner_vector.begin(), this->_inner_vector.end(), 
        [&attribute_name](const value_type& value) -> bool {
            return value.first.get_value() == attribute_name;
        }
    );
    if (iter != this->_inner_vector.end()) {
        return std::get<1>(*iter);
    }
    else {
        return nullptr;
    }
}

std::shared_ptr<amf_ecma_array> amf_object::to_ecma_array() const
{
    auto ecma_array = std::make_shared<amf_ecma_array>();
    for (auto iter = this->_inner_vector.begin(); iter != this->_inner_vector.end(); ++iter) {
        ecma_array->push_back(*iter);
    }
    return ecma_array;
}

amf_ecma_array::amf_ecma_array()
{
}

amf_ecma_array::~amf_ecma_array()
{
}

amf_type amf_ecma_array::get_type() const
{
    return amf_type::ecma_array;
}

void amf_ecma_array::push_back(const value_type& value)
{
    this->_inner_vector.push_back(value);
}

void amf_ecma_array::push_back(value_type&& value)
{
    this->_inner_vector.push_back(std::move(value));
}

amf_ecma_array::iterator amf_ecma_array::begin()
{
    return this->_inner_vector.begin();
}

amf_ecma_array::const_iterator amf_ecma_array::begin() const
{
    return this->_inner_vector.begin();
}

amf_ecma_array::iterator amf_ecma_array::end()
{
    return this->_inner_vector.end();
}

amf_ecma_array::const_iterator amf_ecma_array::end() const
{
    return this->_inner_vector.end();
}

amf_ecma_array::const_iterator amf_ecma_array::cbegin() const
{
    return this->_inner_vector.cbegin();
}

amf_ecma_array::const_iterator amf_ecma_array::cend() const
{
    return this->_inner_vector.cend();
}

amf_ecma_array::iterator amf_ecma_array::find(const std::string& key)
{
    for (auto iter = this->begin(); iter != this->end(); ++iter) {
        if (iter->first.get_value() == key) {
            return iter;
        }
    }
    return this->end();
}

amf_ecma_array::const_iterator amf_ecma_array::find(const std::string& key) const
{
    for (auto iter = this->begin(); iter != this->end(); ++iter) {
        if (iter->first.get_value() == key) {
            return iter;
        }
    }
    return this->end();
}

amf_object_end::amf_object_end()
{
}

amf_object_end::~amf_object_end()
{
}

amf_type amf_object_end::get_type() const
{
    return amf_type::object_end;    
}

amf_strict_array::amf_strict_array()
{
}

amf_strict_array::~amf_strict_array()
{
}

amf_type amf_strict_array::get_type() const
{
    return amf_type::strict_array;
}

void amf_strict_array::push_back(const value_type& value)
{
    this->_inner_vector.push_back(value);
}

void amf_strict_array::push_back(value_type&& value)
{
    this->_inner_vector.push_back(std::move(value));
}

amf_strict_array::iterator amf_strict_array::begin()
{
    return this->_inner_vector.begin();
}

amf_strict_array::const_iterator amf_strict_array::begin() const
{
    return this->_inner_vector.begin();
}

amf_strict_array::iterator amf_strict_array::end()
{
    return this->_inner_vector.end();   
}

amf_strict_array::const_iterator amf_strict_array::end() const
{
    return this->_inner_vector.end();
}

amf_strict_array::const_iterator amf_strict_array::cbegin() const
{
    return this->_inner_vector.cbegin();
}

amf_strict_array::const_iterator amf_strict_array::cend() const
{
    return this->_inner_vector.cend();
}

size_t amf_strict_array::size() const
{
    return this->_inner_vector.size();
}

amf_date::amf_date(double value)
{
}

amf_date::~amf_date()
{
}

amf_type amf_date::get_type() const
{
    return amf_type::date;
}

double amf_date::get_value() const
{
    return this->_value;
}

} // namespace amf
} // namespace dawn_player
