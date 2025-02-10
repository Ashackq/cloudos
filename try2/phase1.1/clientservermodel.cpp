#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <csignal>

#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 4096 // Total memory size
#define PARTITION_SIZE 512      // Each client gets 512 bytes
#define MAX_CLIENTS (SHARED_MEMORY_SIZE / PARTITION_SIZE)

struct SharedMemoryMetadata
{
    int clients_connected;
    int used_size;
    int remaining_size;
    int client_ids[MAX_CLIENTS]; // Array to track connected clients
};

std::mutex mem_lock;
void *shared_memory_ptr;
SharedMemoryMetadata *metadata;

void cleanup(int signum)
{
    std::cout << "\n[Server] Interrupt received. Cleaning up shared memory..." << std::endl;
    munmap(shared_memory_ptr, SHARED_MEMORY_SIZE);
    shm_unlink(SHARED_MEMORY_NAME);
    exit(0);
}

void monitor_memory()
{
    while (true)
    {
        sleep(2);
        std::lock_guard<std::mutex> lock(mem_lock);
        std::cout << "[Server] Monitoring Shared Memory:\n";
        std::cout << "Total Size: " << SHARED_MEMORY_SIZE << " bytes\n";
        std::cout << "Used Size: " << metadata->used_size << " bytes\n";
        std::cout << "Remaining Size: " << metadata->remaining_size << " bytes\n";
        std::cout << "Clients Connected: " << metadata->clients_connected << "\n";
        std::cout << "Client IDs: ";
        for (int i = 0; i < metadata->clients_connected; i++)
        {
            std::cout << metadata->client_ids[i] << " ";
        }
        std::cout << "\n";
    }
}

void server()
{
    signal(SIGINT, cleanup);
    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "[Server] Error creating shared memory" << std::endl;
        return;
    }

    if (ftruncate(shm_fd, SHARED_MEMORY_SIZE) == -1)
    {
        std::cerr << "[Server] Error setting shared memory size" << std::endl;
        return;
    }

    shared_memory_ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_memory_ptr == MAP_FAILED)
    {
        std::cerr << "[Server] Error mapping shared memory" << std::endl;
        return;
    }

    metadata = static_cast<SharedMemoryMetadata *>(shared_memory_ptr);
    metadata->clients_connected = 0;
    metadata->used_size = 0;
    metadata->remaining_size = SHARED_MEMORY_SIZE - sizeof(SharedMemoryMetadata);
    memset(metadata->client_ids, -1, sizeof(metadata->client_ids));

    std::cout << "[Server] Initialized and monitoring shared memory..." << std::endl;

    std::thread monitor_thread(monitor_memory);
    monitor_thread.detach();

    std::cout << "Press Enter to clean up...";
    std::cin.get();

    munmap(shared_memory_ptr, SHARED_MEMORY_SIZE);
    shm_unlink(SHARED_MEMORY_NAME);
    std::cout << "[Server] Shared memory cleaned up." << std::endl;
}

void writer(int client_id, const std::string &message)
{
    std::lock_guard<std::mutex> lock(mem_lock);
    if (client_id < 0 || client_id >= MAX_CLIENTS)
    {
        std::cerr << "[Writer] Invalid client ID" << std::endl;
        return;
    }

    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "[Writer] Error opening shared memory" << std::endl;
        return;
    }

    shared_memory_ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_memory_ptr == MAP_FAILED)
    {
        std::cerr << "[Writer] Error mapping shared memory" << std::endl;
        return;
    }

    metadata = static_cast<SharedMemoryMetadata *>(shared_memory_ptr);

    bool already_present = false;
    for (int i = 0; i < metadata->clients_connected; i++)
    {
        if (metadata->client_ids[i] == client_id)
        {
            already_present = true;
            break;
        }
    }

    if (!already_present)
    {
        metadata->client_ids[metadata->clients_connected] = client_id;
        metadata->clients_connected++;

        char *partition_ptr = static_cast<char *>(shared_memory_ptr) + sizeof(SharedMemoryMetadata) + (client_id * PARTITION_SIZE);
        strncpy(partition_ptr, message.c_str(), PARTITION_SIZE - 1);

        metadata->used_size += PARTITION_SIZE;
        metadata->remaining_size = SHARED_MEMORY_SIZE - metadata->used_size - sizeof(SharedMemoryMetadata);
    }

    std::cout << "[Writer] Client " << client_id << " wrote: " << message << std::endl;

    munmap(shared_memory_ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}
void reader(int client_id)
{
    std::lock_guard<std::mutex> lock(mem_lock);
    if (client_id < 0 || client_id >= MAX_CLIENTS)
    {
        std::cerr << "[Reader] Invalid client ID" << std::endl;
        return;
    }

    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDONLY, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "[Reader] Error opening shared memory" << std::endl;
        return;
    }

    shared_memory_ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shared_memory_ptr == MAP_FAILED)
    {
        std::cerr << "[Reader] Error mapping shared memory" << std::endl;
        return;
    }

    metadata = static_cast<SharedMemoryMetadata *>(shared_memory_ptr);
    char *partition_ptr = static_cast<char *>(shared_memory_ptr) + sizeof(SharedMemoryMetadata) + (client_id * PARTITION_SIZE);
    std::cout << "[Reader] Client " << client_id << " read: " << partition_ptr << std::endl;

    munmap(shared_memory_ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}

void deregister_client(int client_id)
{
    std::lock_guard<std::mutex> lock(mem_lock);

    bool found = false;
    for (int i = 0; i < metadata->clients_connected; i++)
    {
        if (metadata->client_ids[i] == client_id)
        {
            found = true;
            // Shift remaining clients in the array
            for (int j = i; j < metadata->clients_connected - 1; j++)
            {
                metadata->client_ids[j] = metadata->client_ids[j + 1];
            }
            metadata->client_ids[metadata->clients_connected - 1] = -1;
            metadata->clients_connected--;

            // Free up partition space
            metadata->used_size -= PARTITION_SIZE;
            metadata->remaining_size += PARTITION_SIZE;

            std::cout << "[Server] Client " << client_id << " deregistered and memory freed.\n";
            break;
        }
    }

    if (!found)
    {
        std::cerr << "[Server] Client " << client_id << " not found.\n";
    }
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
    else if (mode == "deregister" && argc == 3)
    {
        int client_id = std::stoi(argv[2]);
        deregister_client(client_id);
    }

    else
    {
        std::cerr << "Invalid usage." << std::endl;
        return 1;
    }

    return 0;
}
