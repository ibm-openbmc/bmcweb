#include <ssdp_server.hpp>
#include <error_messages.hpp>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const int SSDP_PORT = 1900;
const char* SSDP_IP = "239.255.255.250";
const int BUFFER_SIZE = 1024;

int SSDPServer::start()
{
    // Create a socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        BMCWEB_LOG_ERROR << "Failed to create socket.";
        return 1;
    }
    BMCWEB_LOG_ERROR << "Socket created";
    // Enable reuse of the address and port
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        BMCWEB_LOG_ERROR << "Failed to set socket options.";
        close(sock);
        return 1;
    }
    BMCWEB_LOG_ERROR << "Socket options set";

    // Bind to the SSDP IP and port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SSDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        BMCWEB_LOG_ERROR << "Failed to bind socket.";
        close(sock);
        return 1;
    }
    BMCWEB_LOG_ERROR << "SSDP IP and Port binding successful";

    // Join the SSDP multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_IP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) == -1) {
        BMCWEB_LOG_ERROR << "Failed to join multicast group.";
        close(sock);
        return 1;
    }
    BMCWEB_LOG_ERROR << "Joined the SSDP multicast group";

    BMCWEB_LOG_ERROR  << "SSDP server running...";

    while (true) {
        char buffer[BUFFER_SIZE];

        // Receive SSDP packet
        struct sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        ssize_t bytesRead = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
        if (bytesRead == -1) {
            BMCWEB_LOG_ERROR << "Error receiving packet.";
            continue;
        }

        BMCWEB_LOG_ERROR << "SSDP packet received from " << inet_ntoa(clientAddr.sin_addr);

        // Process the SSDP packet
        std::string data(buffer, static_cast<std::string::size_type>(bytesRead));

        // TODO: Add your SSDP packet processing logic here

        // Send SSDP response
        std::string response = "HTTP/1.1 200 OK\r\n"
                               "CACHE-CONTROL: max-age=1800\r\n"
                               "ST: urn:dmtf-org:service:redfish-rest:1\r\n"
                               "USN: uuid:1234::urn:dmtf-org:service:redfish-rest:1\r\n"
                               "LOCATION: http://example.com/redfish/v1\r\n"
                               "\r\n";
        ssize_t bytesSent = sendto(sock, response.c_str(), response.size(), 0, reinterpret_cast<struct sockaddr*>(&clientAddr), sizeof(clientAddr));
        if (bytesSent == -1) {
            BMCWEB_LOG_ERROR << "Error sending response.";
            continue;
        }

        BMCWEB_LOG_ERROR << "SSDP response sent to " << inet_ntoa(clientAddr.sin_addr);
    }

    close(sock);
    return 0;
}
