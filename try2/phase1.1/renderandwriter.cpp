#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
// Program for both reader and writer
#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 1024

void writer()
{
    // Create shared memory object
    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "Error creating shared memory" << std::endl;
        return;
    }

    // Set size of shared memory
    if (ftruncate(shm_fd, SHARED_MEMORY_SIZE) == -1)
    {
        std::cerr << "Error setting shared memory size" << std::endl;
        return;
    }

    // Map shared memory to process address space
    void *ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::cerr << "Error mapping shared memory" << std::endl;
        return;
    }

    // Write data to shared memory
    const char *message = "Hello from the shared memory!";
    memcpy(ptr, message, strlen(message) + 1);
    std::cout << "Data written to shared memory: " << message << std::endl;

    std::cout << "Press Enter to clean up...";
    std::cin.get();

    // Clean up
    munmap(ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
    shm_unlink(SHARED_MEMORY_NAME);
}

void reader()
{
    // Open existing shared memory object
    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDONLY, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "Error opening shared memory" << std::endl;
        return;
    }

    // Map shared memory to process address space
    void *ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::cerr << "Error mapping shared memory" << std::endl;
        return;
    }

    // Read data from shared memory
    std::cout << "Data read from shared memory: " << static_cast<char *>(ptr) << std::endl;

    // Clean up
    munmap(ptr, SHARED_MEMORY_SIZE);
    close(shm_fd);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <writer|reader>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "writer")
    {
        writer();
    }
    else if (mode == "reader")
    {
        reader();
    }
    else
    {
        std::cerr << "Invalid mode. Use 'writer' or 'reader'." << std::endl;
        return 1;
    }

    return 0;
}
