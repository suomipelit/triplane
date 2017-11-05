#ifndef NETWORK_WIN_H
#define NETWORK_WIN_H

#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;

bool init();

int write(SOCKET s, const char* buf, int len);

int read(SOCKET s, char* buf, int len);

int close(SOCKET s);

#endif
