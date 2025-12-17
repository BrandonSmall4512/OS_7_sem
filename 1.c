#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE   (1024 * 4)  
#define BLOCK_SIZE  64
#define BLOCK_COUNT (POOL_SIZE / BLOCK_SIZE)

static unsigned char *memory_pool = NULL;
static char block_owner[BLOCK_COUNT]; 
static void* ptr_to_block[BLOCK_COUNT];

void init_memory() {
    memory_pool = (unsigned char*)malloc(POOL_SIZE);
    if (!memory_pool) {
        printf("Error: Failed to allocate memory pool from OS\n");
        exit(1);
    }
    memset(memory_pool, 0, POOL_SIZE);
    memset(block_owner, 0, BLOCK_COUNT);
    memset(ptr_to_block, 0, sizeof(ptr_to_block));
    printf("Memory pool allocated from OS: %d blocks of %d bytes (total %d KB)\n",
           BLOCK_COUNT, BLOCK_SIZE, POOL_SIZE / 1024);
}

void cleanup_memory() {
    free(memory_pool);
    memory_pool = NULL;
    printf("Memory returned to OS.\n");
}

void* my_malloc(size_t size, char process_id) {
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    int consecutive_free = 0;
    int start_index = -1;

    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (block_owner[i] == 0) {
            if (consecutive_free == 0) start_index = i;
            consecutive_free++;
            if (consecutive_free >= blocks_needed) {
                for (int j = start_index; j < start_index + blocks_needed; j++) {
                    block_owner[j] = process_id;
                }
                
                void* data_ptr = &memory_pool[start_index * BLOCK_SIZE];
                ptr_to_block[start_index] = data_ptr;
                
                printf("Allocated %zu bytes (%d blocks) for process %c at blocks %d-%d\n",
                       size, blocks_needed, process_id, start_index, start_index + blocks_needed - 1);
                return data_ptr;
            }
        } else {
            consecutive_free = 0;
        }
    }

    printf("Allocation failed for process %c: not enough contiguous blocks\n", process_id);
    return NULL;
}

void my_free(void* ptr) {
    if (!ptr) return;

    int start_index = -1;
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (ptr_to_block[i] == ptr) {
            start_index = i;
            break;
        }
    }
    
    if (start_index == -1) {
        printf("Error: Pointer not found\n");
        return;
    }

    char pid = block_owner[start_index];
    int blocks_freed = 0;
    
    for (int i = start_index; i < BLOCK_COUNT && block_owner[i] == pid; i++) {
        block_owner[i] = 0;
        ptr_to_block[i] = NULL;
        blocks_freed++;
    }

    printf("Freed %d blocks of process %c starting at block %d\n", 
           blocks_freed, pid, start_index);
}

void compact_memory() {
    printf("\nCompacting memory...\n");

    int target_index = 0;
    int moves = 0;

    for (int i = 0; i < BLOCK_COUNT; ) {
        if (block_owner[i] != 0) {
            char pid = block_owner[i];
            
            int block_count = 0;
            for (int j = i; j < BLOCK_COUNT && block_owner[j] == pid; j++) {
                block_count++;
            }

            if (i != target_index) {
                memmove(&memory_pool[target_index * BLOCK_SIZE],
                        &memory_pool[i * BLOCK_SIZE],
                        block_count * BLOCK_SIZE);
                
                for (int j = 0; j < block_count; j++) {
                    block_owner[target_index + j] = pid;
                    block_owner[i + j] = 0;
                    ptr_to_block[target_index + j] = &memory_pool[(target_index + j) * BLOCK_SIZE];
                    ptr_to_block[i + j] = NULL;
                }
                
                moves++;
                printf("Moved process %c from blocks %d-%d to %d-%d\n", 
                       pid, i, i + block_count - 1, target_index, target_index + block_count - 1);
            }
            
            target_index += block_count;
            i += block_count;
        } else {
            i++;
        }
    }

    printf("Compaction complete. Moved %d processes\n", moves);
}

void print_memory_map() {
    printf("\nMemory map:\n");
    for (int i = 0; i < BLOCK_COUNT; i++) {
        printf("%c", block_owner[i] ? block_owner[i] : '.');
        if ((i + 1) % 64 == 0) printf("\n");
    }
    printf("\n");
}

void find_process_blocks(char pid) {
    printf("Process %c occupies blocks: ", pid);
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (block_owner[i] == pid) {
            int start = i;
            while (i < BLOCK_COUNT && block_owner[i] == pid) i++;
            printf("%d-%d ", start, i-1);
        }
    }
    printf("\n");
}

int main() {
    init_memory();

    printf("=== Initial allocation ===\n");
    void* proc1 = my_malloc(200, '1');
    void* proc2 = my_malloc(350, '2');
    void* proc3 = my_malloc(400, '3');
    
    print_memory_map();

    printf("\n=== Freeing process 2 ===\n");
    my_free(proc2);
    print_memory_map();

    printf("\n=== Allocating process 4 ===\n");
    void* proc4 = my_malloc(128, '4');
    print_memory_map();

    printf("\n=== Before compaction ===\n");
    find_process_blocks('1');
    find_process_blocks('3');
    find_process_blocks('4');
    
    printf("\n=== After compaction ===\n");
    compact_memory();
    print_memory_map();
    
    find_process_blocks('1');
    find_process_blocks('3');
    find_process_blocks('4');

    cleanup_memory();
    return 0;
}