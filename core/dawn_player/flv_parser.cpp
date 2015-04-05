/*
 *    flv_parser.cpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "pch.h"

#include <cassert>
#include <tuple>

#include "amf_decode.hpp"
#include "flv_parser.hpp"

namespace dawn_player {
namespace parser {

flv_parser::flv_parser()
    : length_size_minus_one(0)
{
}

parse_result flv_parser::parse_flv_header(const std::uint8_t* data, size_t size, size_t& bytes_consumed)
{
    // Begin FLV header
    // Signature UI8 'F'(0x46)
    // Signature UI8 'L'(0x4c)
    // Signature UI8 'V'(0x56)
    bytes_consumed = 0;
    if (size < 9) {
        return parse_result::error;
    }
    if (data[0] != 0x46 || data[1] != 0x4c || data[2] != 0x56) {
        return parse_result::error;
    }
    // Version UI8 for FLV version 1 is 0x01
    if (data[3] != 0x01) {
        return parse_result::error;
    }
    // TypeFlagsReserved UB[5] Must be 0
    // TypeFlagsAudio    UB[1] Audio tags are present
    // TypeFlagsReserved UB[1] Must be 0
    // TypeFlagsVideo    UB[1] Video tags are present
    bool hasAudioTags = (data[4] & 0x04) != 0x00;
    bool hasVideoTags = (data[4] & 0x01) != 0x00;
    if (hasAudioTags == false && hasVideoTags == false) {
        return parse_result::error;
    }
    // DataOffset UI32 Offset in bytes from start of file to start of body (that is, size of header)
    // The DataOffset field usually has a value of 9 for FLV version 1
    auto data_offset = this->to_uint32_be(&data[5]);
    if (data_offset != 9) {
        return parse_result::error;
    }
    bytes_consumed = 9;
    return parse_result::ok;
    // End FLV header
}

parse_result flv_parser::parse_flv_tags(const std::uint8_t* data, size_t size, size_t& bytes_consumed)
{
    bytes_consumed = 0;
    auto offset = 0u;
    // FLV tags
    for (;;) {
        auto tag_offset = offset;
        // TagType           UI8                Type of this tag. Values are:
        //                                      8: audio
        //                                      9: video
        //                                      18: script data
        //                                      all others: reserved
        if (size <= offset + 1) {
            break;
        }
        std::uint8_t tag_type = data[offset++];
        if (tag_type != 8 && tag_type != 9 && tag_type != 18) {
            return parse_result::error;
        }

        // DataSize          UI24               Length of the data in the Data field
        if (size <= offset + 3) {
            break;
        }
        auto tag_data_size = this->to_uint24_be(&data[offset]);
        offset += 3;

        // Timestamp         UI24               Time in milliseconds at which the
        //                                      data in this tag applies. This value is
        //                                      relative to the first tag in the FLV
        //                                      file, which always has a timestamp
        //                                      of 0.
        if (size <= offset + 3) {
            break;
        }
        auto timestamp = this->to_uint24_be(&data[offset]);
        offset += 3;

        // TimestampExtended UI8                Extension of the Timestamp field to
        //                                      form a SI32 value. This field
        //                                      represents the upper 8 bits, while
        //                                      the previous Timestamp field
        //                                      represents the lower 24 bits of the
        //                                      time in milliseconds
        if (size <= offset + 1) {
            break;
        }
        auto timestamp_extended = data[offset++];

        // StreamID          UI24 Always 0
        if (size <= offset + 3) {
            break;
        }
        auto stream_id = to_uint24_be(&data[offset]);
        if (stream_id != 0) {
            return parse_result::error;
        }
        offset += 3;

        // Data              If TagType == 8    Body of the tag
        //                     AUDIODATA
        //                   If TagType == 9
        //                     VIDEODATA
        //                   If TagType == 18
        //                     SCRIPTDATAOBJECT
        if (size <= offset + tag_data_size + 4) {
            break;
        }
        auto previous_tag_size = this->to_uint32_be(&data[offset + tag_data_size]);
        if (previous_tag_size != tag_data_size + 11) {
            return parse_result::error;
        }
        auto tag_data_offset = offset;
        if (tag_type == 18) {
            // ScriptDataObject
            std::shared_ptr<dawn_player::amf::amf_base> script_name;
            std::shared_ptr<dawn_player::amf::amf_base> meta_data;
            const std::uint8_t* next_iterator;
            try {
                std::tie(script_name, next_iterator) = dawn_player::amf::decode_amf_and_return_iterator(&data[offset], &data[offset] + tag_data_size);
                meta_data = dawn_player::amf::decode_amf(next_iterator, &data[offset] + tag_data_size);
            }
            catch (const dawn_player::amf::decode_amf_error&) {
                return parse_result::error;
            }
            offset += tag_data_size;
            if (this->on_script_tag) {
                if (!this->on_script_tag(script_name, meta_data)) {
                    return parse_result::abort;
                }
            }
        }
        else if (tag_type == 8) {
            // AUDIODATA
            // SoundFormat    UB[4]  Format of SoundData
            // 1 = ADPCM
            // 2 = MP3
            // 3 = Linear PCM, little endian
            // 4 = Nellymoser 16-kHz mono
            // 5 = Nellymoser 8-kHz mono
            // 6 = Nellymoser
            // 7 = G.711 A-law logarithmic PCM
            // 8 = G.711 mu-law logarithmic PCM
            // 9 = reserved
            // 10 = AAC
            // 11 = Speex
            // 14 = MP3 8-Khz
            // 15 = Device-specific sound
            auto sound_format_flag = data[offset] >> 4;
            // SoundRate UB[2] Sampling rate For AAC: always 3
            // 0 = 5.5-kHz
            // 1 = 11-kHz
            // 2 = 22-kHz
            // 3 = 44-kHz
            auto sound_rate_flag = (data[offset] & 0x0f) >> 2;
            // SoundSize      UB[1]          Size of each sample.
            //                0 = snd8Bit
            //                1 = snd16Bit
            auto sound_size_flag = (data[offset] & 0x02) >> 1;
            std::uint16_t sound_size = 16;
            // SoundType      UB[1]          Mono or stereo sound
            //                0 = sndMono    For Nellymoser: always 0
            //                1 = sndStereo  For AAC: always 1
            auto sound_type_flag = data[offset++] & 0x01;
            // SoundData      UI8[size of sound data] if SoundFormat == 10
            //                                          AACAUDIODATA
            //                                        else
            //                                          Sound data¡ªvaries by format
            if (sound_format_flag == 0x0a) {
                // AAC
                // AACAUDIODATA
                // AACPacketType UI8    0: AAC sequence header
                //                      1: AAC raw
                // Data          UI8[n] if AACPacketType == 0
                //                        AudioSpecificConfig
                //                      else if AACPacketType == 1
                //                        Raw AAC frame data
                auto aac_packet_type = data[offset++];
                if (aac_packet_type == 0) {
                    // AAC sequence header
                    // AudioSpecificConfig ISO/IEC 14496-3
                    // audioObjectType UB[5]
                    auto audio_object_type_flag = data[offset] >> 3;
                    // samplingFrequencyIndex UB[4]
                    auto sampling_frequency_index = (data[offset++] & 0x07) << 1;
                    sampling_frequency_index |= data[offset] >> 7;
                    // 0x0 96000
                    // 0x1 88200
                    // 0x2 64000
                    // 0x3 48000
                    // 0x4 44100
                    // 0x5 32000
                    // 0x6 24000
                    // 0x7 22050
                    // 0x8 16000
                    // 0x9 12000
                    // 0xa 11025
                    // 0xb 8000
                    // 0xc 7350
                    // 0xd reserved
                    // 0xe reserved
                    // 0xf escape value
                    std::uint32_t sampling_frequency;
                    switch (sampling_frequency_index) {
                        case 0x0:
                            sampling_frequency = 96000;
                            break;
                        case 0x1:
                            sampling_frequency = 88200;
                            break;
                        case 0x2:
                            sampling_frequency = 64000;
                            break;
                        case 0x3:
                            sampling_frequency = 48000;
                            break;
                        case 0x4:
                            sampling_frequency = 44100;
                            break;
                        case 0x5:
                            sampling_frequency = 32000;
                            break;
                        case 0x6:
                            sampling_frequency = 24000;
                            break;
                        case 0x7:
                            sampling_frequency = 22050;
                            break;
                        case 0x8:
                            sampling_frequency = 16000;
                            break;
                        case 0x9:
                            sampling_frequency = 12000;
                            break;
                        case 0xa:
                            sampling_frequency = 8000;
                            break;
                        case 0xc:
                            sampling_frequency = 7350;
                            break;
                        case 0xd: // reserved
                        case 0xe: // reserved
                        case 0xf: // escape value
                        default:
                            return parse_result::error;
                    }

                    // channelConfiguration UB[4]
                    auto channel_configuration = (data[offset] & 0x78) >> 3;
                    if (channel_configuration == 0 || channel_configuration > 7) {
                        return parse_result::error;
                    }
                    audio_special_config asc;
                    asc.format_tag = 0x00ff; // AAC
                    asc.channels = channel_configuration == 7 ? 8 : channel_configuration;
                    asc.sample_per_second = sampling_frequency;
                    asc.bits_per_sample = sound_size;
                    asc.block_align = asc.channels * asc.bits_per_sample / 8;
                    asc.size = 0;
                    asc.average_bytes_per_second = asc.sample_per_second * asc.channels * asc.bits_per_sample / asc.block_align;
                    if (this->on_audio_specific_config) {
                        if (!this->on_audio_specific_config(asc)) {
                            return parse_result::abort;
                        }
                    }
                    offset = tag_data_offset + tag_data_size;
                }
                else if(aac_packet_type == 1) {
                    // Raw AAC frame data
                    if (tag_data_size == 0) {
                        return parse_result::error;
                    }
                    if (this->on_audio_sample) {
                        dawn_player::sample::audio_sample sample;
                        sample.timestamp = static_cast<std::int64_t>(static_cast<std::uint32_t>(timestamp | (timestamp_extended << 24))) * 10000;
                        sample.data.reserve(tag_data_offset + tag_data_size - offset);
                        std::copy(&data[offset], &data[tag_data_offset + tag_data_size], std::back_inserter(sample.data));
                        if (!this->on_audio_sample(std::move(sample))) {
                            return parse_result::abort;
                        }
                    }
                    offset = tag_data_offset + tag_data_size;
                }
                else {
                    return parse_result::error;
                }
            }
            else {
                return parse_result::error;
            }
        }
        else if (tag_type == 9) {
            // VIDEODATA video tag
            // FrameType      UB[4]  1: keyframe (for AVC, a seekable
            //                          frame)
            //                       2: inter frame (for AVC, a nonseekable
            //                          frame)
            //                       3: disposable inter frame (H.263
            //                          only)
            //                       4: generated keyframe (reserved for
            //                          server use only)
            //                       5: video info/command frame 
            auto frame_type = data[offset] >> 4;
            bool is_key_frame = frame_type == 1;
            // CodecID        UB[4]  1: JPEG (currently unused)
            //                       2: Sorenson H.263
            //                       3: Screen video
            //                       4: On2 VP6
            //                       5: On2 VP6 with alpha channel
            //                       6: Screen video version 2
            //                       7: AVC 
            auto codec_id = data[offset++] & 0x0f;
            if (codec_id != 7) {
                return parse_result::error;
            }
            
            // VideoData      If CodecID == 2           Video frame payload or UI8
            //                  H263VIDEOPACKET
            //                If CodecID == 3
            //                  SCREENVIDEOPACKET
            //                If CodecID == 4
            //                  VP6FLVVIDEOPACKET
            //                If CodecID == 5
            //                  VP6FLVALPHAVIDEOPACKET
            //                If CodecID == 6
            //                  SCREENV2VIDEOPACKET
            //                if CodecID == 7
            //                  AVCVIDEOPACKET
            if (codec_id == 7) {
                // AVCVIDEOPACKET
                // AVCPacketType    UI8 0: AVC sequence header
                //                      1: AVC NALU
                //                      2: AVC end of sequence (lower level NALU
                //                         sequence ender is not required or supported)
                auto avc_packet_type = data[offset++];
                // CompositionTime  SI24 if AVCPacketType == 1
                //                         Composition time offset
                //                       else
                //                         0
                auto composition_time = this->to_uint24_be(&data[offset]);
                offset += 3;
                // Data UI8[n] if AVCPacketType == 0
                //               AVCDecoderConfigurationRecord
                //             else if AVCPacketType == 1
                //               One or more NALUs (can be individual
                //               slices per FLV packets; that is, full frames
                //               are not strictly required)
                //             else if AVCPacketType == 2
                //               Empty

                if (avc_packet_type == 0) {
                    // AVCDecoderConfigurationRecord ISO/IEC 14496-15 5.2.4
                    // configurationVersion unsigned int(8) 
                    auto configuration_version = data[offset++];
                    if (configuration_version != 1) {
                        return parse_result::error;
                    }
                    // AVCProfileIndication unsigned int(8) contains the profile code as defined in ISO/IEC 14496-10
                    auto avc_profile_indication = data[offset++];
                    // profile_compatibility unsigned int(8) is a byte defined exactly the same as the byte which occurs
                    //                                       between the profile_IDC and level_IDC in a sequence parameter
                    //                                       set (SPS), as defined in ISO/IEC 14496-10. 
                    auto profile_compatibility = data[offset++];
                    // AVCLevelIndication unsigned int(8) contains the level code as defined in ISO/IEC 14496-10. 
                    auto avc_level_indication = data[offset++];
                    // reserved bit(6) 0b111111
                    // lengthSizeMinusOne unsigned int(2) indicates the length in bytes of the NALUnitLength field in an
                    //                                    AVC video sample or AVC parameter set sample of the associated
                    //                                    stream minus one. For example, a size of one byte is indicated
                    //                                    with a value of 0. The value of this field shall be one of 0,
                    //                                    1, or 3 corresponding to a length encoded with 1, 2, or 4 bytes,
                    //                                    respectively. 
                    auto length_size_minus_one_flag = data[offset++] & 0x03;
                    switch (length_size_minus_one_flag) {
                        case 0:
                            this->length_size_minus_one = 1;
                            break;
                        case 1:
                            this->length_size_minus_one = 2;
                            break;
                        case 3:
                            this->length_size_minus_one = 4;
                            break;
                        default:
                            return parse_result::error;
                    }
                    // reserved bit(3) 0b111
                    // numOfSequenceParameterSets unsigned int(5) indicates the number of SPSs that are used as the initial
                    //                                            set of SPSs for decoding the AVC elementary stream.
                    auto num_of_sequence_parameter_sets = data[offset++] & 0x1f;
                    std::vector<std::uint8_t> sequance_parameter_set_nal_units;
                    for (int i = 0; i < num_of_sequence_parameter_sets; ++i) {
                        // sequenceParameterSetLength unsigned int(16) indicates the length in bytes of the SPS NAL unit as
                        //                                             defined in ISO/IEC 14496-10. 
                        auto sps_length = this->to_uint16_be(&data[offset]);
                        offset += 2;
                        // sequenceParameterSetNALUnit bit(8*sequenceParameterSetLength)
                        std::copy(&data[offset], &data[offset + sps_length], std::back_inserter(sequance_parameter_set_nal_units));
                        offset += sps_length;
                    }
                    // unsigned int(8) numOfPictureParameterSets;
                    auto num_of_picture_parameter_sets = data[offset++];
                    std::vector<std::uint8_t> picture_parameter_set_nal_units;
                    for (int i = 0; i < num_of_picture_parameter_sets; ++i) {
                        // pictureParameterSetLength unsigned int(16) indicates the length in bytes of the PPS NAL unit as
                        //                                            defined in ISO/IEC 14496-10. 
                        auto pps_length = this->to_uint16_be(&data[offset]);
                        offset += 2;
                        // pictureParameterSetNALUnit bit(8*pictureParameterSetLength) contains a PPS NAL unit, as specified
                        //                                                             in ISO/IEC 14496-10. PPSs shall occur 
                        //                                                             in order of ascending parameter set 
                        //                                                             identifier with gaps being allowed.
                        std::copy(&data[offset], &data[offset + pps_length], std::back_inserter(picture_parameter_set_nal_units));
                        offset += pps_length;
                    }
                    if (this->on_avc_decoder_configuration_record) {
                        if (!this->on_avc_decoder_configuration_record(sequance_parameter_set_nal_units, picture_parameter_set_nal_units)) {
                            return parse_result::abort;
                        }
                    }
                }
                else if (avc_packet_type == 1) {
                    // One or more NALUs
                    if (this->on_video_sample) {
                        dawn_player::sample::video_sample sample;
                        if (this->length_size_minus_one == 0) {
                            return parse_result::error;
                        }
                        std::uint32_t nalu_length = 0;
                        sample.dts = static_cast<std::int64_t>(static_cast<std::uint32_t>(timestamp | (timestamp_extended << 24))) * 10000;
                        sample.timestamp = sample.dts + composition_time * 10000;
                        sample.is_key_frame = is_key_frame;
                        while (tag_data_size > offset - tag_data_offset) {
                            if (tag_data_size - (offset - tag_data_offset) < this->length_size_minus_one) {
                                return parse_result::error;
                            }
                            if (this->length_size_minus_one == 1) {
                                nalu_length = data[offset++];
                            }
                            else if (this->length_size_minus_one == 2) {
                                nalu_length = this->to_uint16_be(&data[offset]);
                                offset += 2;
                            }
                            else {
                                assert(this->length_size_minus_one == 4);
                                nalu_length = this->to_uint32_be(&data[offset]);
                                offset += 4;
                            }
                            if (nalu_length > tag_data_offset + tag_data_size - offset || nalu_length == 0) {
                                return parse_result::error;
                            }
                            sample.data.push_back(0x00);
                            sample.data.push_back(0x00);
                            sample.data.push_back(0x01);
                            std::copy(&data[offset], &data[offset + nalu_length], std::back_inserter(sample.data));
                            offset += nalu_length;
                        }
                        if (!this->on_video_sample(std::move(sample))) {
                            return parse_result::abort;
                        }
                    }
                }
                else if (avc_packet_type == 2) {
                    // do nothing
                }
                else {
                    return parse_result::error;
                }
                offset = tag_data_offset + tag_data_size;
            }
            else {
                assert(false);
            }
        }
        offset += 4;
        bytes_consumed = offset;
    }
    return parse_result::ok;
}

size_t flv_parser::first_tag_offset() const
{
    return 13;
}

void flv_parser::reset()
{
    this->length_size_minus_one = 0;

    this->on_script_tag = nullptr;
    this->on_audio_specific_config = nullptr;
    this->on_avc_decoder_configuration_record = nullptr;
    this->on_audio_sample = nullptr;
    this->on_video_sample = nullptr;
}

std::uint32_t flv_parser::to_uint32_be(const std::uint8_t* data)
{
    union {
        std::uint8_t from[4];
        std::uint32_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[3] = data[0];
    cvt.from[2] = data[1];
    cvt.from[1] = data[2];
    cvt.from[0] = data[3];
    return cvt.to;
}

std::uint32_t flv_parser::to_uint24_be(const std::uint8_t* data)
{
    union {
        std::uint8_t from[4];
        std::uint32_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[3] = 0;
    cvt.from[2] = data[0];
    cvt.from[1] = data[1];
    cvt.from[0] = data[2];
    return cvt.to;
}

std::uint16_t flv_parser::to_uint16_be(const std::uint8_t* data)
{
    union {
        std::uint8_t from[2];
        std::uint16_t to;
    } cvt;
    static_assert(sizeof(cvt.from) == sizeof(cvt.to), "error");
    cvt.from[1] = data[0];
    cvt.from[0] = data[1];
    return cvt.to;
}

} // namespace parser
} // namespace dawn_player
