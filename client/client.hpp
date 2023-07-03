#ifndef CLIENT_H
#define CLIENT_H

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
#include <vector>
#include "../packet/packet.hpp"

namespace acc
{
    class async_connect_client
    {
    public:
        async_connect_client();
        ~async_connect_client();
        bool connect(std::string_view ip, std::string_view port);
        void disconnect();
        bool is_connected();
        void send_packet(packet::base_packet *const packet);
        void register_callback(std::function<void(async_connect_client *const, const packet::packet_id, packet::detail::serializer &)> callback_fn);
        void register_disconnect_callback(std::function<void(async_connect_client *const)> callback_fn);

    private:
        #ifdef _WIN32
            WSADATA wsa_data_ = {};
        #endif
        packet::header construct_packet_header(packet::packet_length length, packet::packet_id id, packet::packet_flags flags);
        bool perform_handshake();
        bool send_packet_internal(void *const data, const packet::packet_length length);
        enum class disconnect_reasons : std::uint8_t
        {
            reason_handshake_fail = 0,
            reason_error,
            reason_stop,
            reason_server_stop
        };

        void disconnect_internal(const disconnect_reasons reason);
        void process_data();
        void receive_data();

        bool connected_ = false;
        const std::uint32_t buffer_size_ = PACKET_BUFFER_SIZE;
        SOCKET socket_ = 0;

        std::mutex disconnect_mtx_ = {}, process_mtx_ = {}, send_mtx_ = {};
        std::vector<std::uint8_t> process_buffer_ = {};

        std::function<void(async_connect_client *const)> on_disconnect_callback_ = {};
        std::function<void(async_connect_client *const, const packet::packet_id, packet::detail::serializer &)> process_callback_ = {};

        std::thread processing_thread_ = {}, receiving_thread_ = {};
        packet::detail::serializer serializer = {};

    public:
        class exception : public std::exception
        {
        public:
            enum reason_id : std::uint8_t
            {
                none = 0,
                wsastartup_failure,
                already_connected,
                getaddrinfo_failure,
                socket_failure,
                connection_error,
                packet_nullptr,
                null_callback,
                no_callback
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
