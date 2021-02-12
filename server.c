#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

int main(int argc, char** argv)
{
    int socketFD; // Socket file descriptor

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