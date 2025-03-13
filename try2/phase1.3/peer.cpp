#include <iostream>
#include <csignal>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <map>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define PROVIDER_PORT 9090  // Default provider port

int client_id = -1;
int client_socket = -1;
std::map<int, std::string> peer_memory; // Simulated shared memory for each gainer

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

// Register as a client with the server
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
        std::string response(buffer);

        if (response.find("Invalid") != std::string::npos)
        {
            std::cerr << "[Client] Invalid ID, requesting new one..." << std::endl;
            sendToServer("register");
            recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            buffer[bytes_received] = '\0';
            client_id = std::stoi(buffer);
            std::cout << "[Client] Registered with ID: " << client_id << std::endl;
            return;
        }

        if (response.find("Welcome") != std::string::npos)
        {
            std::cout << "[Client] Welcome Back From Server: " << buffer << std::endl;
        }
        else
        {
            client_id = std::stoi(buffer);
            std::cout << "[Client] Registered with ID: " << client_id << std::endl;
        }
        std::cout << "[Client] Ready to connect with peers!" << std::endl;
    }
}

// Connect to a peer provider and read/write data
void connectToProvider(const std::string &ip, int port)
{
    int peer_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_socket < 0)
    {
        std::cerr << "[Gainer] Failed to create socket!" << std::endl;
        return;
    }

    struct sockaddr_in peer_addr;
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &peer_addr.sin_addr);

    if (connect(peer_socket, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0)
    {
        std::cerr << "[Gainer] Failed to connect to provider!" << std::endl;
        return;
    }

    std::cout << "[Gainer] Connected to provider at " << ip << ":" << port << std::endl;

    while (true)
    {
        std::cout << "\n[1] Read from Provider\n[2] Write to Provider\n[3] Disconnect\nChoice: ";
        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 1)
        {
            send(peer_socket, "READ", 4, 0);
            char buffer[256] = {0};
            recv(peer_socket, buffer, sizeof(buffer) - 1, 0);
            std::cout << "[Provider] Data: " << buffer << std::endl;
        }
        else if (choice == 2)
        {
            std::string data;
            std::cout << "Enter data to write: ";
            std::getline(std::cin, data);
            send(peer_socket, ("WRITE " + data).c_str(), data.size() + 6, 0);
        }
        else
        {
            send(peer_socket, "EXIT", 4, 0);
            close(peer_socket);
            std::cout << "[Gainer] Disconnected from provider.\n";
            break;
        }
    }
}

// Request provider list from the server and connect to one
void requestPeerList()
{
    if (client_id == -1)
    {
        std::cerr << "[Client] You must register first!" << std::endl;
        return;
    }
    sendToServer("peerlist " + std::to_string(client_id));

    char buffer[256] = {0};
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        std::string response(buffer);

        if (response == "No providers available")
        {
            std::cout << "[Server] No providers available at the moment.\n";
            return;
        }

        std::string ip = response.substr(0, response.find(':'));
        int port = std::stoi(response.substr(response.find(':') + 1));

        connectToProvider(ip, port);
    }
}

// Provider function to handle a single gainer
void handleGainer(int gainer_socket)
{
    char buffer[256] = {0};
    int bytes_received;

    while ((bytes_received = recv(gainer_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';
        std::string command(buffer);

        if (command == "READ")
        {
            send(gainer_socket, peer_memory[client_id].c_str(), peer_memory[client_id].size(), 0);
        }
        else if (command.find("WRITE ") == 0)
        {
            peer_memory[client_id] = command.substr(6);
            std::cout << "[Provider] Data updated: " << peer_memory[client_id] << std::endl;
        }
        else if (command == "EXIT")
        {
            std::cout << "[Provider] Gainer disconnected.\n";
            close(gainer_socket);
            return;
        }
    }
}

// Provider function to accept gainer connections
void startProvider()
{
    if (client_id == -1)
    {
        std::cerr << "[Client] You must register first!" << std::endl;
        return;
    }

    sendToServer("register provider " + std::to_string(PROVIDER_PORT));

    int provider_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in provider_addr;
    provider_addr.sin_family = AF_INET;
    provider_addr.sin_port = htons(PROVIDER_PORT);
    provider_addr.sin_addr.s_addr = INADDR_ANY;

    bind(provider_socket, (struct sockaddr *)&provider_addr, sizeof(provider_addr));
    listen(provider_socket, 5);

    std::cout << "[Provider] Waiting for gainers on port " << PROVIDER_PORT << "...\n";

    while (true)
    {
        int gainer_socket = accept(provider_socket, nullptr, nullptr);
        std::thread(handleGainer, gainer_socket).detach();
    }
}

void disconnectClient()
{
    sendToServer("disconnect " + std::to_string(client_id));
    close(client_socket);
}

int main()
{
    signal(SIGINT, [](int) { disconnectClient(); exit(0); });

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));

    while (true)
    {
        std::cout << "\n[1] Register\n[2] Gain from Peers\n[3] Provide to Peers\n[4] Exit\nChoice: ";
        int choice;
        std::cin >> choice;

        if (choice == 1) registerClient();
        else if (choice == 2) requestPeerList();
        else if (choice == 3) startProvider();
        else break;
    }

    disconnectClient();
    return 0;
}
