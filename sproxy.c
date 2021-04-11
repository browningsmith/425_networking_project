/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 3
File:       sproxy.c
Class:      425
Due Date:   04/12/2021

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
#define _DEFAULT_SOURCE // Needed to use timersub on Windows Subsystem for Linux

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 1024
#define LOCALHOST "127.0.0.1"
#define TELNET_PORT 23

typedef enum {

    PACKET_TYPE,
    LENGTH,
    PAYLOAD,

} segmentType;

struct packet {
    // header
    uint32_t type;      // 0 = heartbeat, !0 = data
    uint32_t length;    // length of payload
    // payload
    void* payload;      // either int sessionID or buffer
};

// globals
struct timeval timeLastMessageSent;
struct timeval newTime;
struct timeval timeDif;
int sessionID = 0;
int serverConnectionAttempts = 0;
int clientConnectionAttempts = 0;

/*************************************
 * max
 * 
 * Arguments: int a,b
 * Returns: int
 * 
 * Returns the maximum of a and b
 *************************************/
int max(int a, int b);

/******************************************
 * compressPacket
 * 
 * Arguments: char* buffer, struct packet
 * Returns: int
 * 
 * Takes the given packet and compresses all
 * the data into a single buffer, which can
 * then be sent later using send()
 * 
 * Returns the number of bytes now stored
 * in buffer
 *****************************************/
int compressPacket(void* buffer, struct packet);

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
//int relay(int receiveFD, int sendFD, void* buffer, int bufferSize);

/*************************************
 * isValidAddress
 * 
 * Arguments: char* oldClientAddress, char* newClientAddress,
 *            char* serverVMAddress
 * Returns: int
 * 
 * compares a new address with the old to see if the
 * new address is a valid one
 * 
 * Returns 0 on true, -1 on error, >0 on false
 *************************************/
int isValidAddress(char* oldClientAddress, char* newClientAddress, char* serverVMAddress);

int main(int argc, char** argv)
{
    // SETUP //////////////////////////////////////////////////////////////////////////////////////
    int sessionID = 0;
    segmentType segmentExpected = PACKET_TYPE;
    int bytesExpected = sizeof(uint32_t); // Size of packet.type
    int bytesRead = 0;
    
    // Booleans that keep track of which sockets are connected
    int clientConnected = 0; // 0 false, !0 true
    int serverConnected = 0; // 0 false, !0 true
    
    int listenSocketFD, clientSocketFD, serverSocketFD; // Socket file descriptor
    fd_set socketSet;
    in_port_t listenPort;
    struct sockaddr_in listenAddress, serverAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;
    void* sendBuffer = NULL;
    void* receiveBuffer = NULL;
    int receiveBufferIndex = 0;

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

    // defining the heartbeat packet with session ID
    struct packet heartbeatPacket;
    heartbeatPacket.type = (uint32_t) 0;
    heartbeatPacket.length = (uint32_t) sizeof(int);
    heartbeatPacket.payload = (void*) &sessionID;

    // defining the data packet
    struct packet dataPacket;
    dataPacket.type = (uint32_t) 1;
    dataPacket.length = (uint32_t) 0;
    
    // Attempt to allocate space for dataPacket payload
    dataPacket.payload = malloc(BUFFER_LEN);
    if (dataPacket.payload == NULL)
    {
        perror("Unable to allocate space to store payload of dataPacket");
        return -1;
    }

    // defining the receivedPacket
    struct packet receivedPacket;
    receivedPacket.length = (uint32_t) 0;

    // Attempt to allocate space for receivedPacket payload
    receivedPacket.payload = malloc(BUFFER_LEN);
    if (receivedPacket.payload == NULL)
    {
        perror("Unable to allocate space to store payload of receivedPacket");
        return -1;
    }

    // Attempt to allocate space for sendBuffer
    sendBuffer = malloc(2*sizeof(uint32_t) + BUFFER_LEN);
    if (sendBuffer == NULL)
    {
        perror("Unable to allocate space for the sendBuffer");
        return -1;
    }

    // Attempt to allocate space for receiveBuffer
    receiveBuffer = malloc(BUFFER_LEN);
    if (receiveBuffer == NULL)
    {
        perror("Unable to allocate space for the sendBuffer");
        return -1;
    }

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

    // populate info for telnet daemon into serverAddress
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(LOCALHOST);
    serverAddress.sin_port = htons(TELNET_PORT);

    // Infinite loop, continue to listen for new connections //////////////////////////////////////
    while (1)
    {
        if (clientConnected == 0)
        {
            printf("client is not connected. Connecting...\n");
            clientConnectionAttempts++;
            if (clientConnectionAttempts >= 10)
            {
                printf("Too many connection attempts\n");
                return -1;
            }
            
            // accept a new client
            printf("sproxy waiting for new connection...\n");
            clientSocketFD = accept(listenSocketFD, &clientAddress, &clientAddressLength);
            if (clientSocketFD < -1) // accept returns -1 on error
            {
                perror("sproxy unable to receive connection from client");
                continue; // Repeat loop to receive another client connection
            }
            else
            {
                clientConnected = 1;
                printf("sproxy accepted new connection from client!\n");

                //TODO: Add logic to check if the new sessionID of the client is the same as before
                // If the sessionID is different, need to close serverSocketFD
            }
        }

        if (!serverConnected)
        {
            printf("server is not connected. Connecting...\n");

            serverConnectionAttempts++;
            if (serverConnectionAttempts >= 10)
            {
                printf("Too many connection attempts\n");
                return -1;
            }
            
            // Create server socket
            serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocketFD < 0) // socket returns -1 on error
            {
                perror("sproxy unable to create server socket");
                continue; // move to attempt to reconnect to telnet daemon
            }

            // Connect to server
            printf("sproxy attempting to connect to server...\n");
            if (connect(serverSocketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) != 0)
            {
                perror("sproxy unable to connect to telnet daemon");
                continue; // move to attempt to reconnect to telnet daemon
            }
            printf("sproxy successfully connected to telnet daemon!\n");
        }

        if ((serverConnected != 0) && (clientConnected != 0))
        {
            printf("Client and Server are both connected\n");
            
            // Reset segmentExpected to PACKET_TYPE and bytesExpected to sizeof(uint32_t)
            segmentExpected = PACKET_TYPE;
            bytesExpected = sizeof(uint32_t);

            // Set receiveBufferIndex to 0
            receiveBufferIndex = 0;
            
            // Create nextTimeout timeval, and set it to currentTime to ensure the first message sent is a heartbeat
            struct timeval nextTimeout;
            gettimeofday(&nextTimeout, NULL);

            // Use select for data to be ready on both serverSocket and clientSocket //////////////////
            while (1)
            {   
                // Reset socketSet
                FD_ZERO(&socketSet); // zero out socketSet
                FD_SET(serverSocketFD, &socketSet); // add server socket
                FD_SET(clientSocketFD, &socketSet); // add client socket

                // Calculate new timeout value (passing a returned timeout value from a previous select call does not work on all OSs)
                struct timeval currentTime;
                gettimeofday(&currentTime, NULL);
                struct timeval timeout;
                timersub(&nextTimeout, &currentTime, &timeout);
                if (timeout.tv_sec < 0) // If it came back negative, set to zero
                {
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;
                }

                // Wait up to one second for input to be available using select
                int resultOfSelect = select(
                        max(serverSocketFD, clientSocketFD) + 1,
                        &socketSet,
                        NULL,
                        NULL,
                        &timeout
                    );
                // If select timed out, check for a heartbeat from cproxy, and also send one
                if(resultOfSelect == 0 )
                {
                    gettimeofday(&newTime, NULL);
                    timersub(&newTime, &timeLastMessageSent, &timeDif); // getting the time difference
                    if(timeDif.tv_sec >= 3) // if the time difference is 3 or greater
                    {
                        //TODO: close the sockets between cproxy and sproxy
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Add a second to nextTimeout
                    nextTimeout.tv_sec += 1;
                    printf("Would have send a heartbeat to cproxy now\n");
                }
                // If there was an error with select, this is non recoverable
                else if (resultOfSelect < 0)
                {
                    perror("FATAL: sproxy unable to use select to wait for input");

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

                // If input is ready on serverSocket, construct a packet and send to client socket
                if (FD_ISSET(serverSocketFD, &socketSet))
                {   
                    int serverBytesRead = recv(serverSocketFD, dataPacket.payload, BUFFER_LEN, 0);
                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (serverBytesRead < 0)
                    {
                        printf("recv() returned with %i on serverSocketFD\n", serverBytesRead);
                        
                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Create packet and send to clientSocketFD
                    dataPacket.length = serverBytesRead;
                    int bytesToSend = compressPacket(sendBuffer, dataPacket);
                    int bytesSent = send(clientSocketFD, sendBuffer, bytesToSend, 0);
                    // Report if there was an error (just for debugging, no need to exit)
                    if (bytesSent < 0)
                    {
                        perror("Unable to send data to cproxy");
                    }
                    else
                    {
                        printf("Sent data to cproxy\n");
                    }
                }

                // If input is ready on clientSocket, place data into receivedPacket
                if (FD_ISSET(clientSocketFD, &socketSet))
                {   
                    // Try to read bytesExpected into receiveBuffer, starting at receiveBufferIndex
                    bytesRead = recv(clientSocketFD, receiveBuffer + receiveBufferIndex, bytesExpected, 0);

                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (bytesRead < 0)
                    {
                        printf("recv() returned with %i on clientSocketFD\n", bytesRead);

                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Update receiveBufferIndex
                    receiveBufferIndex += bytesRead;

                    // Update bytesExpected
                    bytesExpected -= bytesRead;

                    // If bytesExpected is 0, we just finished reading a packet segment
                    if (bytesExpected == 0)
                    {
                        // Reset receiveBufferIndex
                        receiveBufferIndex = 0;
                        
                        switch (segmentExpected)
                        {
                            case PACKET_TYPE:

                                // Copy packet type data into receivedPacket.type
                                receivedPacket.type = *(uint32_t*) receiveBuffer;

                                // Change segmentExpected to LENGTH
                                segmentExpected = LENGTH;

                                // Update bytesExpected to sizeof(uint32_t)
                                bytesExpected = sizeof(uint32_t);

                                break;
                            case LENGTH:

                                // Copy packet length data into receivedPacket.length
                                receivedPacket.length = *(uint32_t*) receiveBuffer;

                                // Change segmentExpected to PAYLOAD
                                segmentExpected = PAYLOAD;

                                // Update bytesExpected to receivedPacket.length
                                bytesExpected = receivedPacket.length;

                                break;
                            case PAYLOAD:
                                
                                // Copy packet payload data into receivedPacket.payload
                                memcpy(receivedPacket.payload, receiveBuffer, receivedPacket.length);

                                // Change segmentExpected to PACKET_TYPE
                                segmentExpected = PACKET_TYPE;

                                // Update bytesExpected to sizeof(uint32_t)
                                bytesExpected = sizeof(uint32_t);

                                // If the packet is a data packet, send the payload to server
                                if (receivedPacket.type != 0)
                                {
                                    printf("Data packet received from sproxy\n");

                                    int bytesSent = send(serverSocketFD, receivedPacket.payload, receivedPacket.length, 0);

                                    // Report if there was an error (just for debugging, no need to exit)
                                    if (bytesSent < 0)
                                    {
                                        perror("Unable to send data to cproxy");
                                    }
                                    else
                                    {
                                        printf("Sent data to tcproxy\n");
                                    }
                                }
                                else
                                {
                                    printf("Heartbeat received from cproxy\n");
                                }

                                break;
                        }
                    }
                }
            }
        }
    }

    // Close listen socket
    if (close(listenSocketFD)) // close returns -1 on error
    {
        perror("sproxy unable to close listen socket");
    }

    // free buffers
    free(dataPacket.payload);
    free(receivedPacket.payload);
    free(sendBuffer);
    free(receiveBuffer);

    return 0;
}

/*int relay(int receiveFD, int sendFD, void* buffer, int bufferSize)
{
    uint32_t packetType;
    uint32_t payloadLength;
    int newSessionID;

   // reading in the packet type
    ssize_t bytesRead = recv(receiveFD, buffer, sizeof(uint32_t), 0);
    if (bytesRead < 1) return bytesRead;
    packetType = *((uint32_t*) buffer);

    // reading in the payload length
    bytesRead = recv(receiveFD, buffer, sizeof(uint32_t), 0);
    if (bytesRead < 1) return bytesRead;
    payloadLength = *((uint32_t*) buffer);

    // reading the payload
    bytesRead = recv(receiveFD, buffer, payloadLength, 0);
    if (bytesRead < 1) return bytesRead;

    // acting on the message 
    if(packetType == 0) // if the message is a heartbeat
    {
        newSessionID = *((int*) buffer);
        if(sessionID == 0) // if this is the first heartbeat message for the server
        {
            printf("first heartbeat read\n");
            sessionID = newSessionID;
        } else if (sessionID != newSessionID) // if this is a different session (reset Daemon???)
        {
            printf("sessionID changed\n");
            // TODO something something Daemon
        }
        gettimeofday(&timeLastMessageSent, NULL); // setting the time of heartbeat recieved
        printf("heartbeat read at %li seconds\n", timeLastMessageSent.tv_sec);
    } else // if the message is a data one
    {
         // Write from buffer to sendFD
        printf("data read\n");
        ssize_t bytesSent = send(sendFD, buffer, bytesRead, 0);
        if (bytesSent < 1) // Returns -1 on error
        {
            printf("Unable to send bytes\n");
            return -1;
        }
    }
    return 0;
}*/

int max(int a, int b)
{
    if (a > b)
    {
        return a;
    }

    return b;
}

int compressPacket(void* buffer, struct packet pck)
{
    int index = 0;

    // Write in packet type
    *(uint32_t*) (buffer+index) = pck.type;
    index += sizeof(uint32_t);

    // Write in payload length
    *(uint32_t*) (buffer+index) = pck.length;
    index += sizeof(uint32_t);

    // Write in the payload
    memcpy(buffer+index, pck.payload, pck.length);
    index += pck.length;

    return index; // This should now equal the size of the data in buffer
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
