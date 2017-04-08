/*
 *    error.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_ERROR_HPP
#define DAWN_PLAYER_ERROR_HPP

#include <exception>
#include <string>

namespace dawn_player
{

enum class open_error_code {
    io_error,
    parse_error,
    cancel,
    other,
};

enum class get_sample_error_code {
    end_of_stream,
    io_error,
    parse_error,
    cancel,
    other,
};

class open_error : public std::exception {
    std::string what_msg;
    open_error_code error_code;
public:
    open_error(const std::string& what_arg, open_error_code ec);
    virtual const char* what() const;
    open_error_code code() const;
};

class get_sample_error : public std::exception {
    std::string what_msg;
    get_sample_error_code error_code;
public:
    get_sample_error(const std::string& what_arg, get_sample_error_code ec);
    virtual const char* what() const;
    get_sample_error_code code() const;
};

} // namespace dawn_player

#endif
