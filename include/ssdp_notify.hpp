#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class SSDPNotify
{
public :
    int notify();
    std::string generateNotifyMessage(const std::string& ipAddress, const std::string& uuid);
};
