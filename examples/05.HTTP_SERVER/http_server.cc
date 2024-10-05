#include "timeout.h"
#include <NetAPI.h>
#include <debug.hh>
#include <errno.h>
#include <fail-simulator-on-error.h>
#include <memory>
#include <string_view>
#include <thread.h>
#include <tick_macros.h>

using Debug            = ConditionalDebug<true, "HTTP server example test">;
constexpr bool UseIPv6 = CHERIOT_RTOS_OPTION_IPv6;

#define LISTEN_PORT 80
DECLARE_AND_DEFINE_BIND_CAPABILITY(HTTPPort, UseIPv6, LISTEN_PORT);

DECLARE_AND_DEFINE_ALLOCATOR_CAPABILITY(TestMalloc, 32 * 1024);
#define TEST_MALLOC STATIC_SEALED_VALUE(TestMalloc)

static char reply[] =
  "HTTP/1.1 200 OK\r\n"
  "Content-type: text/html\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">"
  "<html>"
  "<head><title>Hello from CHERIoT!</title></head>"
  "<body><h1>It works!</h1><p>Served from a CHERIoT device.</p></body>"
  "</html>\n";

/**
 * Maximum number of clients the server will server before shutting down. This
 * is useful to check that the server can handle multiple clients before
 * terminating.
 */
static const uint16_t MaxClients = 10;

void __cheri_compartment("http_server_example") example()
{
	network_start();

	auto heapAtStart =
	  heap_quota_remaining(TEST_MALLOC);

	Debug::log("Creating a listening socket.");
	Timeout unlimited{UnlimitedTimeout};
	auto    socket = network_socket_listen_tcp(
	     &unlimited, TEST_MALLOC, STATIC_SEALED_VALUE(HTTPPort));

	if (socket == nullptr)
	{
		Debug::log("Failed to create a listening socket.");
		return;
	}

	Debug::log("Listening on port {}...", LISTEN_PORT);
	uint16_t clientsCounter = 0;
	while (clientsCounter < MaxClients)
	{
		NetworkAddress clientAddress = {0};
		uint16_t       clientPort    = 0;

		auto clientSocket = network_socket_accept_tcp(
		  &unlimited, TEST_MALLOC, socket, &clientAddress, &clientPort);

		if (clientSocket == nullptr)
		{
			Debug::log("Failed to establish a connection.");
			continue;
		}

		if (!UseIPv6)
		{
			Debug::log("Established a connection with {}.{}.{}.{}:{}",
			           static_cast<int>(clientAddress.ipv4) & 0xff,
			           static_cast<int>(clientAddress.ipv4 >> 8) & 0xff,
			           static_cast<int>(clientAddress.ipv4 >> 16) & 0xff,
			           static_cast<int>(clientAddress.ipv4 >> 24) & 0xff,
			           clientPort);
		}
		else
		{
			Debug::log("Established a connection.");
		}

		clientsCounter++;

		auto [received, buffer] =
		  network_socket_receive(&unlimited, TEST_MALLOC, clientSocket);

		if (received > 0)
		{
			Debug::log(
			  "Received {} bytes from the client, serving static content.",
			  received);
			constexpr size_t toSend = sizeof(reply) - 1;
			size_t           sent   = 0;
			while (sent < toSend)
			{
				size_t remaining = toSend - sent;

				size_t sentThisCall = network_socket_send(
				  &unlimited, clientSocket, &(reply[sent]), remaining);
				Debug::log("Sent {} bytes", sentThisCall);

				if (sentThisCall >= 0)
				{
					sent += sentThisCall;
				}
				else
				{
					Debug::log("Send failed: {}", sentThisCall);
					break;
				}
			}
		}
		else
		{
			Debug::log("Failed to receive request from the client, error {}.",
			           received);
		}

		Debug::log("Terminating the connection with the client.");
		if (network_socket_close(&unlimited, TEST_MALLOC, clientSocket) != 0)
		{
			Debug::log("Failed to close the client socket.");
			break;
		}
	}

	Debug::log("Closing the listening socket.");
	if (network_socket_close(&unlimited, TEST_MALLOC, socket) != 0)
	{
		Debug::log("Failed to close the listening socket.");
	}

	Debug::log("Now checking for leaks.");
	auto heapAtEnd = heap_quota_remaining(TEST_MALLOC);
	if (heapAtEnd < heapAtStart)
	{
		Debug::log("Warning: The implementation leaked {} bytes (start: {} vs. end: {}).",
		           heapAtStart - heapAtEnd,
		           heapAtEnd,
		           heapAtStart);
	}
	else
	{
		Debug::log("No leaks detected.");
	}

	Debug::log("Terminating the server.");
}
