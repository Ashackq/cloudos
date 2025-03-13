#include <iostream>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <arpa/inet.h>
#include <thread>
#include <jsoncpp/json/json.h>
#include <algorithm>  // Required for std::all_of
#include <cctype>  

#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 4096
#define PARTITION_SIZE 512
#define SERVER_PORT 8080

std::mutex mem_lock;
std::shared_mutex client_map_lock;
std::map<int, std::string> client_data; // Stores ID -> IP mapping
std::map<int, std::string> provider_data; // Stores ID -> {IP,port} mapping
std::map<int, int> client_partitions;   // Stores ID -> Partition

void logMessage(const std::string &message)
{
    std::ofstream logFile("server.log", std::ios::app);
    if (logFile)
    {
        logFile << message << std::endl;
    }
}

// Load existing clients from a file
void loadClientData()
{
    std::ifstream file("clients.json");
    if (!file.is_open()) return;

    Json::Value root;
    file >> root;
    
    for (const auto &id : root.getMemberNames())
    {
        int client_id = std::stoi(id);
        client_data[client_id] = root[id].asString();

        // Recalculate partition assignment
        // int partition_index = client_id % (SHARED_MEMORY_SIZE / PARTITION_SIZE);
        // client_partitions[client_id] = partition_index;
    }
}


// Save clients to a file
void saveClientData()
{
    Json::Value root;
    for (const auto &[id, ip] : client_data)
    {
        root[std::to_string(id)] = ip;
    }

    std::ofstream file("clients.json");
    file << root;
}

// Remove inactive clients by pinging them
void cleanInactiveClients()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        std::lock_guard<std::shared_mutex> lock(client_map_lock);
        for (auto it = client_data.begin(); it != client_data.end();)
        {
            std::string command = "ping -c 1 " + it->second + " > /dev/null 2>&1";
            if (system(command.c_str()) != 0) // If unreachable, remove
            {
                logMessage("[Server] Removing inactive client: " + std::to_string(it->first));
                client_partitions.erase(it->first);
                it = client_data.erase(it);
            }
            else
            {
                ++it;
            }
        }
        saveClientData();
    }
}

void handle_client(int client_sock)
{
    struct sockaddr_in client_addr;
    socklen_t addr_size = sizeof(client_addr);
    getpeername(client_sock, (struct sockaddr *)&client_addr, &addr_size);
    std::string client_ip = inet_ntoa(client_addr.sin_addr);

    char buffer[256] = {0};
    int client_id = -1;

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0)
        {
            logMessage("[Server] Client unexpectedly disconnected.");
            close(client_sock);
            return;
        }

        buffer[bytes_received] = '\0';
        std::string command(buffer);
        if (command.find("register provider") == 0)
        {
            std::string provider_port_str = command.substr(18); // Extract the port number
            if (!provider_port_str.empty() && std::all_of(provider_port_str.begin(), provider_port_str.end(), ::isdigit))
            {
                int provider_port = std::stoi(provider_port_str);
                logMessage("[Server] Registered provider at " + client_ip + ":" + std::to_string(provider_port));
                
                // Store provider info in client_data using a negative ID to distinguish from normal clients
                int provider_id = (provider_data.size() + 1);
                provider_data[client_id] = client_ip +":"+ std::to_string(provider_port);
                // saveClientData();

                send(client_sock, "Provider registered successfully", 32, 0);
            }
            else
            {
                send(client_sock, "Invalid provider registration", 29, 0);
            }
        }
        else if (command.find("register") == 0)
        {
            if (command.size() > 9)  // Ensure the string is long enough
            {
                std::string requested_id = command.substr(9);
                if (!requested_id.empty() && std::all_of(requested_id.begin(), requested_id.end(), ::isdigit))
                    {
                    int client_id = std::stoi(requested_id);
                    if (client_data.count(client_id) && client_data[client_id] == client_ip)
                    {
                        logMessage("[Server] Welcome back Client " + std::to_string(client_id));
                        int partition_index = client_id % (SHARED_MEMORY_SIZE / PARTITION_SIZE);
                        client_partitions[client_id] = partition_index;
                        
                        for (const auto &[id, partition] : client_partitions) {
                            std::cerr << "[" << id << " -> " << partition << "] ";
                        }
                        std::cerr << std::endl;
                        send(client_sock, ("Welcome back! Your ID: " + std::to_string(client_id)).c_str(), 50, 0);
                    }
                    else
                    {
                        logMessage("[Server] Invalid ID request.");
                        send(client_sock, "Invalid ID!", 11, 0);
                        continue;
                    }
                }
               
            }
            
            else
            {
                std::lock_guard<std::mutex> lock(mem_lock);
                client_id = client_data.size() + 1;
                int partition_index = client_id % (SHARED_MEMORY_SIZE / PARTITION_SIZE);
                client_partitions[client_id] = partition_index;
                client_data[client_id] = client_ip;
                saveClientData();

                logMessage("[Server] Assigned Client ID: " + std::to_string(client_id));
                send(client_sock, std::to_string(client_id).c_str(), 50, 0);
            }
        }
        else if (command.find("peerlist") == 0)
        {
            if (command.size() > 9)  // Ensure the string is long enough
            {
                std::string requested_id = command.substr(9);
                int client_id = std::stoi(requested_id);
                if (client_id == -1)
                {
                    logMessage("[Server] Unregistered client requested peer list.");
                    std::string mess = "Seems like you are unregistered";
                    send(client_sock,mess.c_str() , mess.size(), 0);
                    continue;
                }
                else
                {
                    if (provider_data.empty())
                    {
                        send(client_sock, "No providers available", 23, 0);
                    }
                    else
                    {
                        auto first_provider = provider_data.begin();
                        int first_id = first_provider->first;
                        std::string first_value = first_provider->second;
                        
                        std::cout << "First Provider ID: " << first_id << "\n";
                        std::cout << "First Provider Data: " << first_value << "\n";
                        std::string response =  provider_data[first_id];
                        send(client_sock, response.c_str(), response.size(), 0);
                    }

                }
            }
            else {
                logMessage("[Server] Invalid ID request.");
                send(client_sock, "Invalid ID!", 11, 0);
                continue;
            }
        }
        else if (command.find("connect") == 0)
        {
            std::string target_id_str = command.substr(8);
            if (!target_id_str.empty() && std::all_of(target_id_str.begin(), target_id_str.end(), ::isdigit))
            {
                int target_id = std::stoi(target_id_str);
                std::shared_lock<std::shared_mutex> lock(client_map_lock);
                if (client_data.count(target_id))
                {
                    std::string target_ip = client_data[target_id];
                    std::string response = "Connect to: " + target_ip + ":" + std::to_string(SERVER_PORT);
                    send(client_sock, response.c_str(), response.size(), 0);
                }
                else
                {
                    std::string error_msg = "Client ID not found.";
                    send(client_sock, error_msg.c_str(), error_msg.size(), 0);
                }
            }
            else
            {
                std::string error_msg = "Invalid client ID format.";
                send(client_sock, error_msg.c_str(), error_msg.size(), 0);
            }
        }
        else if (command.find("disconnect") == 0)
        {
            std::string requested_id = command.substr(11);
            int client_id = std::stoi(requested_id);
            logMessage("[Server] Client " + std::to_string(client_id) + " disconnected.");
            std::lock_guard<std::shared_mutex> lock(client_map_lock);
            client_partitions.erase(client_id);
            saveClientData();
            close(client_sock);
            return;
        }
    }
}

void server()
{
    loadClientData();
    std::thread(cleanInactiveClients).detach();

    int server_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    logMessage("[Server] Initialized, listening on port " + std::to_string(SERVER_PORT));

    while (true)
    {
        client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_sock >= 0)
        {
            std::thread(handle_client, client_sock).detach();
        }
    }
}


int main()
{
    server();
    return 0;
}
