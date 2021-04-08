/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 2
File:       sproxy.c
Class:      425
Due Date:   03/15/2021

Note:       This is the server part of the program. The program takes 1
            command line argument: lport (the port to listen for an
            incoming connection). When cproxy receives a TCP connection on
            it's listening socket, it establishes another TCP connection
            with the server socket, at localhost and port 23, the VMs telnet daemon.
            The program uses select() to wait for
            data on either the client side or server side, or both, and
            sends data from the client to the server, or the server to
            the client, as it comes in.
*/
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 1024
#define LOCALHOST "127.0.0.1"
#define TELNET_PORT 23
#define PACKET_SIZE 2*sizeof(uint32_t)+BUFFER_LEN

struct packet {
    // header
    uint32_t type;      // 0 = heartbeat, !0 = data
    uint32_t length;    // length of payload
    // payload
    void* payload;      // either int sessionID or buffer
};

/*************************************
 * max
 * 
 * Arguments: int a,b
 * Retunrs: int
 * 
 * Returns the maximum of a and b
 *************************************/
int max(int a, int b);

/*************************************
 * relay
 * 
 * Arguments: int receiveFD, sendFD
 *            char* buffer
 * Returns: int
 * 
 * Reads a message up to bufferSize bytes from
 * receiveFD and copies it in to buffer, then
 * writes the copied message over to sendFD
 * 
 * Returns -1 on error, 0 otherwise
 *************************************/
int relay(int receiveFD, int sendFD, void* buffer, int bufferSize);

/*************************************
 * isValidAddress
 * 
 * Arguments: char oldAddress[], char newAddress[]
 * Returns: int
 * 
 * compares a new address with the old to see if the
 * new address is a valid one
 * 
 * Returns 0 on true, -1 on error, >0 on false
 *************************************/
int isValidAddress(char oldAddress[], char newAddress[]);

int main(int argc, char** argv)
{
    int listenSocketFD, clientSocketFD, serverSocketFD; // Socket file descriptor
    fd_set socketSet;
    in_port_t listenPort;
    struct sockaddr_in listenAddress, serverAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;
    void* buffer = NULL;

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

    // set to listen to incoming connections
    if (listen(listenSocketFD, 5) < 0) // listen returns -1 on error
    {
        perror("sproxy unable to listen to port");
        return -1;
    }

    // Allocate space for buffer
    buffer = malloc(BUFFER_LEN);
    if (buffer == NULL) // malloc returns NULL on error
    {
        perror("Unable to allocate space for buffer");
        return -1;
    }

    // populate info for telnet daemon into serverAddress
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(LOCALHOST);
    serverAddress.sin_port = htons(TELNET_PORT);

    // Infinite loop, continue to listen for new connections
    while (1)
    {
        // accept a new client
        printf("sproxy waiting for new connection...\n");
        clientSocketFD = accept(listenSocketFD, &clientAddress, &clientAddressLength);
        if (clientSocketFD < -1) // accept returns -1 on error
        {
            perror("sproxy unable to receive connection from client");
        }
        else
        {
            printf("sproxy accepted new connection from client!\n");
        }

        // Create server socket
        serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocketFD < 0) // socket returns -1 on error
        {
            perror("sproxy unable to create server socket");

            // Close client socket, so it doesn't stay open
            if (close(clientSocketFD)) // close returns -1 on error
            {
                perror("sproxy unable to properly close client socket");
            }
            else
            {
                printf("sproxy closed connection to client\n");
            }

            continue; // move to accept new connection
        }

        // Connect to server
        printf("sproxy attempting to connect to server...\n");
        if (connect(serverSocketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) != 0)
        {
            perror("sproxy unable to connect to server");

            // Close client socket, so it doesn't stay open
            if (close(clientSocketFD)) // close returns -1 on error
            {
                perror("sproxy unable to properly close client socket");
            }
            else
            {
                printf("sproxy closed connection to client\n");
            }

            continue; // move to accept new connection
        }
        printf("sproxy successfully connected to server!\n");

        // Use select for data to be ready on both serverSocket and clientSocket
        while (1)
        {   
            // Reset socketSet
            FD_ZERO(&socketSet); // zero out socketSet
            FD_SET(serverSocketFD, &socketSet); // add server socket
            FD_SET(clientSocketFD, &socketSet); // add client socket

            // Wait indefinitely for input to be available using select
            if(
                select(
                    max(serverSocketFD, clientSocketFD) + 1,
                    &socketSet,
                    NULL,
                    NULL,
                    NULL
                ) < 0 // select returns -1 on error
            )
            {
                perror("sproxy unable to use select to wait for input");

                // Close server socket, so it doesn't stay open
                if (close(serverSocketFD)) // close returns -1 on error
                {
                    perror("sproxy unable to properly close server socket");
                }
                else
                {
                    printf("sproxy closed connection to server\n");
                }

                // Close client socket, so it doesn't stay open
                if (close(clientSocketFD)) // close returns -1 on error
                {
                    perror("sproxy unable to properly close client socket");
                }
                else
                {
                    printf("sproxy closed connection to client\n");
                }

                return -1;
            }

            // If input is ready on serverSocket, relay to clientSocket
            if (FD_ISSET(serverSocketFD, &socketSet))
            {   
                if (relay(serverSocketFD, clientSocketFD, buffer, BUFFER_LEN) < 0) // relay returns -1 on error
                {
                    printf("Unable to send data from server to client\nConnection closed by either server or client\n");
                    break; // Break out of loop to move on to close server and client
                }
            }

            // If input is ready on clientSocket, relay to serverSocket
            if (FD_ISSET(clientSocketFD, &socketSet))
            {   
                if (relay(clientSocketFD, serverSocketFD, buffer, BUFFER_LEN) < 0) // relay returns -1 on error
                {
                    printf("Unable to send data from client to server\nConnection closed by either server or client\n");
                    break; // Break out of loop to move on to close server and client
                }
            }
        }

        // Close server socket
        if (close(serverSocketFD)) // close returns -1 on error
        {
            perror("sproxy unable to properly close server socket");
        }
        else
        {
            printf("sproxy closed connection to server\n");
        }

        // Close client socket
        if (close(clientSocketFD)) // close returns -1 on error
        {
            perror("sproxy unable to properly close client socket");
        }
        else
        {
            printf("sproxy closed connection to client\n");
        }
    }

    // Close listen socket
    if (close(listenSocketFD)) // close returns -1 on error
    {
        perror("sproxy unable to close listen socket");
    }

    // free buffer
    free(buffer);

    return 0;
}

int relay(int receiveFD, int sendFD, void* buffer, int bufferSize)
{
    // Read from receiveFD into buffer
    ssize_t bytesRead = recv(receiveFD, buffer, bufferSize, 0);
    if (bytesRead < 1) // Returns 0 on closed connection, -1 on error
    {
        printf("Unable to receive bytes, either connection closed or error\n");
        return -1;
    }

    // Write from buffer to sendFD
    ssize_t bytesSent = send(sendFD, buffer, bytesRead, 0);
    if (bytesSent < 1) // Returns -1 on error
    {
        printf("Unable to send bytes\n");
        return -1;
    }

    return 0;
}

int max(int a, int b)
{
    if (a > b)
    {
        return a;
    }

    return b;
}

int isValidAddress(char *oldClientAddress, char *newClientAddress, char *serverVMAddress)
{
    char *lastNum;
    int i = 0;
    int periods = 0;
    
    // if any of the pointers are null
    if(oldClientAddress == NULL || newClientAddress == NULL || serverVMAddress == NULL) return 1;

    // check that the first 3 numbers remain the same
    while(periods<3){
        if(oldClientAddress[i] != newClientAddress[i]) return 2;
        if(newClientAddress[i] == '.') periods++;
        i++;
    }

    // grabbing the last number based if there is a slash or not
    lastNum = (strchr(newClientAddress, '/') == NULL)? strrchr(newClientAddress, '.') : strrchr(newClientAddress, '/');
    lastNum++;
    
    // checking that the last num is unique
    if(strstr(oldClientAddress, lastNum) != NULL) return 3;
    if(strstr(serverVMAddress, lastNum) != NULL) return 4;
    
    // checking that the number is in range
    if(atoi(lastNum)>254 || atoi(lastNum)<1) return 5;

    // else new address is valid
    return 0;
}
