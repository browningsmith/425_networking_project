/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 2
File:       sproxy.c
Class:      425
Due Date:   03/11/2021

TODO Update Note
Note:       This is the Server part of the program where the port number is
            given as command line argument.. As far as the design goes the payload is
            constructed as a string array without a termination character and the first
            4 bytes is the length of the broadcast string. The size is read first then
            printed to the console followed by the message in the payload
*/
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int listenSocketFD, clientSocketFD; // Socket file descriptor
    int listenPort;
    size_t bytesRead;
    struct sockaddr_in listenAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;

    // Get port number to listen on from command line
    if (argc < 2)
    {
        printf(
            "ERROR: No port specified!\n"
            "Usage: ./sproxy portNumber\n"
        );
        return -1;
    }
    listenPort = atoi(argv[1]);

    // Create listen socket
    listenSocketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocketFD < 0) // socket returns -1 on error
    {
        perror("sproxy unable to create listen socket");
        return -1;
    }

    // Bind listen socket to port
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = INADDR_ANY;
    listenAddress.sin_port = htons(listenPort);

    // bind returns -1 on error
    if (bind(listenSocketFD, (struct sockaddr*) &listenAddress, sizeof(listenAddress)) < 0)
    {
        perror("sproxy unable to bind listen socket to port");
        return -1;
    }

    // listen to incoming connections
    if (listen(listenSocketFD, 5) < 0) // listen returns -1 on error
    {
        perror("sproxy unable to listen to port");
        return -1;
    }
    
    // accepting the connected client
    clientSocketFD = accept(listenSocketFD, &clientAddress, &clientAddressLength);
    if (clientSocketFD < -1) // accept returns -1 on error
    {
        perror("sproxy to receive connection from client");
        return -1;
    }
    printf("sproxy accepted connection from client!\n");

    // Close client socket
    if (close(clientSocketFD)) // close returns -1 on error
    {
        perror("sproxy unable to close client socket");
    }

    // Close listen socket
    if (close(listenSocketFD)) // close returns -1 on error
    {
        perror("sproxy unable to close server socket");
    }

    return 0;
}
