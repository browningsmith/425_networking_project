#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// sean test comment

#define BUFFLEN 1024

int main(int argc, char** argv)
{
    int serverSocketFD, clientSocketFD; // Socket file descriptor
    int port;
    size_t bytesRead;
    struct sockaddr_in serverAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;
    uint32_t messageLength;
    void* receiveBuffer = malloc(BUFFLEN);

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
    // printf("Port number specified: %i\n", port);

    // Create server socket
    serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFD < 0) // socket returns -1 on error
    {
        perror("Server unable to create socket");
        return -1;
    }
    // else
    // {
    //     printf("Server created socket.\n");
    // }

    // Bind server to port
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    // bind returns -1 on error
    if (bind(serverSocketFD, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("Server unable to bind socket to port");
        return -1;
    }
    // else
    // {
    //     printf("Server bound socket to port %i.\n", port);
    // }

    // listen to incoming connections
    if (listen(serverSocketFD, 5) < 0) // listen returns -1 on error
    {
        perror("Server unable to listen to port");
        return -1;
    }
    // else
    // {
    //     printf("Server set to listen on port %i.\n", port);
    // }

    // accept an incoming connection
    // printf("Server waiting for client to connect...\n");
    clientSocketFD = accept(serverSocketFD, &clientAddress, &clientAddressLength);
    if (clientSocketFD < -1) // accept returns -1 on error
    {
        perror("Server unable to receive connection from client");
        return -1;
    }
    // else
    // {
    //     printf("Server accepted connection from client!\n");
    // }

    messageLength = 1; // As long as messageLength is greater than 0, continue trying to read
    while (messageLength > 0)
    {
        // Read first 4 bytes, the length of message
        bytesRead = recv(clientSocketFD, receiveBuffer, 4, 0);
        if (bytesRead < 1) // recv returns -1 on error, or 0 on close
        {
            break;
        }
        // else
        // {
        //     printf("Server read %li bytes from client\n", bytesRead);
        // }
        messageLength = *((uint32_t*) receiveBuffer);
        // printf("Message length: %i\n", messageLength);

        // Read payload
        bytesRead = recv(clientSocketFD, receiveBuffer, messageLength, 0);
        if (bytesRead < 1) // recv returns -1 on error, or 0 on close
        {
            break;
        }
        // else
        // {
        //     printf("Server read %li bytes from client\n", bytesRead);
        // }
        
        // Write payload to stdout. Using write since buffer doesn't have a null terminating character
        write(STDOUT_FILENO, receiveBuffer, messageLength);
        printf("\n");
    }

    // Close client socket
    if (close(clientSocketFD)) // close returns -1 on error
    {
        perror("Server unable to close client socket");
    }
    else
    // {
    //     printf("Server closed client socket.\n");
    // }

    // Close server socket
    if (close(serverSocketFD)) // close returns -1 on error
    {
        perror("Server unable to close server socket");
    }
    else
    // {
    //     printf("Server closed server socket.\n");
    // }

    // Free receiveBuffer
    free(receiveBuffer);

    return 0;
}