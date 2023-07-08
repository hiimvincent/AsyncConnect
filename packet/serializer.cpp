#include "serializer.hpp"

using namespace acc::packet::detail;

void serializer::serialize_value(const std::string &value)
{
    write_to_buffer<std::uint32_t>(value.length());
    serialized_buffer_.insert(serialized_buffer_.end(), value.begin(), value.end());
}

void serializer::serialize_value(const std::vector<std::string> &value)
{
    write_to_buffer<std::uint32_t>(value.size());

    for (auto &s : value)
        serialize_value(s);
}

void serializer::deserialize_value(std::string &out_value)
{
    auto length = read_from_buffer<std::uint32_t>();

    out_value.resize(length + 1);
    memcpy(out_value.data(), serialized_buffer_.data() + deserialized_bytes_, length);

    deserialized_bytes_ += length;
}

void serializer::deserialize_value(std::vector<std::string> &out_value)
{
    auto num_strings = read_from_buffer<std::uint32_t>();

    out_value.resize(num_strings);
    for (std::uint32_t i = 0; i < num_strings; i++)
    {
        std::string deserialized = "";
        deserialize_value(deserialized);
        out_value[i] = std::move(deserialized);
    }
}

std::uint8_t *serializer::get_serialized_data()
{
    return serialized_buffer_.data();
}

std::uint32_t serializer::get_serialized_data_length()
{
    return serialized_buffer_.size();
}

void serializer::reset()
{
    deserialized_bytes_ = 0;
    serialized_buffer_.clear();
}

void serializer::assign_buffer(std::uint8_t *const data, std::uint32_t length)
{
    reset();
    serialized_buffer_.insert(serialized_buffer_.begin(), data, data + length);
}