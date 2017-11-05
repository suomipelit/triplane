#include "network_win.h"

bool init()
{
    int res;
    WSADATA wsaData;
    res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return !res;
}

int write(SOCKET s, const char* buf, int len)
{
    return send(s, buf, len, 0);
}

int read(SOCKET s, char* buf, int len)
{
    return recv(s, buf, len, 0);
}

int close(SOCKET s)
{
    return closesocket(s);
}
