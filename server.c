#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080

int main(int argc, char** argv)
{
    int socketFD; // Socket file descriptor
    struct sockaddr_in serverAddress;

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

    // Bind socket to PORT
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT);

    // bind returns -1 on error
    if (bind(socketFD, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("Server unable to bind socket");
        return -1;
    }
    else
    {
        printf("Server bound socket.\n");
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