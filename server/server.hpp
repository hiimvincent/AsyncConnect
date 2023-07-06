#ifndef SERVER_H
#define SERVER_H

#pragma region windows_includes

#ifdef _WIN32
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #error Currently only Windows is supported.
#endif

#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include "../packet/packet.hpp"

namespace acc
{
    class async_connect_server
    {
    public:
        async_connect_server();
        ~async_connect_server();
        void start(std::string_view port);
        void stop();
        void disconnect_client(SOCKET who);
        bool is_running();
        void send_packet(SOCKET to, packet::base_packet *packet);
        void register_callback(std::function<void(async_connect_server *const, const SOCKET, const packet::packet_id, packet::detail::serializer &)> callback_fn);
        void register_stop_callback(std::function<void(async_connect_server *const)> callback_fn);
        void register_connect_callback(std::function<void(async_connect_server *const, const SOCKET)> callback_fn);
        void register_disconnect_callback(std::function<void(async_connect_server *const, const SOCKET)> callback_fn);

    private:
#ifdef _WIN32
        WSADATA wsa_data_ = {};
#endif

        packet::header construct_packet_header(packet::packet_length length, packet::packet_id id, packet::packet_flags flags);

        bool perform_handshake(SOCKET with);
        bool send_packet_internal(SOCKET to, void *const data, const packet::packet_length length);
        void accept_clients();
        void process_data();
        void receive_data();
        void run_heartbeat();

        bool running_ = false;

        const std::uint32_t buffer_size_ = PACKET_BUFFER_SIZE;
        const std::chrono::duration<long long> heartbeat_interval_ = std::chrono::seconds(5);

        SOCKET server_socket_ = 0;

        std::mutex send_mtx_ = {}, disconnect_mtx_ = {};

        std::recursive_mutex client_mtx_ = {}, process_mtx_ = {};

        std::vector<SOCKET> clients_to_disconnect_ = {};

        std::vector<SOCKET> connected_clients_ = {};
        std::unordered_map<SOCKET, std::vector<std::uint8_t>> process_buffers_ = {};

        std::function<void(async_connect_server *const, const SOCKET)> on_connect_callback = {}, on_disconnect_callback_ = {};
        std::function<void(async_connect_server *const)> on_stop_callback_ = {};

        std::function<void(async_connect_server *const, const SOCKET, const packet::packet_id, packet::detail::serializer &)> process_callback_ = {};

        std::thread accepting_thread_ = {}, processing_thread_ = {}, receiving_thread_ = {}, heartbeat_thread_{};

        packet::detail::serializer serializer = {};

    public:
        class exception : public std::exception
        {
        public:
            enum reason_id : std::uint8_t
            {
                none = 0,
                wsastartup_failure,
                already_running,
                getaddrinfo_failure,
                socket_failure,
                packet_nullptr,
                null_callback,
                no_callback,
                bind_error,
                listen_error
            };

            exception(reason_id reason, std::string_view what) : reason_(reason), what_(what){};

            virtual const char *what() const noexcept
            {
                return what_.data();
            }

            const reason_id get_reason()
            {
                return reason_;
            }

        private:
            std::string what_ = {};
            reason_id reason_ = reason_id::none;
        };
    };
}

#endif