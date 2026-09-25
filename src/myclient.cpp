#include <iostream>
#include <sys/stat.h>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

using namespace std;

const int SEND_MAX = 4;

void printLog(const char* type, int SN, int initialSN, int nextSN, int ws){
    time_t now;
    struct tm *utc_tm;
    char iso_time[25];

    // Get current time
    now = time(NULL);

    // Convert to UTC time
    utc_tm = gmtime(&now);

    // Format as ISO format
    strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", utc_tm);
    
    cout << iso_time << ", " << type << ", " << SN << ", " << initialSN << ", " << nextSN << ", " << (initialSN + ws) << endl;
}

int main(int argc, char* argv[]){
    // Check argument count
    if(argc != 7){
        cerr << "Invalid number of command line arguments." << endl;
        exit(-1);
    }

    // Verify file integrity
    char* infile = argv[5];
    
    // QUESTION: How to verify outfile
    // char* outfile = argv[6];

    // Check infile integrity
    struct stat sb;
    if(::stat(infile, &sb) == 0){
        // Check if input is directory
        if (S_ISDIR(sb.st_mode)){
            cerr << "Infile input is directory. Please provide a File Path. Exiting..." << endl;
            exit(-1);
        }
    }
    else{
        cerr << "File does not exist: " << infile << endl;
        exit(-1);
    }
    
    // QUESTION: what should we do about the mss and the header size
    // ANSWER: just handle it bro

    // Construct example header to get header size
    int headerSize = (strlen("CTRL\nSN: 0\nOut: \r\n") + strlen(argv[6]))*sizeof(char);
    // cout << "Header Size: " << headerSize << endl;

    // Verify MSS value
    int mss = strtol(argv[3], nullptr, 10);
    int mtu = -1;
    // Header size should be 
    if(mss < headerSize){
        cerr << "Required minimum MSS is " << headerSize << " + 1. Exiting..." << endl;
        exit(-1);
    }
    else{
        mtu = mss - headerSize;
    }
    
    // Verify Window Size
    int windowsize = strtol(argv[4], nullptr, 10);
    if(windowsize <= 0){
        cerr << "Invalid windowsize input." << endl;
        exit(-1);
    }

    
    // Verify port
    int port = -1;
    try{
        port = strtol(argv[2], nullptr, 10);
        // cout << port << endl;
    }
    catch(const exception& e){
        cerr << "Exception: " << e.what() << endl;
        exit(-1);
    }
    if(port == 0){
        cerr << "Invalid port argument" << endl;
        exit(-1);
    }

    // Verify IP Address
    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if(inet_pton(AF_INET, argv[1], &server.sin_addr) <= 0) {
        cerr << "Invalid IP address / Address not supported" << endl;
        exit(-1);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        cerr << "Error in creating socket" << endl;
        exit(-1);
    }

    // Set receive timeout
    struct timeval timeout;
    timeout = (struct timeval){0};
    timeout.tv_sec = 20;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        cerr << "Unable to set a receiving timeout on socket" << endl;
        close(sock);
        exit(-1);
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        cerr << "Unable to set a sending timeout on socket" << endl;
        close(sock);
        exit(-1);
    }
    
    // Open infile
    FILE *in = fopen(infile, "r");
    if(in == NULL){
        cerr << "Unable to open infile: " << infile << endl;
        close(sock);
        exit(-1);
    }

    // Handshake
    // Send initial SN and file_dest
    // Construct handshake
    const char* handshake = "CTRL\nSN: 0\nOut: ";

    char* sendReq = (char*) malloc((strlen(handshake) + strlen(argv[6]) + 3) * sizeof(char));
    memcpy(sendReq, handshake, strlen(handshake));
    memcpy(sendReq + strlen(handshake), argv[6], strlen(argv[6]));
    memcpy(sendReq + strlen(handshake) + strlen(argv[6]), "\r\n\0", 3);
    
    // waiting for ACK
    int sendCounter = 0;
    int bytesRead = -1;
    const int BUF_SIZE = 32000;
    char buf[BUF_SIZE];
    bool acked = false;

    // Packet types
    const char* ACK = "ACK";
    const char* DROP = "DROP";
    // Resend req if not ACKed and within resend limits
    while(bytesRead < 0 || sendCounter < SEND_MAX){
        // Send handshake
        sendto(sock, sendReq, strlen(sendReq), 0, (struct sockaddr *)&server, sizeof(server));
        sendCounter+=1;
        printLog("CTRL", 0, 0, 0, 0);
        // break;
        // Check if ACKed
        bytesRead = recvfrom(sock, buf, BUF_SIZE, 0, NULL, NULL);
        // cout << bytesRead << endl;
        if(bytesRead > 0){
            buf[bytesRead] = '\0';
            // Check if ACKed
            // Get operation
            char newchar = '\n';
            char* idx = strchr(buf, newchar);
            size_t opSize = idx-buf;

            char* operation = (char*) malloc((opSize+1)*(sizeof(char)));
            memcpy(operation, buf, opSize);
            operation[opSize] = '\0';

            // if ACK, move forward
            if(strcmp(operation, ACK) == 0){
                // Free operation
                free(operation);
                // Get SN
                printLog("ACK", 0, 0, 0, 0);
                acked=true;
                break;
            }
            // if dropped, retransmit
            else if(strcmp(operation, DROP) == 0){
                free(operation);
                printLog("DROP CTRL", -1, -1, -1, 0);
                continue;
            }
        }
        else if(bytesRead < 0 && sendCounter >= SEND_MAX){
            cerr << "Cannot detect server" << endl;
            free(sendReq);
            fclose(in);
            exit(3);
        }
    }
    // Cleanup
    sendCounter = 0;
    free(sendReq);
    // if not ACKed
    if(!acked){
        cerr << "Reached maximum re-transmission limit" << endl;
        fclose(in);
        exit(4);
    }
    
    // Send Loop
    
    // Storing MSS number of bytes
    char sendline[mss];
    int counter = 0;
    int fileRead = 1;
    int SN = 0;

    // Track if packets sent
    bool sent = false;
    // While not eof
    while(fileRead > 0){
        // Keep track of retransmissions
        sendCounter = 0;
        // For windowsize amount of iterations
        int counter = 0;
        // Header
        const char* dataHeader = "DATA\nSN: ";

        // Temp Storage for retransmission purposes
        char* storage[windowsize];
        memset(storage, 0, windowsize);
        // Keep track of sizes
        int storageSize[windowsize];
        for(int i=0; i<windowsize; i++){
            storageSize[i] = -1;
        }

        // Read MTU number of bytes from file
        int initialSN = SN;
        while(fileRead > 0 && counter < windowsize){
            fileRead = fread(sendline, 1, mtu, in);
            // cout << fileRead << endl;
            // Convert SN to text
            int length = snprintf( NULL, 0, "%d", SN );
            char* snText = (char*)malloc( length + 1 );
            snprintf( snText, length + 1, "%d", SN );

            // Construct packet
            // Bytes = sizeofHeader + bytes read from file
            //sendline[fileRead] = '\0';
            char* dataPacket = (char*) malloc((strlen(dataHeader) + length + 2 + fileRead+1) * sizeof(char)); 
            
            memcpy(dataPacket, dataHeader, strlen(dataHeader));
            memcpy(dataPacket + strlen(dataHeader), snText, length);
            memcpy(dataPacket + strlen(dataHeader) + length, "\r\n", 2);
            memcpy(dataPacket + strlen(dataHeader) + length + 2, &sendline, fileRead);
            //memcpy(dataPacket + strlen(dataHeader) + length + 2 + fileRead, "\0", 1);

            // cout << dataPacket << endl;

            // Calculate total packet size
            //int packetSize = strlen(dataHeader) + length + 2 + fileRead + 1;
            int packetSize = strlen(dataHeader) + length + 2 + fileRead;
            // cout << packetSize << endl;
            
            // Send 
            // NEED TO HANDLE RETRANSMISSION
            sendto(sock, dataPacket, packetSize, 0, (struct sockaddr *)&server, sizeof(server));
            printLog("DATA", SN, initialSN, SN+1, windowsize);

            // Increment counter and SN
            // cout << counter << endl;
            // STORE IN STORAGE
            storage[counter] = dataPacket;
            storageSize[counter] = packetSize;
            counter+=1;
            // cout << SN << endl;
            SN+=1;

            //free(dataPacket);
            // Cleanup
            free(snText);
        }
        // cout << "Receive ACKs" << endl;
        // Check for ACKs
        int acksReceived = 0;
        // int mostRecentSN = -1;
        int expectedSN = initialSN + windowsize - 1;
        int response = 0;
        char recvline[mss+1];
        
        // Determine if all acks received
        bool accepted = false;
        while(!accepted && sendCounter < (SEND_MAX-1)){
            while(acksReceived < windowsize && (response = recvfrom(sock, recvline, mss, 0, NULL, NULL)) > 0) {
                //cout << "Bytes received: " << response << endl;
                // Timeout, break
                if(response < 0){
                    // Timeout after max amount of retransmission
                    // Server is most likely down
                    if(sendCounter >= (SEND_MAX-1)){
                        cerr << "Cannot detect server" << endl;
                        // For good measure
                        const char* fin = "FIN\n";
                        sendto(sock, fin, strlen(fin), 0, (struct sockaddr *)&server, sizeof(server));
                        printLog("FIN", -1, -1, -1, 0);
                        // Cleanup
                        close(sock);
                        fclose(in);
                        exit(3);
                    }
                    break;
                }
                // Null terminate
                recvline[response] = '\0';
                //cout << recvline << endl;

                // Get operation
                char newchar = '\n';
                char* idx = strchr(recvline, newchar);
                size_t opSize = idx-recvline;

                char* operation = (char*) malloc((opSize+1)*(sizeof(char)));
                memcpy(operation, &recvline, opSize);
                operation[opSize] = '\0';

                // if ACK, move forward
                if(strcmp(operation, ACK) == 0){
                    // Free operation
                    free(operation);
                    // Get SN
                    char* SNidx = strchr(recvline+opSize+1, newchar);
                    size_t SNsize = SNidx - recvline - opSize - 1 - 4;
                    char* snText = (char*) malloc((SNsize+1)*sizeof(char));
                    memcpy(snText, recvline+opSize+1+4, SNsize);
                    snText[SNsize]='\0';
                    
                    // Convert to int
                    int ackedSN = strtol(snText, nullptr, 10);
                    // if SN in expected range, note
                    if(ackedSN >= initialSN || ackedSN <= expectedSN){
                        acksReceived+=1;
                        printLog("ACK", ackedSN, initialSN, ackedSN+1, windowsize);
                    }
                    // Cleanup
                    free(snText);
                }
                // if dropped, break
                else if(strcmp(operation, DROP) == 0){
                    free(operation);
                    printLog("DROP DATA", -1, -1, -1, 0);
                    break;
                }
            }
            // if not all acks received before end of loop, retransmit
            if(acksReceived < windowsize){
                // Go-Back-N
                for(int i=0; i<windowsize; i++){
                    if(storageSize[i] != -1){
                        sendto(sock, storage[i], storageSize[i], 0, (struct sockaddr *)&server, sizeof(server));
                        printLog("DATA", initialSN+i, initialSN, initialSN+i+1, windowsize);
                    }
                }
                // redo until all acks returned or max retransmission
                sendCounter+=1;
            }
            else{
                accepted=true;
            }
        }
        // Clean the storage
        for(int i=0; i<windowsize; i++){
            if(storage[i] != 0){
                free(storage[i]);
            }
        }
        // if max retransmission hit and still not completely acked,
        if(sendCounter >= (SEND_MAX-1) && !accepted){
            cerr << "Reached maximum re-transmission limit" << endl;
            // Close on server side
            const char* fin = "FIN\n";
            sendto(sock, fin, strlen(fin), 0, (struct sockaddr *)&server, sizeof(server));
            // Cleanup
            fclose(in);
            close(sock);
            exit(4);
        }
    }
    // cout << "Sending complete" << endl;
    // Send FIN
    const char* fin = "FIN\n";
    sendto(sock, fin, strlen(fin), 0, (struct sockaddr *)&server, sizeof(server));
    printLog("FIN", -1, -1, -1, 0);
    // Cleanup
    fclose(in);
    close(sock);
    exit(0);
}