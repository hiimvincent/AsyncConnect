#include "server.hpp"

#define PACKET_PROCESS(ID, name) void name(acc::async_connect_server *const sv, const SOCKET from, const acc::packet::packet_id id, acc::packet::detail::serializer &s)

PACKET_PROCESS(acc::packet::id_example, on_example_packet)
{
    // read packet
    acc::packet::example_packet example(s);

    // access the data
    for (std::size_t i = 0; i < example.some_string_array.size(); i++)
        printf("[ %i ] %s\n", i, example.some_string_array[i].data());

    // answer client
    example.some_string_array = {"Hello", "from", "server!"};

    sv->send_packet(from, &example);
}

int main()
{
    try
    {
        acc::async_connect_server server = {};

        // before starting the server setup all callbacks
        server.register_connect_callback([](acc::async_connect_server *const sv, SOCKET who)
                                         { printf("Client with socket ID %i has connected.\n", who); });

        server.register_disconnect_callback([](acc::async_connect_server *const sv, SOCKET who)
                                            {
			printf( "Client with socket ID %i has disconnected.\n", who );
			
			// stop server
			sv->stop( ); });

        server.register_stop_callback([](acc::async_connect_server *const sv)
                                      { printf("Server has been stopped.\n"); });

        server.register_callback([](acc::async_connect_server *const sv, SOCKET from, const acc::packet::packet_id id, acc::packet::detail::serializer &s)
                                 {
			switch ( id ) {
				case acc::packet::id_example:
					on_example_packet( sv, from, id, s );
					break;
				default:
					printf( "Unknown packet ID %i received\n", id );
			} });

        server.start("1337");

        printf("Server running on port 1337.\n");

        // wait for server to stop running
        while (server.is_running())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return 0;
    }
    catch (const acc::async_connect_server::exception &e)
    {
        printf("%s\n", e.what());
        return 1;
    }
}