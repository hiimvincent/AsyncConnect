#include "client.hpp"

using namespace acc;

async_connect_client::async_connect_client()
{
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data_) != 0)
        throw exception(exception::reason_id::wsastartup_failure, "async_connect_client::connect: WSAStartup failed");
#endif
}

async_connect_client::~async_connect_client()
{
    disconnect();

    if (processing_thread_.joinable())
        processing_thread_.join();

    if (receiving_thread_.joinable())
        receiving_thread_.join();

#ifdef _WIN32
    WSACleanup();
#endif
}

bool async_connect_client::connect(std::string_view ip, std::string_view port)
{
    if (connected_)
        throw exception(exception::reason_id::already_connected, "async_connect_client::connect: attempted to connect while a connection was open");

    if (!process_callback_)
        throw exception(exception::reason_id::no_callback, "async_connect_client::connect: no processing callback set");

    addrinfo hints = {}, *result = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip.data(), port.data(), &hints, &result) != 0)
        throw exception(exception::reason_id::getaddrinfo_failure, "async_connect_client::connect: getaddrinfo error");

    socket_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (socket_ == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::socket_failure, "async_connect_client::connect: failed to create socket");
    }

    if (::connect(socket_, result->ai_addr, int(result->ai_addrlen)) == SOCKET_ERROR)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::connection_error, "async_connect_client::connect: error connecting");
    }

    freeaddrinfo(result);

    if (!perform_handshake())
    {
        disconnect_internal(disconnect_reasons::reason_handshake_fail);
        return false;
    }

    connected_ = true;
    processing_thread_ = std::thread(&async_connect_client::process_data, this);
    receiving_thread_ = std::thread(&async_connect_client::receive_data, this);

    return true;
}

void async_connect_client::disconnect()
{
    if (connected_)
        disconnect_internal(disconnect_reasons::reason_stop);
}

bool async_connect_client::is_connected()
{
    return connected_;
}

void async_connect_client::send_packet(packet::base_packet *const packet)
{
    if (!packet)
        throw exception(exception::reason_id::packet_nullptr, "async_connect_client::send_packet: packet was nullptr");

    std::lock_guard guard(send_mtx_);

    serializer.reset();

    packet->serialize_value(serializer);

    std::vector<std::uint8_t> packet_data(sizeof(packet::header) + serializer.get_serialized_data_length());

    packet::header packet_header = construct_packet_header(
        serializer.get_serialized_data_length(),
        packet->get_id(),
        packet::flags::fl_none);

    memcpy(packet_data.data(), &packet_header, sizeof(packet::header));

    memcpy(
        packet_data.data() + sizeof(packet::header),
        serializer.get_serialized_data(),
        serializer.get_serialized_data_length());

    if (!send_packet_internal(packet_data.data(), packet_data.size()))
        disconnect_internal(disconnect_reasons::reason_error);
}

void async_connect_client::register_callback(std::function<void(async_connect_client *const, const packet::packet_id, packet::detail::serializer &)> callback_fn)
{
    if (!callback_fn)
        throw exception(exception::reason_id::null_callback, "async_connect_client::register_callback: no callback given");

    process_callback_ = callback_fn;
}

void async_connect_client::register_disconnect_callback(std::function<void(async_connect_client *const)> callback_fn)
{
    on_disconnect_callback_ = callback_fn;
}

packet::header async_connect_client::construct_packet_header(packet::packet_length length, packet::packet_id id, packet::packet_flags flags)
{
    packet::header packet_header = {};

    packet_header.flags = flags;
    packet_header.id = id;
    packet_header.length = sizeof(packet::header) + length;
    packet_header.magic = PACKET_MAGIC;

    return packet_header;
}

bool async_connect_client::perform_handshake()
{
    packet::header packet_header = construct_packet_header(0, packet::ids::id_handshake, packet::flags::fl_handshake_cl);

    auto buffer = reinterpret_cast<char *>(&packet_header);

    if (!send_packet_internal(&packet_header, sizeof(packet::header)))
        return false;

    int bytes_received = 0;

    do
    {
        int received = recv(socket_, buffer + bytes_received, sizeof(packet::header) - bytes_received, 0);

        if (received <= 0)
            return false;

        bytes_received += received;
    } while (bytes_received < sizeof(packet::header));

    if (packet_header.flags != packet::flags::fl_handshake_sv)
        return false;

    if (packet_header.id != packet::ids::id_handshake)
        return false;

    if (packet_header.length != sizeof(packet::header))
        return false;

    if (packet_header.magic != PACKET_MAGIC)
        return false;

    return true;
}

bool async_connect_client::send_packet_internal(void *const data, const packet::packet_length length)
{
    std::uint32_t bytes_sent = 0;
    do
    {
        int sent = send(
            socket_,
            reinterpret_cast<char *>(data) + bytes_sent,
            length - bytes_sent,
            0);

        if (sent <= 0)
            return false;

        bytes_sent += sent;
    } while (bytes_sent < length);

    return true;
}

void async_connect_client::disconnect_internal(const disconnect_reasons reason)
{
    std::lock_guard guard(disconnect_mtx_);

    connected_ = false;

    switch (reason)
    {
    case disconnect_reasons::reason_handshake_fail:
        if (socket_)
        {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            socket_ = 0;
        }
        break;
    case disconnect_reasons::reason_stop:
    {
        packet::header packet_header = construct_packet_header(0, packet::ids::id_disconnect, packet::flags::fl_disconnect);
        send_packet_internal(&packet_header, sizeof(packet::header));
    }
    case disconnect_reasons::reason_error:
    case disconnect_reasons::reason_server_stop:
        if (socket_)
        {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            socket_ = 0;

            if (on_disconnect_callback_)
                on_disconnect_callback_(this);
        }
    }
}

void async_connect_client::process_data()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard guard(process_mtx_);

        if (!connected_)
        {
            if (process_buffer_.size() < sizeof(packet::header))
                break;

            auto header = reinterpret_cast<packet::header *>(process_buffer_.data());

            if (process_buffer_.size() < header->length)
                break;
        }

        if (process_buffer_.size() < sizeof(packet::header))
            continue;

        auto header = reinterpret_cast<packet::header *>(process_buffer_.data());

        if (header->magic != PACKET_MAGIC)
        {
            connected_ = false;
            break;
        }

        if (process_buffer_.size() < header->length)
            continue;

        auto data_start = process_buffer_.data() + sizeof(packet::header);
        std::uint32_t data_length = header->length - sizeof(packet::header);

        serializer.assign_buffer(data_start, data_length);

        if (header->id > packet::ids::num_preset_ids)
            process_callback_(this, header->id, serializer);

        process_buffer_.erase(process_buffer_.begin(), process_buffer_.begin() + data_length + sizeof(packet::header));
    }

    process_buffer_.clear();
}

void async_connect_client::receive_data()
{
    std::vector<std::uint8_t> buffer(buffer_size_);

    while (connected_)
    {
        int bytes_received = recv(socket_, reinterpret_cast<char *>(buffer.data()), buffer_size_, NULL);

        switch (bytes_received)
        {
        case -1:
            disconnect_internal(disconnect_reasons::reason_error);
            break;
        case 0:
            disconnect_internal(disconnect_reasons::reason_server_stop);
            break;
        default:
            std::lock_guard guard(process_mtx_);

            process_buffer_.insert(process_buffer_.end(), buffer.begin(), buffer.begin() + bytes_received);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
