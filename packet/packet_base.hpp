#ifndef PACKET_BASE_H
#define PACKET_BASE_H

#include "serializer.hpp"

#define PACKET_BUFFER_SIZE 4096
#define PACKET_MAGIC 'FI00'

#pragma pack(push, 1)

namespace acc::packet {
    enum ids {
		id_none = 0,
		id_handshake,
		id_heartbeat,
		id_disconnect,
		num_preset_ids,
		id_example
	};

	enum flags {
		fl_none				= 0,
		fl_handshake_cl		= (1 << 0),
		fl_handshake_sv		= (1 << 1),
		fl_heartbeat		= (1 << 2),
		fl_disconnect		= (1 << 3)
	};

	struct header {
		std::uint32_t magic		= PACKET_MAGIC;
		std::uint16_t id		= ids::id_none;
		std::uint16_t flags		= flags::fl_none;
		std::uint32_t length	= sizeof(header);
	};

	typedef decltype(header::id) packet_id;
	typedef decltype(header::flags) packet_flags;
	typedef decltype(header::length) packet_length;

	class base_packet {
	public:
		virtual void serialize_value(detail::serializer& s) = 0;
		virtual void deserialize_value(detail::serializer& s) = 0;
		virtual packet_id get_id() = 0;
	};
}

#pragma pack(pop)

#endif