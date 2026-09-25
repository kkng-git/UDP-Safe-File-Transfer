#include <iostream>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <time.h>
#include <map>
#include <sys/stat.h>
#include <fstream>
#include <time.h>

using namespace std;

const int BUF_SIZE = 32000;

// Used for key comparisons between char*
struct cmp_str
{
   bool operator()(char const *a, char const *b) const
   {
        /*int value = strcmp(a,b);
        cout << "STRCMP: " << (value == 0) << endl;*/
        return strcmp(a, b) < 0;
   }
};

// Gracefully handling interrupt
volatile sig_atomic_t interrupted = 0;

void signal_handler(int signal) {
    interrupted = 1;
}

void printLog(const char* type, int SN){
    time_t now;
    struct tm *utc_tm;
    char iso_time[25];

    // Get current time
    now = time(NULL);

    // Convert to UTC time
    utc_tm = gmtime(&now);

    // Format as ISO format
    strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", utc_tm);
    
    cout << iso_time << ", " << type << ", " << SN << endl;
}

int main(int argc, char* argv[]){
    // Check argument count
    if(argc != 3){
        cerr << "Invalid number of command line arguments." << endl;
        exit(-1);
    }

    // Verify port number
    int port = -1;
    try{
        port = strtol(argv[1], nullptr, 10);
    }
    catch(const exception& e){
        cerr << "Exception: " << e.what() << endl;
        exit(-1);
    }
    if(port <= 0){
        cerr << "Invalid port argument" << endl;
        exit(-1);
    }

    // Verify drop rate
    int dropRate = -1;
    try{
        dropRate = strtol(argv[2], nullptr, 10);
    }
    catch(const exception& e){
        cerr << "Exception: " << e.what() << endl;
        exit(-1);
    }
    if(dropRate < 0 || dropRate > 100){
        cerr << "Invalid Drop Rate argument" << endl;
        exit(-1);
    }

    // Calculate how many packets before dropping


    // Create a UDP socket
    int sockfd;
    struct sockaddr_in receive;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        cerr << "Error creating socket" << endl;
        exit(-1);
    }

    // Set receive timeout
    struct timeval timeout;
    timeout = (struct timeval){0};
    timeout.tv_sec = 18000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "Unable to set a timeout on socket" << endl;
        close(sockfd);
        exit(-1);
    }

    // Set up receiving address
    bzero(&receive, sizeof(receive));
    receive.sin_family = AF_INET;
    receive.sin_port = htons(port);
    receive.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind to socket
    if(::bind(sockfd, (struct sockaddr *)&receive, sizeof(receive)) < 0){
        cerr << "Error binding socket" << endl;
        close(sockfd);
        exit(-1);
    }

    // Store client details
    struct sockaddr_in client;

    // Set up data reception
    int bytesRead;
    socklen_t len;
    char buf[BUF_SIZE];
    
    // Handle interrupt
    signal(SIGINT, signal_handler);

    // Maps
    map<char*, int, cmp_str> sequenceNumberTrack;
    map<char*, char*, cmp_str> filePathTrack;

    // Packet types
    const char* CONTROL = "CTRL";
    const char* DATA = "DATA";
    const char* FIN = "FIN";

    while(!interrupted){
        // Receive
        len = sizeof(client);
        bytesRead = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr *)&client, &len);

        // Error?
        if(bytesRead < 0){
            break;
            /*cerr << "Unable to receive from client" << endl;
            // Cleanup
            for (auto& i : filePathTrack){
                //cout << i.first << " \t\t\t " << i.second << endl;
                if(i.second != nullptr){
                    free(i.second);
                }
            }
            sequenceNumberTrack.clear();
            filePathTrack.clear();
            exit(-1);*/
        }
        // sendto(sockfd, buf, bytesRead, 0, (struct sockaddr *)&client, len);
        // cout << bytesRead << endl;
        
        // null terminate
        // buf[bytesRead] = '\0';
        //cout << buf << endl;

        // Destruct request
        char newchar = '\n';
        char rchar = '\r';
        char* idx = strchr(buf, newchar);
        size_t opSize = idx-buf;

        char* operation = (char*) malloc((opSize+1)*(sizeof(char)));
        memcpy(operation, &buf, opSize);
        
        // Null terminate
        operation[opSize] = '\0';
        //cout << operation << endl;
        //cout << operation-buf << endl;
        
        // If CTRL packet
        if(strcmp(operation, CONTROL) == 0){
            // Free operation
            free(operation);
            // Calculate whether drop or not
            if(rand()%100 < dropRate){
                // Drop
                printLog("DROP CTRL", 0);
                const char* drop = "DROP\n";
                sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));
                continue;
            }
            // Log
            printLog("CTRL", 0);
            //cout << "CONTROL" << endl;
            
            // Grab SN
            char* SNidx = strchr(buf+opSize+1, newchar);
            
            // Isolate the digit portion of SN
            size_t SNsize = SNidx - buf - opSize - 1 - 4;
            char* snText = (char*) malloc((SNsize+1)*sizeof(char));
            memcpy(snText, buf+opSize+1+4, SNsize);
            snText[SNsize]='\0';

            // Convert to int
            int initialSN = strtol(snText, nullptr, 10);

            // Grab filepath
            char* fpIDX = strchr(buf, rchar);
            
            // Isolate filepath part
            size_t fpSize = fpIDX - buf - opSize - 1 - 4 - SNsize - 1 - 5;
            char* filePath = (char*) malloc((fpSize+1)*sizeof(char));
            memcpy(filePath, buf+opSize+1+4+SNsize+1+5, fpSize);
            filePath[fpSize]='\0';

            // Check outfile integrity
            struct stat sb;
            if(::stat(filePath, &sb) == 0){
                // Check if output is directory?
                    if (S_ISDIR(sb.st_mode)){
                        cerr << "Outfile input is directory. Drop packet." << endl;
                        // Cleanup
                        free(filePath);
                        free(snText);
                        continue;
                    }
            }
            else{
                // File does not exist, creating
                ofstream file(filePath);
                file.close();
            }

            // Extract IP address
            char* addrs = inet_ntoa(client.sin_addr);
            int port = ntohs(client.sin_port);
            // Convert port to text
            int length = snprintf( NULL, 0, "%d", port );
            char* portText = (char*)malloc( length + 1 );
            snprintf( portText, length + 1, "%d", port );
            portText[length] = '\0';
            //char* ip = (char*)malloc((length+1+strlen(addrs))*sizeof(char));
            char ip[strlen(addrs) + length + 1];
            memcpy(ip, addrs, strlen(addrs));
            memcpy(ip+strlen(addrs), portText, length+1);
            // ip[length+1+strlen(addrs)] = '\0';
            // cout << ip << endl;
            // if ip & port already in use, drop
            if(filePathTrack.count(ip) > 0){
                cerr << "Client already registered" << endl;
                // Drop
                printLog("DROP CTRL", initialSN);
                const char* drop = "DROP\n";
                sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));
                continue;
            }
            else{
                // Map
                sequenceNumberTrack[ip] = initialSN;
                char* filePathEntry = (char*) malloc((fpSize+1)*sizeof(char));
                memcpy(filePathEntry, filePath, (fpSize+1)*sizeof(char));
                filePathTrack[ip] = filePathEntry;
                
                //filePathTrack[ip] = (char*) malloc((fpSize+1)*sizeof(char));
                //memcpy(filePathTrack[ip], filePath, (fpSize+1)*sizeof(char));

                // ACK
                if(rand()%100 < dropRate){
                    // Drop
                    printLog("DROP ACK", 0);
                    const char* drop = "DROP\n";
                    sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));
                    // Cleanup
                    free(portText);
                    // free(ip);
                    free(filePath);
                    free(snText);
                    continue;
                }
                const char* ack = "ACK\nSN: 0\r\n";
                sendto(sockfd, ack, strlen(ack), 0, (struct sockaddr *)&client, sizeof(client));
                // Log
                printLog("ACK", initialSN);
            }

            // Client should be registered at this point

            // Cleanup
            free(portText);
            // free(ip);
            free(filePath);
            free(snText);
        }
        else if(strcmp(operation, DATA) == 0){
            // Free operation
            free(operation);
            // Extract IP address
            char* addrs = inet_ntoa(client.sin_addr);
            int port = ntohs(client.sin_port);
            // Convert port to text
            int length = snprintf( NULL, 0, "%d", port );
            char* portText = (char*)malloc( length + 1 );
            snprintf( portText, length + 1, "%d", port );
            portText[length] = '\0';
            //char* ip = (char*)malloc((length+1+strlen(addrs))*sizeof(char));
            char ip[strlen(addrs) + length + 1];
            memcpy(ip, addrs, strlen(addrs));
            memcpy(ip+strlen(addrs), portText, length+1);
            // ip[length+1+strlen(addrs)] = '\0';

            // Grab SN
            char* SNidx = strchr(buf+opSize+1, newchar);
            size_t SNsize = SNidx - buf - opSize - 1 - 4;
            char* snText = (char*) malloc((SNsize+1)*sizeof(char));
            memcpy(snText, buf+opSize+1+4, SNsize);
            snText[SNsize]='\0';
            int receivedSN = strtol(snText, NULL, 10);
            // cout << receivedSN << endl;

            // Check if client is a registered client
            // cout << "FILEPATHCOUNT:" << filePathTrack.count(ip) << endl;
            if(filePathTrack.count(ip) <= 0){
                // Drop if not registered
                cerr << "IP not yet registered" << endl;
                // Log
                printLog("DROP DATA", receivedSN);
                // Cleanup
                free(snText);
                free(portText);
                continue;
            }

            // Check if needs to be dropped
            if(rand()%100 < dropRate){
                // Drop
                printLog("DROP DATA", 0);
                const char* drop = "DROP\n";
                sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));
                // Cleanup
                free(portText);
                free(snText);
                continue;
            }

            // Check sequence number
            // if not sequential
            // duplicate
            if(receivedSN < sequenceNumberTrack[ip]){
                // Drop
                printLog("DROP DATA", receivedSN);
                cerr << "Dropped duplicate packet" << endl;
                // ACK Anyways
                // Construct ACK for most recent packet
                const char* ackHeader = "ACK\nSN: ";
                char ack[8+SNsize+1];
                memcpy(ack, ackHeader, 8);
                memcpy(ack + 8, snText, SNsize);
                memcpy(ack + 8 + SNsize, "\n", 1);
                
                // Send ACK
                sendto(sockfd, ack, 8+SNsize+1, 0, (struct sockaddr *)&client, sizeof(client));
                // Log
                printLog("ACK", receivedSN);
                // Cleanup
                free(portText);
                free(snText);
                continue;
            }
            else if(receivedSN != sequenceNumberTrack[ip]){
                // Out of order
                // Log
                printLog("DROP DATA", receivedSN);
                cerr << "Dropped out of order packet" << endl;
                const char* drop = "DROP\n";
                sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));
                // Cleanup
                free(portText);
                free(snText);
                continue;
            }
            // Log
            printLog("DATA", receivedSN);
            
            // If sequential write to file
            // Get filepath
            char* filePath = filePathTrack.at(ip);
            
            // Try to open file
            // If SN = 0 then it is the first packet
            FILE *out = NULL;
            if(receivedSN == 0){
                out = fopen(filePath, "w");
            }
            else{
                out = fopen(filePath, "a");
            }
            if(out == NULL){
                // if unable to open, drop
                cerr << "Unable to open outfile." << endl;
                // Cleanup
                free(snText);
                continue;
            }
            // Extract buffer
            //char* rcharIdx = strchr(buf, rchar);
            //char* bufStart = strchr(rcharIdx, newchar);
            char* bufStart = strchr(buf, rchar);
            int headerSize = bufStart - buf + 2;
            //cout << dataSize << endl;

            int res = fwrite(bufStart+2, sizeof(char), bytesRead - headerSize, out);

            // Increment SN
            sequenceNumberTrack[ip]+=1;

            // Check if ACK drops
            if(rand()%100 < dropRate){
                // Drop
                printLog("DROP ACK", 0);
                /*const char* drop = "DROP\n";
                sendto(sockfd, drop, strlen(drop), 0, (struct sockaddr *)&client, sizeof(client));*/
                // Cleanup
                free(portText);
                // free(filePath);
                free(snText);
                fclose(out);
                continue;
            }
            // Construct ACK for most recent packet
            const char* ackHeader = "ACK\nSN: ";
            char ack[8+SNsize+1];
            memcpy(ack, ackHeader, 8);
            memcpy(ack + 8, snText, SNsize);
            memcpy(ack + 8 + SNsize, "\n", 1);
            
            // Send ACK
            sendto(sockfd, ack, 8+SNsize+1, 0, (struct sockaddr *)&client, sizeof(client));
            // Log
            printLog("ACK", receivedSN);

            // Cleanup
            free(portText);
            free(snText);
            fclose(out);
        }
        else if(strcmp(operation, FIN)==0){
            // Free operation
            free(operation);
            // Extract IP address
            char* addrs = inet_ntoa(client.sin_addr);
            int port = ntohs(client.sin_port);
            // Convert port to text
            int length = snprintf( NULL, 0, "%d", port );
            char* portText = (char*)malloc( length + 1 );
            snprintf( portText, length + 1, "%d", port );
            char ip[strlen(addrs) + length + 1];
            memcpy(ip, addrs, strlen(addrs));
            memcpy(ip+strlen(addrs), portText, length+1);

            auto entry = filePathTrack.find(ip);
            if (entry != filePathTrack.end()) {
                // Get the address of the key
                const char* keyAddress = entry->first;
                // Remove entry
                free(entry->second);
                filePathTrack.erase(ip);
                sequenceNumberTrack.erase(ip);
            }
            // free 
            free(portText);

            // Log
            printLog("FIN", -1);
        }
        else{
            free(operation);
            cerr << "ERROR" << endl;
        }
    }

    cout << "Closing server.." << endl;
    // Cleanup
    /*for (auto& i : filePathTrack){
        //cout << i.first << " \t\t\t " << i.second << endl;
        if(i.second != nullptr){
            free(i.second);
        }
    }*/
    /*
    for (auto& i : sequenceNumberTrack){
        //cout << i.first << " \t\t\t " << i.second << endl;
        if(i.first != nullptr){
            free(i.first);
        }
    }*/
    sequenceNumberTrack.clear();
    filePathTrack.clear();
    close(sockfd);

    exit(0);
}