#include <iostream>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <zconf.h>
#include <netdb.h>
#include <arpa/inet.h>

class File {
private:
    FILE* file_pointer_ = NULL;
public:
    File(const char* path_name, const char* mode) {
        file_pointer_ = fopen(path_name, mode);

        if(file_pointer_ == NULL) {
            fprintf(stderr, "Problem during open file\n");
        }
    }

    ~File() {
        if(file_pointer_ != NULL) {
            fclose(file_pointer_);
        }
    }


    void Read(char* buffer, size_t size) {

    }

};

#define MAXPENDING 1000

void die(char* e) {
    perror(e);
    exit(1);
}

void DieWithError(char* error) {
    perror(error);
    exit(EXIT_FAILURE);
}


int CreateServerSocket(int port) {
    int socket_fd;
    struct sockaddr_in server_addr;

    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        DieWithError("socket() failed");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    /* Bind */
    if(bind(socket_fd, (struct sockaddr*)&server_addr,  sizeof(server_addr)) < 0) {
        DieWithError("bind()");
    }

    /* Mark the socket */
    if(listen(socket_fd, MAXPENDING) < 0) {
        DieWithError("listen()");
    }

    return socket_fd;

}

int AcceptTCPConnection(int server_socket) {
    int client_socket;
    struct sockaddr_in client_addr;
    unsigned int client_len;

    client_len = sizeof(client_addr);

    /* Wait */
    if ((client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_len)) < 0) {
        DieWithError("acept9)");
    }

    printf("Handling client %s \n", inet_ntoa(client_addr.sin_addr));

    return client_socket;
}



int main() {

    int server_socket;
    int client_socket;
    unsigned short port;
    pid_t processID;
    unsigned int child_process_count;

    port = 1234;


    server_socket = CreateServerSocket(port);




    for(;;) {
        client_socket = AcceptTCPConnection(server_socket);


        if((processID = fork()) < 0) {
            DieWithError("fork() failed");
        } else if(processID == 0) {
            close(server_socket);
            HandleTCPClient(client_socket);
            exit(0);
        }

        printf("With child process %d\n", (int)processID);
        close(client_socket); /* Parent closes child socket descritpor */
        child_process_count++; 
        
        
    }





    return 0;
}