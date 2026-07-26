#pragma once
#include <cstddef>
#include <cstdint>

class FreeList{
private:
    struct Node{
        Node* next = nullptr;
    };
    
public:
    static constexpr std::size_t kNodeSize = sizeof(Node);

    void push(void* block) noexcept {
        auto* node = static_cast<Node*>(block);
        node->next = head_;
        head_ = node;
        ++count_;
    }

    void pushBatch(void** in, std::size_t count) noexcept {
        for(std::size_t i = 0; i < count; ++i){
            push(in[i]);
        }
    }

    void* pop() noexcept {
        if(!head_) return nullptr;
        Node* node = head_;
        head_ = node->next;
        --count_;
        return static_cast<void*>(node);
    }

    std::size_t popBatch(void** out, std::size_t maxCount) noexcept {
        std::size_t count = 0;
        while(count < maxCount && head_){
            out[count] = pop();
            ++count;
        }
        return count;
    }

    bool empty() const noexcept { return (head_ == nullptr); }

    std::size_t count() const noexcept { return count_; }

    void reset() noexcept {
        head_ = nullptr;
        count_ = 0;
    }

private:
    Node* head_ = nullptr;
    std::size_t count_ = 0;
};
