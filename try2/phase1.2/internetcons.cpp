#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <mutex>
#include <arpa/inet.h>
#include <thread>

#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 4096 // Increased size for partitions
#define PARTITION_SIZE 512      // Each client gets a fixed partition
#define SERVER_PORT 8080

std::mutex mem_lock;
std::map<int, int> client_partitions; // Map client ID to partition index

void handle_client(int client_sock)
{
    char buffer[256];
    recv(client_sock, buffer, 256, 0);
    std::string command(buffer);

    if (command == "register")
    {
        std::lock_guard<std::mutex> lock(mem_lock);
        int client_id = client_partitions.size() + 1;
        int partition_index = client_id % (SHARED_MEMORY_SIZE / PARTITION_SIZE);
        client_partitions[client_id] = partition_index;
        send(client_sock, &client_id, sizeof(client_id), 0);
    }
    close(client_sock);
}

void server()
{
    int server_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    std::cout << "Server initialized, listening on port " << SERVER_PORT << std::endl;
    while (true)
    {
        client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        std::thread(handle_client, client_sock).detach();
    }
}

void writer(int client_id, const std::string &message)
{
    std::lock_guard<std::mutex> lock(mem_lock);
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
    std::cout << "Client " << client_id << " wrote: " << message << std::endl;

    munmap(ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}

void reader(int client_id)
{
    std::lock_guard<std::mutex> lock(mem_lock);
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
    std::cout << "Client " << client_id << " read: " << partition_ptr << std::endl;

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
