#ifndef SPOTIC_PROTOCOL_AP_SOCKET_H
#define SPOTIC_PROTOCOL_AP_SOCKET_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include "ap_session.h"

typedef struct ApSocket {
    SOCKET socket;
    WSAEVENT network;
    OVERLAPPED dns;
    ADDRINFOEXW dns_hints;
    struct timeval dns_timeout;
    PADDRINFOEXW addresses, next;
    Metrics *metrics;
    MetricsHandle socket_metric, network_metric, dns_metric;
    wchar_t host[AP_HOST_CHARS], service[6];
    unsigned address_attempts;
    bool initialized, dns_pending, cancelling, connecting, connected;
} ApSocket;
ApResult ap_socket_create(ApSocket *socket, Metrics *metrics);
ApTransport ap_socket_transport(ApSocket *socket);
ApResult ap_socket_destroy(ApSocket *socket);
#endif
