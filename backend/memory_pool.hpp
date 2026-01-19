#pragma once

#include <vector>
#include <cstddef>
#include <new>
#include <cstdint>

namespace orderbook {

// Lock-free memory pool for ultra-low latency allocation
// Pre-allocates memory blocks to avoid dynamic allocation during trading
template<typename T, size_t BlockSize = 4096>
class MemoryPool {
private:
    union Node {
        Node() : next(nullptr) {}
        ~Node() {}
        T data;
        Node* next;
    };
    
    struct Block {
        Node* nodes;
        size_t size;
        Block* next;
        
        explicit Block(size_t s) : nodes(static_cast<Node*>(::operator new(s * sizeof(Node)))), size(s), next(nullptr) {
            // Initialize all nodes in placement
            for (size_t i = 0; i < size; ++i) {
                new (&nodes[i]) Node();
            }
        }
        
        ~Block() {
            // Destroy all nodes
            for (size_t i = 0; i < size; ++i) {
                nodes[i].~Node();
            }
            ::operator delete(nodes);
        }
    };
    
    Block* blocks_;
    Node* free_list_;
    size_t block_size_;
    
    void allocate_block() {
        Block* new_block = new Block(block_size_);
        new_block->next = blocks_;
        blocks_ = new_block;
        
        // Link all nodes in the new block to free list
        for (size_t i = 0; i < block_size_ - 1; ++i) {
            new_block->nodes[i].next = &new_block->nodes[i + 1];
        }
        new_block->nodes[block_size_ - 1].next = free_list_;
        free_list_ = new_block->nodes;
    }
    
public:
    explicit MemoryPool(size_t initial_blocks = 1)
        : blocks_(nullptr), free_list_(nullptr), block_size_(BlockSize) {
        for (size_t i = 0; i < initial_blocks; ++i) {
            allocate_block();
        }
    }
    
    ~MemoryPool() {
        while (blocks_) {
            Block* next = blocks_->next;
            delete blocks_;
            blocks_ = next;
        }
    }
    
    // Disable copy
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // Allocate an object
    template<typename... Args>
    T* allocate(Args&&... args) {
        if (!free_list_) {
            allocate_block();
        }
        
        Node* node = free_list_;
        free_list_ = node->next;
        
        // Construct object in-place
        return new (&node->data) T(std::forward<Args>(args)...);
    }
    
    // Deallocate an object
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        // Call destructor
        ptr->~T();
        
        // Add back to free list
        Node* node = reinterpret_cast<Node*>(ptr);
        node->next = free_list_;
        free_list_ = node;
    }
};

} // namespace orderbook
