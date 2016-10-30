#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define HANDSHAKE_SIZE 1024
#define STRING_BUF_SIZE 4096
#define PROTOCOL_VERSION 210

size_t build_handshake(char *buffer, char *host, unsigned short port) {
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
  nread = read(sfd, buf, 1);
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
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s, json_len;
  size_t len;
  ssize_t nread;
  char byte;
  char handshake[HANDSHAKE_SIZE];
  char request[] = {0x1, 0x0};
  char string[STRING_BUF_SIZE];

  if (argc != 3) {
    printf("MC Ping 1.0, Minecraft Server List Ping tool.\n");
    printf("Usage: mcping <host> <port>\n");
    return EXIT_FAILURE;
  }

  if (strlen(argv[1]) > 250) {
    fprintf(stderr, "Hostname too long\n");
    return EXIT_FAILURE;
  }

  port = atoi(argv[2]);
  if (port == 0) {
    fprintf(stderr, "Invalid port\n");
    return EXIT_FAILURE;
  }

  /* Obtain address(es) matching host/port */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* TCP socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  s = getaddrinfo(argv[1], argv[2], &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return EXIT_FAILURE;
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break; /* Success */

    close(sfd);
  }

  if (rp == NULL) {  /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    return EXIT_FAILURE;
  }

  freeaddrinfo(result);

  len = build_handshake(handshake, argv[1], port);
  if (write(sfd, handshake, len) != len) {
    fprintf(stderr, "Failed to send handshake\n");
    return EXIT_FAILURE;
  }

  if (write(sfd, request, 2) != 2) {
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
    nread = read(sfd, &string, STRING_BUF_SIZE);
    if (nread == -1) {
      perror("json read");
      return EXIT_FAILURE;
    }

    json_len -= nread;

    printf("%s", string);
  }

  return EXIT_SUCCESS;
}

