#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <sstream>
#include <algorithm>

using namespace std;

vector<string> chatMessages;
pthread_mutex_t messagesMutex = PTHREAD_MUTEX_INITIALIZER;

const char* htmlContent =  R"=====(<!DOCTYPE html>
<html>
<head>
    <title>Simple Chat</title>
    <script>
        function fetchMessages() {
            fetch('/messages')
                .then(response => response.json())
                .then(messages => {
                    const msgDiv = document.getElementById('messages');
                    msgDiv.innerHTML = messages.join('<br>');
                });
        }

        function sendMessage() {
            const input = document.getElementById('messageInput');
            const message = input.value;
            if(message.trim()) {
                input.value = '';
                fetch('/send', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'message=' + encodeURIComponent(message)
                }).then(fetchMessages);
            }
        }

        setInterval(fetchMessages, 1000);
        window.onload = fetchMessages;
    </script>
</head>
<body>
    <div id="messages" style="border: 1px solid #000; height: 300px; overflow: auto; margin-bottom: 10px;"></div>
    <input type="text" id="messageInput" placeholder="Type a message" style="width: 300px;">
    <button onclick="sendMessage()">Send</button>
</body>
</html>)=====";

void urlDecode(string &str) {
    string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '+') {
            result += ' ';
        } else if (str[i] == '%' && i + 2 < str.size()) {
            int hexValue;
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &hexValue);
            result += static_cast<char>(hexValue);
            i += 2;
        } else {
            result += str[i];
        }
    }
    str = result;
}

void handleRequest(int clientSocket) {
    char buffer[4096] = {0};
    read(clientSocket, buffer, sizeof(buffer)-1);

    string request(buffer);
    istringstream iss(request);
    string method, path, protocol;
    iss >> method >> path >> protocol;

    string response;

    if (method == "GET") {
        if (path == "/") {
            response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/html\r\n"
                       "Content-Length: " + to_string(strlen(htmlContent)) + "\r\n\r\n" + htmlContent;
        }
        else if (path == "/messages") {
            pthread_mutex_lock(&messagesMutex);
            string json = "[";
            for (size_t i = 0; i < chatMessages.size(); ++i) {
                string msg = chatMessages[i];
                replace(msg.begin(), msg.end(), '"', '\'');
                json += "\"" + msg + "\"" + (i < chatMessages.size()-1 ? "," : "");
            }
            json += "]";
            pthread_mutex_unlock(&messagesMutex);
            
            response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: " + to_string(json.size()) + "\r\n\r\n" + json;
        }
        else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
    }
    else if (method == "POST" && path == "/send") {
        size_t contentLength = 0;
        size_t headerEnd = request.find("\r\n\r\n");
        
        if (headerEnd != string::npos) {
            string header = request.substr(0, headerEnd);
            size_t clPos = header.find("Content-Length: ");
            if (clPos != string::npos) {
                contentLength = stoul(header.substr(clPos + 16));
            }
        }

        string postData = request.substr(headerEnd + 4, contentLength);
        size_t msgPos = postData.find("message=");
        
        if (msgPos != string::npos) {
            string message = postData.substr(msgPos + 8);
            urlDecode(message);
            
            if (!message.empty()) {
                pthread_mutex_lock(&messagesMutex);
                chatMessages.push_back(message);
                pthread_mutex_unlock(&messagesMutex);
            }
        }
        
        response = "HTTP/1.1 303 See Other\r\n"
                   "Location: /\r\n\r\n";
    }
    else {
        response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    }

    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);
    cout << "Server running on port 8080..." << endl;

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
        
        if (clientSocket >= 0) {
            pthread_t thread;
            pthread_create(&thread, nullptr, [](void* arg) -> void* {
                handleRequest(*(int*)arg);
                delete (int*)arg;
                return nullptr;
            }, new int(clientSocket));
            pthread_detach(thread);
        }
    }

    close(serverSocket);
    return 0;
}