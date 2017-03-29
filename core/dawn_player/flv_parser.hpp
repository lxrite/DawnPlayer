/*
 *    flv_parser.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_PARSER_HPP
#define DAWN_PLAYER_FLV_PARSER_HPP

#include <cstdint>
#include <functional>
#include <memory>

#include "amf_types.hpp"
#include "samples.hpp"

namespace dawn_player {
namespace parser {

enum class parse_result {
    ok,
    abort,
    error
};

struct audio_special_config {
    std::uint16_t format_tag;
    std::uint16_t channels;
    std::uint32_t sample_per_second;
    std::uint16_t bits_per_sample;
    std::uint16_t block_align;
    std::uint16_t size;
    std::uint32_t average_bytes_per_second;
};

class flv_parser {
private:
    std::uint32_t length_size_minus_one;
public:
    flv_parser();

    parse_result parse_flv_header(const std::uint8_t* data, size_t size, size_t& bytes_consumed);
    parse_result parse_flv_tags(const std::uint8_t* data, size_t size, size_t& bytes_consumed);

    size_t first_tag_offset() const;

    void reset();
public:
    std::function<bool(std::shared_ptr<dawn_player::amf::amf_base>, std::shared_ptr<dawn_player::amf::amf_base>)> on_script_tag;
    std::function<bool(const audio_special_config&)> on_audio_specific_config;
    std::function<bool(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&)> on_avc_decoder_configuration_record;
    std::function<bool(dawn_player::sample::audio_sample&&)> on_audio_sample;
    std::function<bool(dawn_player::sample::video_sample&&)> on_video_sample;
private:
    std::uint32_t to_uint32_be(const std::uint8_t* data);
    std::uint32_t to_uint24_be(const std::uint8_t* data);
    std::uint16_t to_uint16_be(const std::uint8_t* data);
};

} // namespace parser
} // namespace dawn_player

#endif
