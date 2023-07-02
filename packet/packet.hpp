#ifndef PACKET_H
#define PACKET_H

#include "packet_base.hpp"

namespace acc::packet {
	class example_packet : public base_packet {
	public:
		example_packet() { }

		example_packet(detail::serializer& s) {
			deserialize_value(s);
		}

		virtual void serialize_value(detail::serializer& s) {
			s.serialize_value(some_short);
			s.serialize_value(some_array);
			s.serialize_value(some_string_array);
		}

		virtual void deserialize_value(detail::serializer& s) {
			s.deserialize_value(some_short);
			s.deserialize_value(some_array);
			s.deserialize_value(some_string_array);
		}
	
		virtual packet_id get_id() {
			return ids::id_example;
		}

		std::uint16_t some_short = 0;
		std::vector<std::uint8_t> some_array = { };
		std::vector<std::string> some_string_array = { };
	};
}

#endif