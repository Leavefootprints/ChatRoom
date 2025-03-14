#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 18080
#define MAX_CLIENTS 5
#define MAX_MSGS 100

typedef struct {
    char username[32];
    char message[256];
} Message;

Message messages[MAX_MSGS];
int msg_count = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* handle_client(void* socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);

    char buffer[1024] = {0};
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(sock);
        pthread_exit(NULL);
    }
    buffer[bytes_read] = '\0';

    char* method = strtok(buffer, " \n");
    char* path = strtok(NULL, " \n");

    if (method && path) {
        if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
            const char* html = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<!DOCTYPE html>"
                "<html>"
                "<head><title>Chat</title></head>"
                "<body>"
                "<div id='messages'></div>"
                "<input type='text' id='message' placeholder='Type message...'>"
                "<button onclick='sendMessage()'>Send</button>"
                "<script>"
                "function updateMessages() {"
                "  fetch('/messages').then(r => r.json()).then(data => {"
                "    const messagesDiv = document.getElementById('messages');"
                "    messagesDiv.innerHTML = '';"
                "    data.forEach(msg => {"
                "      messagesDiv.innerHTML += `<div>${msg.username}: ${msg.message}</div>`;"
                "    });"
                "  });"
                "}"
                "setInterval(updateMessages, 1000);"
                "function sendMessage() {"
                "  const msg = document.getElementById('message').value;"
                "  fetch('/send', {method: 'POST', body: msg})"
                "    .then(() => document.getElementById('message').value = '');"
                "}"
                "updateMessages();"
                "</script>"
                "</body>"
                "</html>";

            write(sock, html, strlen(html));
        } else if (strcmp(method, "POST") == 0 && strcmp(path, "/send") == 0) {
            char* content_length_str = strstr(buffer, "Content-Length:");
            if (content_length_str) {
                char* colon = strchr(content_length_str, ':');
                if (colon) {
                    colon += 2; // 跳过冒号和空格
                    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                    int content_length = atoi(colon);
                    if (content_length > 255) content_length = 255;

                    char message_data[256];
                    ssize_t bytes_read = read(sock, message_data, content_length);
                    if (bytes_read == -1) {
                        const char* error_response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                        write(sock, error_response, strlen(error_response));
                        goto cleanup;
                    }
                    message_data[bytes_read] = '\0';

                    pthread_mutex_lock(&mutex);
                    if (msg_count < MAX_MSGS) {
                        strcpy(messages[msg_count].username, "User");
                        strncpy(messages[msg_count].message, message_data, sizeof(messages[msg_count].message) - 1);
                        messages[msg_count].message[sizeof(messages[msg_count].message) - 1] = '\0';
                        msg_count++;
                    }
                    pthread_mutex_unlock(&mutex);

                    const char* response = "HTTP/1.1 200 OK\r\n\r\n";
                    write(sock, response, strlen(response));
                }
            } else {
                const char* error_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                write(sock, error_response, strlen(error_response));
            }
        } else if (strcmp(path, "/messages") == 0) {
            pthread_mutex_lock(&mutex);
            char json[2048] = "{ \"messages\": [";
            for (int i = 0; i < msg_count; i++) {
                char temp[512];
                int len = snprintf(temp, sizeof(temp), 
                    "{\"username\":\"%s\", \"message\":\"%s\"}", 
                    messages[i].username, messages[i].message);
                if (len >= 0 && len < sizeof(temp)) {
                    strcat(json, temp);
                    if (i < msg_count - 1) strcat(json, ",");
                }
            }
            if (msg_count > 0) strcat(json, "]");
            else strcat(json, "[]");
            strcat(json, "}");
            pthread_mutex_unlock(&mutex);

            const char* response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n\r\n";
            write(sock, response, strlen(response));
            write(sock, json, strlen(json));
        }
    }

cleanup:
    close(sock);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while(1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        int* client_socket = (int*)malloc(sizeof(int));
        *client_socket = new_socket;

        // 声明 thread_id
        pthread_t thread_id;

        if (pthread_create(&thread_id, NULL, handle_client, (void*)client_socket) != 0) {
            perror("pthread_create");
            free(client_socket);
            close(new_socket);
        }
        pthread_detach(thread_id);
    }

    return 0;
}