#pragma once
#include <mutex>
#include <list>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <unordered_map>
#include "core/common/memory/chunk.hpp"

class Slab{
public:
    Slab() = default;
    ~Slab() = default;

    Slab(const Slab&) = delete;
    Slab& operator=(const Slab&) = delete;
    Slab(Slab&&) = delete;
    Slab& operator=(Slab&&) = delete;

    void init(std::size_t blockSize){
        assert(blockSize != 0);
        blockSize_ = blockSize;
    }

    void* allocate(){
        std::lock_guard<std::mutex> guard(mutex_);
        Chunk* chunk = partialChunks_.empty() ? addChunk() : partialChunks_.front();
        ChunkState before = chunk->state();
        void* ptr = chunk->allocate();
        assert(ptr != nullptr);
        ++allocatedBlocks_;
        transitionState(before, chunk);
        return ptr;
    }

    void deallocate(void* ptr){
        assert(ptr != nullptr);
        std::lock_guard<std::mutex> guard(mutex_);
        Chunk* chunk = findChunk(ptr);
        if(!chunk) return;
        ChunkState before = chunk->state();
        chunk->deallocate(ptr);
        --allocatedBlocks_;
        transitionState(before, chunk);
    }

    std::size_t fetchBatch(void** out, std::size_t batchSize){
        assert(out != nullptr);
        assert(batchSize > 0);
        std::lock_guard<std::mutex> guard(mutex_);
        std::size_t fetched = 0;
        for(auto it = partialChunks_.begin(); it != partialChunks_.end() && fetched < batchSize;){
            Chunk* chunk = *it++;
            ChunkState before = chunk->state();
            fetched += chunk->allocateBatch(out + fetched, batchSize - fetched);
            transitionState(before, chunk);
        }
        while(fetched < batchSize){
            Chunk* chunk = addChunk();
            ChunkState before = chunk->state();
            fetched += chunk->allocateBatch(out + fetched, batchSize - fetched);
            transitionState(before, chunk);
            if(chunk->full()) break;
        }
        allocatedBlocks_ += fetched;
        return fetched;
    }

    std::size_t returnBatch(void** in, std::size_t count){
        assert(in != nullptr);
        std::lock_guard<std::mutex> guard(mutex_);
        std::size_t returned = 0;
        for(std::size_t i = 0; i < count; ++i){
            Chunk* chunk = findChunk(in[i]);
            assert(chunk != nullptr);
            ChunkState before = chunk->state();
            chunk->deallocate(in[i]);
            transitionState(before, chunk);
            ++returned;
        }
        allocatedBlocks_ -= returned;
        return returned;
    }

    std::size_t blockSize() const noexcept{ return blockSize_; }
    std::size_t allocatedBlocks() const noexcept{ return allocatedBlocks_; }

private:
    static constexpr std::size_t kChunkSize = 4096;
    static_assert((kChunkSize & (kChunkSize - 1)) == 0, "Chunk size must be power of two");

    Chunk* addChunk(){
        auto chunk = std::make_unique<Chunk>(blockSize_);
        Chunk* raw = chunk.get();
        chunkMap_.emplace(reinterpret_cast<std::uintptr_t>(raw->begin()), raw);
        chunks_.push_back(std::move(chunk));
        return raw;
    }

    Chunk* findChunk(void* ptr){
        auto it = chunkMap_.find(reinterpret_cast<std::uintptr_t>(ptr) & ~(kChunkSize - 1));
        return (it != chunkMap_.end()) ? it->second : nullptr;
    }

    void transitionState(ChunkState before, Chunk* chunk){
        ChunkState after = chunk->state();
        if(before == after) return;
        if(before == ChunkState::Partial) partialChunks_.remove(chunk);
        if(after == ChunkState::Partial) partialChunks_.push_back(chunk);
    }

    std::mutex mutex_;
    std::size_t blockSize_ = 0;
    std::size_t allocatedBlocks_ = 0;
    std::list<Chunk*> partialChunks_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::unordered_map<std::uintptr_t, Chunk*> chunkMap_;
};
