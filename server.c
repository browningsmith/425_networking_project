#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFLEN 1024

int main(int argc, char** argv)
{
    int socketFD; // Socket file descriptor
    int port;
    struct sockaddr_in serverAddress;
    char receiveBuffer[BUFFLEN];

    // Get port number from command line
    if (argc < 2)
    {
        printf(
            "ERROR: No port specified!\n"
            "Usage: ./server portNumber\n"
        );
        return -1;
    }
    port = atoi(argv[1]);
    printf("Port number specified: %i\n", port);

    // Create socket
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) // socket returns -1 on error
    {
        perror("Server unable to create socket");
        return -1;
    }
    else
    {
        printf("Server created socket.\n");
    }

    // Bind socket to port
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    // bind returns -1 on error
    if (bind(socketFD, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("Server unable to bind socket to port");
        return -1;
    }
    else
    {
        printf("Server bound socket to port %i.\n", port);
    }

    // listen to incoming connections
    if (listen(socketFD, 5) < 0) // listen returns -1 on error
    {
        perror("Server unable to listen to port");
        return -1;
    }
    else
    {
        printf("Server set to listen on port %i.\n", port);
    }


    // Close socket
    if (close(socketFD)) // close returns -1 on error
    {
        perror("Server unable to close socket");
    }
    else
    {
        printf("Server closed socket.\n");
    }


    return 0;
}