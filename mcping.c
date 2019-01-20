#include <sys/types.h>
#ifdef _WIN32
#include <Winsock2.h>
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HANDSHAKE_SIZE 1024
#define STRING_BUF_SIZE 4096
#define PROTOCOL_VERSION 210
#define TIMEOUT_USEC 500000 // 500ms

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
typedef SSIZE_T ssize_t;
#endif

int connect_w_to(struct addrinfo *addr, suseconds_t usec) {
  int res;
  long arg;
  fd_set myset;
  struct timeval tv;
  int valopt;
  socklen_t lon;
  int soc;

  // Create socket
  soc = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if (soc < 0) {
     fprintf(stderr, "Error creating socket (%d %s)\n", errno, strerror(errno));
     return -1;
  }

  // Set non-blocking
  if ((arg = fcntl(soc, F_GETFL, NULL)) < 0) {
     fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
     return -1;
  }
  arg |= O_NONBLOCK;
  if (fcntl(soc, F_SETFL, arg) < 0) {
     fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
     return -1;
  }
  // Trying to connect with timeout
  res = connect(soc, addr->ai_addr, addr->ai_addrlen);
  if (res < 0) {
     if (errno == EINPROGRESS) {
        do {
           tv.tv_sec = 0;
           tv.tv_usec = usec;
           FD_ZERO(&myset);
           FD_SET(soc, &myset);
           res = select(soc+1, NULL, &myset, NULL, &tv);
           if (res < 0 && errno != EINTR) {
              fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno));
              return -1;
           }
           else if (res > 0) {
              // Socket selected for write
              lon = sizeof(int);
              if (getsockopt(soc, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) {
                 fprintf(stderr, "Error in getsockopt() %d - %s\n", errno, strerror(errno));
                 return -1;
              }
              // Check the value returned...
              if (valopt) {
                 return -1;
              }
              break;
           }
           else {
              return -1;
           }
        } while (1);
     }
     else {
        fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno));
        return -1;
     }
  }
  // Set to blocking mode again...
  if( (arg = fcntl(soc, F_GETFL, NULL)) < 0) {
     fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
     return -1;
  }
  arg &= (~O_NONBLOCK);
  if( fcntl(soc, F_SETFL, arg) < 0) {
     fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
     return -1;
  }

  return soc;
}

size_t build_handshake(unsigned char *buffer, char *host, unsigned short port) {
  size_t host_len = strlen(host);
  size_t len = 1 /* packet id */ + 2 /* Protocol version */;
  len += 1 /* str len */ + host_len;
  len += 2; // port
  len += 1; // state

  size_t i = 0;
  buffer[i++] = len;
  buffer[i++] = 0; /* packet id */
  buffer[i++] = PROTOCOL_VERSION;
  buffer[i++] = 1; /* encoded protocol version - varint */
  buffer[i++] = host_len;
  memcpy(buffer + i, host, host_len);
  i += host_len;
  buffer[i++] = (port >> 8) & 0xFF; /* port little-endian */
  buffer[i++] = port & 0xFF;
  buffer[i] = 1; // next state

  return len + 1; /* add length byte */
}

ssize_t read_byte(const int sfd, void *buf) {
  ssize_t nread;
  nread = recv(sfd, buf, 1, 0);
  if (nread == -1) {
    perror("Read byte");
    exit(EXIT_FAILURE);
  }
  return nread;
}

int read_varint(const int sfd) {
  int numread = 0;
  int result = 0;
  int value;
  char byte;
  do {
    if (read_byte(sfd, &byte) == 0){
      fprintf(stderr, "Failed read varint: eof\n");
      exit(EXIT_FAILURE);
    }
    value = byte & 0x7F;
    result |= value << (7 * numread);

    numread++;

    if (numread > 5) {
      fprintf(stderr, "Error reading varint: varint too big\n");
      exit(EXIT_FAILURE);
    }
  } while ((byte & 0x80) != 0);

  return result;
}

int main(int argc, char **argv) {
  unsigned short port;
  char port_str[6];
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s, json_len;
  size_t len;
  ssize_t nread;
  char byte;
  unsigned char handshake[HANDSHAKE_SIZE];
  char request[] = {0x1, 0x0};
  char string[STRING_BUF_SIZE];

  if (argc < 2) {
    printf("MC Ping 1.2.0, Minecraft Server List Ping tool.\n");
    printf("Usage: mcping <host> <port>\n");
    return EXIT_FAILURE;
  }

  if (strlen(argv[1]) > 250) {
    fprintf(stderr, "Hostname too long\n");
    return EXIT_FAILURE;
  }

  if (argc < 3) {
    port = 25565;
  } else {
    port = atoi(argv[2]);
  }

  if (port == 0) {
    fprintf(stderr, "Invalid port\n");
    return EXIT_FAILURE;
  }

#ifdef _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
      /* Tell the user that we could not find a usable */
      /* Winsock DLL.                                  */
      fprintf(stderr, "WSAStartup failed with error: %d\n", err);
      return EXIT_FAILURE;
    }
    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
      /* Tell the user that we could not find a usable */
      /* WinSock DLL.                                  */
      fprintf(stderr, "Could not find a usable version of Winsock.dll\n");
      WSACleanup();
      return EXIT_FAILURE;
    }
#endif

  /* Obtain address(es) matching host/port */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* TCP socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  sprintf(port_str, "%d", port);
  s = getaddrinfo(argv[1], port_str, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return EXIT_FAILURE;
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = connect_w_to(rp, TIMEOUT_USEC);
    if (sfd != -1) {
      break;
    }

    close(sfd);
  }

  if (rp == NULL) {  /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    return EXIT_FAILURE;
  }

  freeaddrinfo(result);

  len = build_handshake(handshake, argv[1], port);
  if (send(sfd, handshake, len, 0) != len) {
    fprintf(stderr, "Failed to send handshake\n");
    return EXIT_FAILURE;
  }

  if (send(sfd, request, 2, 0) != 2) {
    fprintf(stderr, "Failed to send request\n");
    return EXIT_FAILURE;
  }

  read_varint(sfd); /* read packet length */
  if (read_byte(sfd, &byte) == 0) { /* read packet id */
    fprintf(stderr, "Failed to read\n");
    return EXIT_FAILURE;
  }
  if (byte != 0) {
    fprintf(stderr, "Unknown packet id\n");
    return EXIT_FAILURE;
  }

  /* read json and print to stdout */
  json_len = read_varint(sfd);
  while(json_len > 0) {
    nread = recv(sfd, string, STRING_BUF_SIZE, 0);
    if (nread == -1) {
      perror("json read");
      return EXIT_FAILURE;
    }

    json_len -= nread;

    fwrite(string, 1, nread, stdout);
  }

  return EXIT_SUCCESS;
}
