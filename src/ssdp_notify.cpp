#include <ssdp_notify.hpp>
#include <error_messages.hpp>

const int SSDP_PORT = 1900;
const char* SSDP_IP = "239.255.255.250";

const char* NOTIFY_TEMPLATE =
    "NOTIFY * HTTP/1.1\r\n"
    "HOST: %s:%d\r\n"
    "CACHE-CONTROL: max-age=1800\r\n"
    "LOCATION: http://%s:8080/redfish/v1\r\n"
    "NT: upnp:rootdevice\r\n"
    "NTS: ssdp:alive\r\n"
    "USN: uuid:%s::upnp:rootdevice\r\n"
    "SERVER: MyDevice/1.0 UPnP/1.1 MyServer/1.0\r\n\r\n";
    //"X-SERVICES: Service1,Service2,Service3\r\n\r\n";

int SSDPNotify::notify()
{
    int sockfd;
    struct sockaddr_in server_addr;
    std::string ipAddress = "9.3.29.122"; // Replace with your actual IP address
    std::string uuid = "12345678-1234-5678-1234-56789abcdef0"; // Replace with a unique UUID
							    
    // Create a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
	BMCWEB_LOG_ERROR << "Failed to create socket.";
        return 1;
    }


    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SSDP_PORT);
    if (inet_pton(AF_INET, SSDP_IP, &server_addr.sin_addr) <= 0)
    {
	BMCWEB_LOG_ERROR << "Failed to set up server address.";
        return 1;
    }

    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
	BMCWEB_LOG_ERROR << "Failed to enable broadcast.";
        return 1;
    }

    // Send SSDP NOTIFY messages periodically
    while (true) {
        std::string notifyMessage = generateNotifyMessage(ipAddress, uuid);
        ssize_t bytesSent = sendto(sockfd, notifyMessage.c_str(), notifyMessage.length(), 0,
                                   reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
        if (bytesSent < 0) {
            BMCWEB_LOG_ERROR << "Error sending response.";
            continue;
        }

	BMCWEB_LOG_ERROR << "Successfully sending notify packets.";

        sleep(10); // Send NOTIFY message every 10 seconds
    }

    close(sockfd);
    return 0;
}

std::string SSDPNotify::generateNotifyMessage(const std::string& ipAddress, const std::string& uuid) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), NOTIFY_TEMPLATE, SSDP_IP, SSDP_PORT, ipAddress.c_str(), uuid.c_str());
    return buffer;
}
