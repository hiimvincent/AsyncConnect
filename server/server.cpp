#include "server.hpp"

using namespace acc;

async_connect_server::async_connect_server()
{
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data_) != 0)
        throw exception(exception::reason_id::wsastartup_failure, "async_connect_server::async_connect_server: WSAStartup failed");
#endif
}

async_connect_server::~async_connect_server()
{
    stop();

    if (accepting_thread_.joinable())
        accepting_thread_.join();

    if (processing_thread_.joinable())
        processing_thread_.join();

    if (receiving_thread_.joinable())
        receiving_thread_.join();

    if (heartbeat_thread_.joinable())
        heartbeat_thread_.join();

#ifdef _WIN32
    WSACleanup();
#endif
}

void async_connect_server::start(std::string_view port)
{
    if (running_)
        throw exception(exception::reason_id::already_running, "async_connect_server::start: attempted to start server while it was running");

    if (!process_callback_)
        throw exception(exception::reason_id::no_callback, "async_connect_server::start: no processing callback set");

    addrinfo hints = {}, *result = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(nullptr, port.data(), &hints, &result) != 0)
        throw exception(exception::reason_id::getaddrinfo_failure, "async_connect_server::start: getaddrinfo error");

    server_socket_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (server_socket_ == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::socket_failure, "async_connect_server::start: failed to create socket");
    }

    if (bind(server_socket_, result->ai_addr, result->ai_addrlen) == SOCKET_ERROR)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::bind_error, "async_connect_server::start: failed to bind socket");
    }

    if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::listen_error, "async_connect_server::start: failed to listen on socket");
    }

    freeaddrinfo(result);

    running_ = true;

    accepting_thread_ = std::thread(&async_connect_server::accept_clients, this);
    processing_thread_ = std::thread(&async_connect_server::process_data, this);
    receiving_thread_ = std::thread(&async_connect_server::receive_data, this);
    heartbeat_thread_ = std::thread(&async_connect_server::run_heartbeat, this);
}

void async_connect_server::stop()
{
    if (running_)
    {
        running_ = false;

        shutdown(server_socket_, SD_BOTH);
        closesocket(server_socket_);

        if (on_stop_callback_)
            on_stop_callback_(this);
    }

    connected_clients_.clear();
    process_buffers_.clear();
    clients_to_disconnect_.clear();
}

void async_connect_server::disconnect_client(SOCKET who)
{
    std::lock_guard guard1(client_mtx_);
    std::lock_guard guard2(process_mtx_);

    auto it = std::find_if(connected_clients_.begin(), connected_clients_.end(), [&who](const SOCKET &s)
                           { return s == who; });

    if (it == connected_clients_.end())
        return;

    shutdown(who, SD_SEND);
    closesocket(who);

    process_buffers_.erase(who);
    connected_clients_.erase(it);

    if (on_disconnect_callback_)
        on_disconnect_callback_(this, who);
}

bool async_connect_server::is_running()
{
    return running_;
}

void async_connect_server::send_packet(SOCKET to, packet::base_packet *packet)
{
    if (!packet)
        throw exception(exception::reason_id::packet_nullptr, "async_connect_server::send_packet: packet was nullptr");

    std::lock_guard guard(send_mtx_);

    serializer.reset();

    packet->serialize(serializer);

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

    if (!send_packet_internal(to, packet_data.data(), packet_data.size()))
        disconnect_client(to);
}

void async_connect_server::register_callback(std::function<void(async_connect_server *const, const SOCKET, const packet::packet_id, packet::detail::serializer &)> callback_fn)
{
    if (!callback_fn)
        throw exception(exception::reason_id::null_callback, "async_connect_server::register_callback: no callback given");

    process_callback_ = callback_fn;
}

void async_connect_server::register_stop_callback(std::function<void(async_connect_server *const)> callback_fn)
{
    on_stop_callback_ = callback_fn;
}

void async_connect_server::register_connect_callback(std::function<void(async_connect_server *const, const SOCKET)> callback_fn)
{
    on_connect_callback = callback_fn;
}

void async_connect_server::register_disconnect_callback(std::function<void(async_connect_server *const, const SOCKET)> callback_fn)
{
    on_disconnect_callback_ = callback_fn;
}

packet::header async_connect_server::construct_packet_header(packet::packet_length length, packet::packet_id id, packet::packet_flags flags)
{
    packet::header packet_header = {};

    packet_header.flags = flags;
    packet_header.id = id;
    packet_header.length = sizeof(packet::header) + length;
    packet_header.magic = PACKET_MAGIC;

    return packet_header;
}

bool async_connect_server::perform_handshake(SOCKET with)
{
    packet::header packet_header = construct_packet_header(0, packet::ids::id_handshake, packet::flags::fl_handshake_sv);

    auto buffer = reinterpret_cast<char *>(&packet_header);

    if (!send_packet_internal(with, &packet_header, sizeof(packet::header)))
        return false;

    int bytes_received = 0;
    do
    {
        int received = recv(with, buffer + bytes_received, sizeof(packet::header) - bytes_received, 0);

        if (received <= 0)
            return false;

        bytes_received += received;
    } while (bytes_received < sizeof(packet::header));

    if (packet_header.flags != packet::flags::fl_handshake_cl)
        return false;

    if (packet_header.id != packet::ids::id_handshake)
        return false;

    if (packet_header.length != sizeof(packet::header))
        return false;

    if (packet_header.magic != PACKET_MAGIC)
        return false;

    return true;
}

bool async_connect_server::send_packet_internal(SOCKET to, void *const data, const packet::packet_length length)
{
    std::uint32_t bytes_sent = 0;
    do
    {
        int sent = send(
            to,
            reinterpret_cast<char *>(data) + bytes_sent,
            length - bytes_sent,
            0);

        if (sent <= 0)
            return false;

        bytes_sent += sent;
    } while (bytes_sent < length);

    return true;
}

void async_connect_server::accept_clients()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        auto client = accept(server_socket_, nullptr, nullptr);

        if (client == INVALID_SOCKET)
            continue;

        if (!perform_handshake(client))
        {
            shutdown(client, SD_BOTH);
            closesocket(client);
            continue;
        }

        std::lock_guard guard(client_mtx_);
        connected_clients_.push_back(client);

        if (!on_connect_callback)
            continue;

        on_connect_callback(this, client);
    }
}

void async_connect_server::process_data()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard guard1(client_mtx_);
        std::lock_guard guard2(process_mtx_);

        for (std::size_t i = 0; i < connected_clients_.size(); i++)
        {
            auto client = connected_clients_[i];
            auto &process_buffer = process_buffers_[client];

            if (process_buffer.size() < sizeof(packet::header))
                continue;

            auto header = reinterpret_cast<packet::header *>(process_buffer.data());

            bool is_disconnect_packet = header->id == packet::ids::id_disconnect && header->flags & packet::flags::fl_disconnect;

            if (header->magic != PACKET_MAGIC || is_disconnect_packet)
            {
                disconnect_client(client);
                continue;
            }

            if (process_buffer.size() < header->length)
                continue;

            auto data_start = process_buffer.data() + sizeof(packet::header);
            std::uint32_t data_length = header->length - sizeof(packet::header);

            serializer.assign_buffer(data_start, data_length);

            if (header->id > packet::ids::num_preset_ids)
                process_callback_(this, client, header->id, serializer);

            if (process_buffers_.find(client) != process_buffers_.end())
                process_buffer.erase(process_buffer.begin(), process_buffer.begin() + data_length + sizeof(packet::header));
        }
    }
}

void async_connect_server::receive_data()
{
    std::vector<std::uint8_t> buffer(buffer_size_);

    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard guard(client_mtx_);
        for (std::size_t i = 0; i < connected_clients_.size(); i++)
        {
            auto &client = connected_clients_[i];

            unsigned long available_to_read = 0;
            ioctlsocket(client, FIONREAD, &available_to_read);

            if (!available_to_read)
                continue;

            int bytes_received = recv(client, reinterpret_cast<char *>(buffer.data()), buffer_size_, 0);

            switch (bytes_received)
            {
            case -1:
                disconnect_client(client);
                break;
            case 0:
                break;
            default:
                std::lock_guard guard(process_mtx_);

                auto &process_buffer = process_buffers_[client];
                process_buffer.insert(process_buffer.end(), buffer.begin(), buffer.begin() + bytes_received);
            }
        }
    }

    for (auto &client : connected_clients_)
        disconnect_client(client);
}

void async_connect_server::run_heartbeat()
{

    auto next = std::chrono::high_resolution_clock::now() + heartbeat_interval_;
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (std::chrono::high_resolution_clock::now() < next)
            continue;

        std::lock_guard client_guard(client_mtx_);
        for (auto it = connected_clients_.begin(); it != connected_clients_.end();)
        {
            auto &client = *it;

            auto header = construct_packet_header(0, packet::ids::id_heartbeat, packet::flags::fl_heartbeat);

            if (!send_packet_internal(client, &header, sizeof(header)))
                disconnect_client(client);
            else
                it++;
        }

        auto next = std::chrono::high_resolution_clock::now() + heartbeat_interval_;
    }
}