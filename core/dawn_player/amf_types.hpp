/*
 *    amf_types.hpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_AMF_TYPES_HPP
#define DAWN_PLAYER_AMF_TYPES_HPP

#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dawn_player {
namespace amf {

enum class amf_type {
    number,
    boolean,
    string,
    object,
    ecma_array,
    object_end,
    strict_array,
    date
};

class amf_base {
public:
    amf_base();
    virtual ~amf_base();
    virtual amf_type get_type() const = 0;
};

class amf_number : public amf_base {
private:
    double _value;
public:
    explicit amf_number(double value);
    virtual ~amf_number();
    virtual amf_type get_type() const override;
    double get_value() const;
};

class amf_boolean : public amf_base {
private:
    bool _value;
public:
    explicit amf_boolean(bool value);
    virtual ~amf_boolean();
    virtual amf_type get_type() const override;
    bool get_value() const;
};

class amf_string : public amf_base {
private:
    std::string _value;
public:
    amf_string();
    explicit amf_string(const std::string& value);
    explicit amf_string(std::string&& value);
    virtual ~amf_string();
    virtual amf_type get_type() const override;
    const std::string& get_value() const;
    bool empty() const;
};

class amf_ecma_array;

class amf_object : public amf_base {
public:
    typedef amf_string property_type;
    typedef std::shared_ptr<amf_base> mapped_value_type;
    typedef std::pair<property_type, mapped_value_type> value_type;
private:
    typedef std::vector<value_type> inner_vector_type;
    inner_vector_type _inner_vector;
public:
    amf_object();
    virtual ~amf_object();
    virtual amf_type get_type() const override;
    void push_back(const value_type& value);
    void push_back(value_type&& value);
    mapped_value_type get_attribute_value(const std::string& attribute_name) const;
    std::shared_ptr<amf_ecma_array> to_ecma_array() const;
};

class amf_ecma_array : public amf_base {
public:
    typedef amf_string key_type;
    typedef std::shared_ptr<amf_base> mapped_type;
    typedef std::pair<key_type, mapped_type> value_type;
private:
    typedef std::vector<value_type> inner_vector_type;
    inner_vector_type _inner_vector;
public:
    typedef inner_vector_type::iterator iterator;
    typedef inner_vector_type::const_iterator const_iterator;
public:
    amf_ecma_array();
    virtual ~amf_ecma_array();
    virtual amf_type get_type() const override;
    void push_back(const value_type& value);
    void push_back(value_type&& value);
    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;
    iterator find(const std::string& key);
    const_iterator find(const std::string& key) const;
};

class amf_object_end : public amf_base {
public:
    amf_object_end();
    virtual ~amf_object_end();
    virtual amf_type get_type() const override;
};

class amf_strict_array : public amf_base {
public:
    typedef std::shared_ptr<amf_base> value_type;
private:
    typedef std::vector<value_type> inner_vector_type;
    inner_vector_type _inner_vector;
public:
    typedef inner_vector_type::size_type size_type;
    typedef inner_vector_type::iterator iterator;
    typedef inner_vector_type::const_iterator const_iterator;
public:
    amf_strict_array();
    virtual ~amf_strict_array();
    virtual amf_type get_type() const override;
    void push_back(const value_type& value);
    void push_back(value_type&& value);
    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;
    size_t size() const;
};

class amf_date : public amf_base {
private:
    double _value;
public:
    explicit amf_date(double value);
    virtual ~amf_date();
    virtual amf_type get_type() const override;
    double get_value() const;
};

} // namespace amf
} // namespace dawn_player

#endif
