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
    int socketFD; // Socket file descriptor
    int port;
    int ip;
    int i;


    // get port number and IP from command line
    if (argc < 2)
    {
        printf(
            "ERROR: No port specified!\n"
            "Usage: ./server portNumber\n"
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

    // connect to a remote server on a certain IP and port
    // UNTESTED PLZ REVIEW
    connect(socketFD, &ip, sizeof(ip));

    // reading user input till ctrl+d
    while(scanf("%s", buffer) != EOF){
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