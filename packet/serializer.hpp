#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <vector>
#include <string>
#include <cstdint>

#define ONLY_ARITHMETIC_TYPE typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr

namespace acc::packet::detail
{
    class serializer
    {
    public:
        template <typename T, ONLY_ARITHMETIC_TYPE>
        void serialize_value(T value)
        {
            write_to_buffer(value);
        }

        template <typename T, ONLY_ARITHMETIC_TYPE>
        void serialize_value(std::vector<T> &value)
        {
            write_to_buffer<std::uint32_t>(value.size());

            serialized_buffer_.insert(
                serialized_buffer_.end(),
                reinterpret_cast<std::uint8_t *>(value.data()),
                reinterpret_cast<std::uint8_t *>(value.data()) + value.size() * sizeof(T));
        }

        void serialize_value(const std::string &value);
        void serialize_value(const std::vector<std::string> &value);

        template <typename T, ONLY_ARITHMETIC_TYPE>
        void deserialize_value(T &value)
        {
            value = read_from_buffer<T>();
        }

        template <typename T, ONLY_ARITHMETIC_TYPE>
        void deserialize_value(std::vector<T> &out_value)
        {
            auto num_items = read_from_buffer<std::uint32_t>();

            out_value.resize(num_items);
            memcpy(out_value.data(), serialized_buffer_.data() + deserialized_bytes_, num_items * sizeof(T));

            deserialized_bytes_ += num_items * sizeof(T);
        }

        void deserialize_value(std::string &out_value);
        void deserialize_value(std::vector<std::string> &out_value);

        std::uint8_t *get_serialized_data();
        std::uint32_t get_serialized_data_length();

        void reset();
        void assign_buffer(std::uint8_t *const data, std::uint32_t length);

    private:
        template <typename T, ONLY_ARITHMETIC_TYPE>
        void write_to_buffer(T value)
        {
            serialized_buffer_.insert(
                serialized_buffer_.end(),
                reinterpret_cast<std::uint8_t *>(&value),
                reinterpret_cast<std::uint8_t *>(&value) + sizeof(T));
        }

        template <typename T, ONLY_ARITHMETIC_TYPE>
        T read_from_buffer()
        {
            auto value = *reinterpret_cast<T *>(serialized_buffer_.data() + deserialized_bytes_);
            deserialized_bytes_ += sizeof(T);

            return value;
        }

        std::uint32_t deserialized_bytes_ = 0;
        std::vector<std::uint8_t> serialized_buffer_ = {};
    };
}

#endif