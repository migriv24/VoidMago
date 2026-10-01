#include "mago/sha256.hpp"

#include <array>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace mago {
namespace {

// FIPS 180-4. The constants are the first 32 bits of the fractional parts of
// the cube roots of the first 64 primes; the initial state is the same of the
// square roots of the first 8. Both are in the standard and neither is a
// choice anybody made.
constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

std::uint32_t rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

class Sha256 {
public:
    void update(const unsigned char* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            buffer_[fill_++] = data[i];
            if (fill_ == 64) {
                compress();
                fill_ = 0;
                bits_ += 512;
            }
        }
    }

    std::string final_hex() {
        const std::uint64_t total_bits = bits_ + static_cast<std::uint64_t>(fill_) * 8;
        // The padding is a 1 bit, then zeros, then the length as 64 big-endian
        // bits -- and the length is of the ORIGINAL message, which is why it
        // is computed before any of this is appended.
        unsigned char one = 0x80;
        update(&one, 1);
        unsigned char zero = 0;
        while (fill_ != 56) update(&zero, 1);
        for (int i = 7; i >= 0; --i) {
            const unsigned char byte =
                static_cast<unsigned char>((total_bits >> (i * 8)) & 0xFF);
            update(&byte, 1);
        }

        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (std::uint32_t word : state_) {
            for (int i = 3; i >= 0; --i) {
                const unsigned char byte = static_cast<unsigned char>((word >> (i * 8)) & 0xFF);
                out += hex[byte >> 4];
                out += hex[byte & 0x0F];
            }
        }
        return out;
    }

private:
    void compress() {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(buffer_[i * 4]) << 24) |
                (static_cast<std::uint32_t>(buffer_[i * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(buffer_[i * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(buffer_[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 =
                rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 =
                rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<unsigned char, 64> buffer_{};
    std::size_t fill_ = 0;
    std::uint64_t bits_ = 0;
};

}  // namespace

std::string sha256_hex(std::string_view bytes) {
    Sha256 h;
    h.update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    return h.final_hex();
}

std::optional<std::string> sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    Sha256 h;
    std::array<char, 64 * 1024> chunk{};
    while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0) {
        h.update(reinterpret_cast<const unsigned char*>(chunk.data()),
                 static_cast<std::size_t>(in.gcount()));
        if (!in) break;
    }
    return h.final_hex();
}

}  // namespace mago
