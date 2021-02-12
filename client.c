#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#define MAX_BUFFER 1024

int main(int argc, char** argv)
{
    char buffer[MAX_BUFFER];
    char payload[MAX_BUFFER + 1];
    uint32_t inputSize;
    struct sockaddr_in serverAddress, cli;
    int socketFD; // Socket file descriptor
    int connFD;
    int port;
    int ip;
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
    ip = atoi(argv[1]);
    port = atoi(argv[2]);

    // Create socket
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) // socket returns -1 on error
    {
        perror("Client unable to create socket");
        return -1;
    }
    else
    {
        printf("Client created socket.\n");
    }

    // Bind socket to port and IP
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = ip;
    serverAddress.sin_port = htons(port);

    // connect to a remote server on a certain IP and port
    if (connect(socketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) != 0){
        perror("client unable to connect to server");
        return -1;
    }

    // reading user input till ctrl+d
    while(scanf("%s", buffer) != EOF){
        // technically strlen should not be used in this assignment but with scanf it works
        inputSize = strlen(buffer);
        // setting the first elem to be the size
        payload[0] = inputSize;
        // copying the input into the payload
        for(int i=0; i<inputSize; i++){
            payload[i+1] = buffer[i];
        }
        // send the data over to the server TODO
        ssize_t write(socketFD, payload, inputSize);
        // clean up arrays for no buggies
        bzero(buffer, sizeof(buffer));
        bzero(payload, sizeof(payload));
    }

    // Close socket
    if (close(socketFD)) // close returns -1 on error
    {
        perror("Client unable to close socket");
    }
    else
    {
        printf("Client closed socket.\n");
    }


    return 0;
}