#include "client.hpp"
#include <iostream>

#define PACKET_PROCESS(ID, name) void name(acc::async_connect_client *const cl, const acc::packet::packet_id id, acc::packet::detail::serializer &s)

PACKET_PROCESS(acc::packet::id_example, on_example_packet)
{
    // read packet
    acc::packet::example_packet example(s);

    // access the data
    for (std::size_t i = 0; i < example.some_string_array.size(); i++)
        printf("[ %i ] %s\n", i, example.some_string_array[i].data());

    // disconnect from server
    cl->disconnect();
}

int main()
{
    try
    {
        acc::async_connect_client client = {};

        client.register_disconnect_callback([](acc::async_connect_client *const cl)
                                            { printf("Disconnected from server.\n"); });

        client.register_callback([](acc::async_connect_client *const cl, const acc::packet::packet_id id, acc::packet::detail::serializer &s)
                                 {
			switch ( id ) {
				case acc::packet::id_example:
					on_example_packet( cl, id, s );
					break;
				default:
					printf( "Unknown packet ID %i received\n", id );
			} });

        if (client.connect("localhost", "1337"))
        {
            printf("Connected to server!\n");

            // create packet
            acc::packet::example_packet example = {};

            example.some_short = 128;
            example.some_array = {1, 2, 3, 4, 5};
            example.some_string_array = {"Hello", "from", "client!"};

            // send packet
            client.send_packet(&example);

            // wait for server response
            while (client.is_connected())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        else
            printf("Handshake has failed.\n");

        std::cin.get();
        return 0;
    }
    catch (const acc::async_connect_client::exception &e)
    {
        printf("%s\n", e.what());

        std::cin.get();
        return 1;
    }
}
