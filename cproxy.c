/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 2
file:       cproxy.c
Class:      425
Due Date:   03/11/2021

TODO Update Note
Note:       This is the client part of the program where the IP and port number are
            given as command line arguments. As far as the design goes the payload is
            constructed as a string array without a termination character and the first
            4 bytes is the length of the broadcast string
*/
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_BUFFER 1024

int main(int argc, char** argv)
{
    char buffer[MAX_BUFFER + 2]; // Include two extra bytes, one for the newline or EOF character, and one for the null terminator.
                                 // These will not be transmitted to the server
    char payload[MAX_BUFFER + 4];
    uint32_t inputSize;
    struct sockaddr_in serverAddress, cli;
    int socketFD; // Socket file descriptor
    int connFD;
    int port;
    //int ip;
    int i;

    // get port number and IP from command line
    if (argc < 3)
    {
        printf(
            "ERROR: No port/IP specified!\n"
            "Usage: ./client IP portNumber\n"
        );
        return -1;
    }
    port = atoi(argv[2]);

    // Create socket
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) // socket returns -1 on error
    {
        perror("Client unable to create socket");
        return -1;
    }

    // Bind socket to port and IP
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(argv[1]);
    serverAddress.sin_port = htons(port);

    // connect to a remote server on a certain IP and port
    if (connect(socketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) != 0){
        perror("Client unable to connect to server");
        return -1;
    }

    // reading user input till ctrl+d
    while(fgets(buffer, MAX_BUFFER + 2, stdin) != NULL){
        // technically strlen should not be used in this assignment but with fgets it works
        inputSize = strlen(buffer) - 1; // Disregard newline or EOF character
        // setting the first elem to be the size
        ((uint32_t *)payload)[0] = inputSize;
        // copying the input into the payload
        for(int i=0; i<inputSize; i++){
            payload[i + 4] = buffer[i];
        }
        // send the payload to the server
        write(socketFD, payload, inputSize + sizeof(uint32_t));
        // clean up arrays for no buggies
        bzero(buffer, sizeof(buffer));
        bzero(payload, sizeof(payload));
    }

    // Close socket
    if (close(socketFD)) // close returns -1 on error
    {
        perror("Client unable to close socket");
    }
    return 0;
}
