#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>

#define BUFFER_SIZE 65537

typedef struct
{
    char name[255];
    int sock;
} User;

static char* name_message = "Please enter your name: ";
static char* welcome_message = "Welcome to the chatroom!\n\n";
static User current_users[1000];
static int num_users=0;
pthread_mutex_t lock;


/* broadcast message to everyone except the origin of the message*/
void broadcast(int origin_sock, char* message) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < num_users; i++){
        int sock = current_users[i].sock;
        if (sock != origin_sock){
            write(sock, message, strlen(message));
        }
    }
    pthread_mutex_unlock(&lock);
}

void list_users(int origin_sock) {
    char message[BUFFER_SIZE];
    pthread_mutex_lock(&lock);
    if (num_users == 1){
        strcpy(message, "You are the first here!\n");
    }
    else {
        strcpy(message, "Currently in the chatroom:\n");
        for (int i = 0; i < num_users; i++){
            int sock = current_users[i].sock;
            if (sock != origin_sock){
                strcat(message, "\t");
                strcat(message, current_users[i].name);
                strcat(message, "\n");
            }
        }
    }
    strcat(message, "\n");
    write(origin_sock, message, strlen(message));
    pthread_mutex_unlock(&lock);
}


/* this function handles the client request in a separate thread */
void* chat(void* sockptr) {
    int sock = *(int*) sockptr;
    free(sockptr);

    // Ask for user name
    int recv_count = 0;
    char name[255];
    // need to fix this
    while (!recv_count) {
        write(sock, name_message, strlen(name_message));
        recv_count = recv(sock, name, 255, 0);
        if(recv_count<3) {
            recv_count = 0;
        }
        else {
            write(sock, welcome_message, strlen(welcome_message));
        }
    }

    name[recv_count-2]=0;

    // put socket into user list
    pthread_mutex_lock(&lock);
    User *cur = (User *)malloc(sizeof(User));
    strcpy(cur->name, name);
    cur->sock = sock;
    current_users[num_users] = *cur;
    num_users++;
    pthread_mutex_unlock(&lock);

    // print list of people who are in chatroom
    list_users(sock);

    // tell everyone user has entered
    char enter_notification[strlen(name)+50];
    strcpy(enter_notification, "-----> ");
    strcat(enter_notification, name);
    strcat(enter_notification, " has entered\n");
    broadcast(sock, enter_notification);

    // Wait until a message is received, and then send it to everyone else
    // use the poll system call to be notified about socket status changes
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN | POLLHUP | POLLRDNORM;
    pfd.revents = 0;
    int chatting = 1;
    int num_events = 0;
    // call poll with a timeout of 100 ms
    // if result > 0, this means that there is either data available on the
    // socket, or the socket has been closed
    while (chatting) {
        if (poll(&pfd, 1, 100)) {
            char* buffer = malloc(BUFFER_SIZE);
            int result = recv(sock, buffer, BUFFER_SIZE, 0);
            if (result == 0) {
                // if recv returns zero, that means the connection has been closed
                chatting = 0;
            }
            else if (result > 0) {
                buffer[result] = 0;
                char* chat_message = malloc(strlen(name)+strlen(buffer)+5);
                strcpy(chat_message, name);
                strcat(chat_message, ": ");
                strcat(chat_message, buffer);
                broadcast(sock, chat_message);
                free(chat_message);
            }
            free(buffer);
        }
    }

    // When user leaves the chat room, takes self off list
    pthread_mutex_lock(&lock);
    for (int i = 0; i < num_users; i++){
        if (current_users[i].sock == sock){
            if (num_users > 0){
                current_users[i] = current_users[num_users-1];
            }
            break;
        }
    }
    num_users--;
    pthread_mutex_unlock(&lock);

    // notifies everyone else that I am leaving
    char leave_notification[strlen(name)+50];
    strcpy(leave_notification, "<----- ");
    strcat(leave_notification, name);
    strcat(leave_notification, " has exited\n");
    broadcast(sock, leave_notification);

    shutdown(sock,SHUT_RDWR);
    close(sock);
    pthread_exit(0);
}


int main(int argc, char** argv) {
    // select port
    int port;
    if (argc > 1) {
        int port_status = sscanf(argv[1], "%d", &port);
        if(!port_status) {
            perror("Port must be a number ");
            exit(1);
        }
    }
    else {
        port = 8080;
    }

    // create socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sock < 0) {
        perror("Creating socket failed: ");
        exit(1);
    }

    // allow fast reuse of ports
    int reuse_true = 1;
    setsockopt(server_sock,
               SOL_SOCKET,
               SO_REUSEADDR,
               &reuse_true,
               sizeof(reuse_true));

    struct sockaddr_in addr; // internet socket address data structure
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port); // byte order is significant
    addr.sin_addr.s_addr = INADDR_ANY; // listen to all interfaces

    int res = bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    if(res < 0) {
        perror("Error binding to port");
        exit(1);
    }

    struct sockaddr_in remote_addr;
    unsigned int socklen = sizeof(remote_addr);
    // wait for connections
    res = listen(server_sock,0);
    if(res < 0) {
        perror("Error listening for connection");
        exit(1);
    }

    pthread_t thread;
    while(1) {
        int* sockptr = (int*)malloc(sizeof(int));
        *sockptr = accept(server_sock, (struct sockaddr*)&remote_addr, &socklen);
        int sock = *sockptr;
        if(sock < 0) {
            perror("Error accepting connection");
        }
        else {
            // create a thread to handle requests from client side
            pthread_attr_t attr;
            pthread_attr_init( &attr );
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&thread, &attr, chat, (void*)sockptr);
        }
    }

    shutdown(server_sock,SHUT_RDWR);
}
