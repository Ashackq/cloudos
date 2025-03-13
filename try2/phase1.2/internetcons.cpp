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

#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 4096
#define PARTITION_SIZE 512
#define SERVER_PORT 8080

std::mutex mem_lock;
std::shared_mutex client_map_lock;
std::map<int, std::string> client_data; // Stores ID -> IP mapping
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

        if (command.find("register") == 0)
        {
            std::string requested_id = command.substr(9);
            if (!requested_id.empty())
            {
                client_id = std::stoi(requested_id);
                if (client_data.count(client_id) && client_data[client_id] == client_ip)
                {
                    logMessage("[Server] Welcome back Client " + std::to_string(client_id));
                    send(client_sock, ("Welcome back! Your ID: " + std::to_string(client_id)).c_str(), 50, 0);
                }
                else
                {
                    logMessage("[Server] Invalid ID request.");
                    send(client_sock, "Invalid ID!", 11, 0);
                    continue;
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
            if (client_id == -1)
            {
                logMessage("[Server] Unregistered client requested peer list.");
                continue;
            }

            std::string peer_list = "[";
            std::shared_lock<std::shared_mutex> lock(client_map_lock);
            for (const auto &[id, _] : client_data)
            {
                if (id != client_id)
                    peer_list += std::to_string(id) + ",";
            }
            if (peer_list.back() == ',')
                peer_list.pop_back();
            peer_list += "]";

            send(client_sock, peer_list.c_str(), peer_list.size(), 0);
        }
        else if (command.find("disconnect") == 0)
        {
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

void writer(int client_id, const std::string &message)
{
    std::shared_lock<std::shared_mutex> lock(client_map_lock);
    if (client_partitions.find(client_id) == client_partitions.end())
    {
        std::cerr << "Invalid client ID." << std::endl;
        return;
    }
    
    int partition_index = client_partitions[client_id];

    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "Error opening shared memory" << std::endl;
        return;
    }

    void *ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::cerr << "Error mapping shared memory" << std::endl;
        return;
    }

    char *partition_ptr = static_cast<char *>(ptr) + (partition_index * PARTITION_SIZE);
    strncpy(partition_ptr, message.c_str(), PARTITION_SIZE - 1);
    
    logMessage("[Writer] Client " + std::to_string(client_id) + " wrote: " + message);

    munmap(ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}

void reader(int client_id)
{
    std::shared_lock<std::shared_mutex> lock(client_map_lock);
    if (client_partitions.find(client_id) == client_partitions.end())
    {
        std::cerr << "Invalid client ID." << std::endl;
        return;
    }

    int partition_index = client_partitions[client_id];

    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDONLY, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "Error opening shared memory" << std::endl;
        return;
    }

    void *ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::cerr << "Error mapping shared memory" << std::endl;
        return;
    }

    char *partition_ptr = static_cast<char *>(ptr) + (partition_index * PARTITION_SIZE);
    std::cout << "[Reader] Client " << client_id << " read: " << partition_ptr << std::endl;

    logMessage("[Reader] Client " + std::to_string(client_id) + " read: " + partition_ptr);

    munmap(ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <server|writer|reader> [client_id] [message]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "server")
    {
        server();
    }
    else if (mode == "writer" && argc == 4)
    {
        int client_id = std::stoi(argv[2]);
        std::string message = argv[3];
        writer(client_id, message);
    }
    else if (mode == "reader" && argc == 3)
    {
        int client_id = std::stoi(argv[2]);
        reader(client_id);
    }
    else
    {
        std::cerr << "Invalid usage." << std::endl;
        return 1;
    }

    return 0;
}
