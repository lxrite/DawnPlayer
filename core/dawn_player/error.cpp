/*
 *    error.cpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "error.hpp"

namespace dawn_player {

open_error::open_error(const std::string& what_arg, open_error_code ec)
    : what_msg(what_arg), error_code(ec)
{}

const char* open_error::what() const
{
    return this->what_msg.c_str();
}

open_error_code open_error::code() const
{
    return this->error_code;
}

get_sample_error::get_sample_error(const std::string& what_arg, get_sample_error_code ec)
    : what_msg(what_arg), error_code(ec)
{}

const char* get_sample_error::what() const {
    return this->what_msg.c_str();
}

get_sample_error_code get_sample_error::code() const {
    return this->error_code;
}

} // namespace dawn_player
