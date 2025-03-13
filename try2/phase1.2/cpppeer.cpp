#include <iostream>
#include <csignal>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <jsoncpp/json/json.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

int client_id = -1;
int client_socket = -1;  // Keep socket open for multiple requests

void sendToServer(const std::string &message)
{
    if (send(client_socket, message.c_str(), message.size(), 0) == -1)
    {
        std::cerr << "[Client] Failed to send data!" << std::endl;
    }
}

void receiveFromServer()
{
    char buffer[1024] = {0};
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        std::cout << "[Server] " << buffer << std::endl;
    }
    else
    {
        std::cerr << "[Client] Server closed the connection!" << std::endl;
    }
}

void registerClient()
{
    if (client_id != -1)
    {
        sendToServer("register " + std::to_string(client_id));
    }
    else
    {
        sendToServer("register");
    }

    char buffer[256] = {0};
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        if (std::string(buffer).find("Invalid") != std::string::npos)
        {
            std::cerr << "[Client] Invalid ID, requesting new one..." << std::endl;
            sendToServer("register");
            recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        }
        client_id = std::stoi(buffer);
        std::cout << "[Client] Registered with ID: " << client_id << std::endl;
    }
}


void requestPeerList()
{
    if (client_id == -1)
    {
        std::cerr << "[Client] You must register first!" << std::endl;
        return;
    }
    sendToServer("peerlist " + std::to_string(client_id));
    receiveFromServer();
}

void disconnectClient()
{
    if (client_id != -1)
    {
        sendToServer("disconnect " + std::to_string(client_id));
        std::cout << "[Client] Disconnected from server." << std::endl;
    }
    close(client_socket);
}

void signalHandler(int signum)
{
    std::cout << "\n[Client] Caught signal " << signum << ", disconnecting...\n";
    disconnectClient();
    exit(signum);
}

int main(int argc, char *argv[])
{
    int client_id = (argc == 2) ? std::stoi(argv[1]) : -1;
    
    signal(SIGINT, signalHandler); // Handle Ctrl+C

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        std::cerr << "[Client] Socket creation failed!" << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "[Client] Connection to server failed!" << std::endl;
        return 1;
    }

    std::cout << "[Client] Connected to server.\n";

    while (true)
    {
        std::cout << "\n[1] Register\n[2] Get Peer List\n[3] Exit\nChoice: ";
        int choice;
        std::cin >> choice;

        switch (choice)
        {
        case 1:
            registerClient();
            break;
        case 2:
            requestPeerList();
            break;
        case 3:
            disconnectClient();
            return 0;
        default:
            std::cout << "[Client] Invalid choice, try again." << std::endl;
        }
    }

    return 0;
}
