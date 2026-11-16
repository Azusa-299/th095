#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace th95::crypto {

// Port of retail routine: interleaved (odd/even) XOR against a stepping key stream.
// Decrypts `buf` in place; `limit` bounds how many source bytes may be consumed.
void decrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block,
             std::size_t limit);

inline void decrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block)
{
    decrypt(buf, key, step, block, buf.size());
}

// Port of retail routine, the writer side of the same cipher: it gathers the block
// in the interleaved order and stores sequentially, so it is the exact inverse
// of decrypt() rather than the same operation applied twice.
void encrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block,
             std::size_t limit);

inline void encrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block)
{
    encrypt(buf, key, step, block, buf.size());
}

} // namespace th95::crypto
