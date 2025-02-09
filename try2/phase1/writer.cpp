#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#define SHARED_MEMORY_NAME "p2p_shared_memory"
#define SHARED_MEMORY_SIZE 1024

int main()
{
    // Create shared memory object
    int shm_fd = shm_open(SHARED_MEMORY_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        std::cerr << "Error creating shared memory" << std::endl;
        return 1;
    }

    // Set size of shared memory
    if (ftruncate(shm_fd, SHARED_MEMORY_SIZE) == -1)
    {
        std::cerr << "Error setting shared memory size" << std::endl;
        return 1;
    }

    // Map shared memory to process address space
    void *ptr = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::cerr << "Error mapping shared memory" << std::endl;
        return 1;
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

    return 0;
}
