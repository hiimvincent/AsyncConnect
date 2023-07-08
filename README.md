# AsyncConnect

```AsyncConnect``` is a lightweight asynchronous client-server implementation in C++ using TCP sockets.

## Basics

For detailed usage examples, please see ```sample_main.cpp``` in both ```/client``` and ```/server```.

*Note: ```AsyncConnect``` currently supports only Windows but I hope to add support Linux/Unix in the near future.*

## Server Functions

```c++
void async_connect_server::start(std::string_view port);
```
Start server using provided port.

```c++
void async_connect_server::stop();
```
Close server and disconnect clients.

```c++
void async_connect_server::disconnect_client(SOCKET who);
```
Disconnect client from server, which will call the disconnect callback if provided.

```c++
void async_connect_server::send_packet(SOCKET to, packet::base_packet* packet);
```
Send packet to client. Client will be disconnected if packet fails to send.

```c++
void async_connect_server::register_callback( std::function<void(async_connect_server* const, const SOCKET, const packet::packet_id, packet::detail::serializer&)> callback_fn);
```
Register callback for when packet is received. Must be registered before connection.

```c++
bool async_connect_server::is_running();
```
Get running status of server.

```c++
void async_connect_server::register_stop_callback(std::function<void(async_connect_server* const)> callback_fn);
```
Register callback for when server is destroyed or ```stop()``` is called. 
```c++
void async_connect_server::register_connect_callback(std::function<void(async_connect_server* const, const SOCKET)> callback_fn);
```
Register callback that notifies when client connects successfully.

```c++
void async_connect_server::register_disconnect_callback(std::function<void( async_connect_server* const, const SOCKET)> callback_fn);
```
Register callback for when client is disconnected from the server.

## Client Functions

```c++
bool async_connect_client::connect(std::string_view ip, std::string_view port);
```
Establish connection with server. Returns false if handshake fails and throws exception if connection cannot be established.

```c++
void async_connect_client::disconnect();
```
Close connection with server. The server will be notified before the socket is closed.

```c++
void async_connect_client::send_packet(packet::base_packet* const packet);
```
Send packet to server. Server connection will be closed if there is a failure sending the packet.

```c++
void async_connect_client::register_callback(std::function<void(async_connect_client* const, const packet::packet_id, packet::detail::serializer&)> callback_fn);
```
Register callback for when packet is received. Must be registered before connection.

```c++
void async_connect_client::register_disconnect_callback(std::function<void(async_connect_client* const)> callback_fn);
```
Register callback for when client is disconnected from the server.

```c++
bool async_connect_client::is_connected( );
```
Get status of connection.

