// SHA-256, because an update feed that cannot say what it is pointing at is
// not an update feed.
//
// Written here rather than linked: the CLI takes nothing from the family and
// nothing from the machine, and a hash function is 120 lines of arithmetic
// that has been unchanged since 2001. Vendoring OpenSSL to get it would
// contradict the property that makes `mago` able to run on a broken install.
//
// This is NOT a signature. A hash says the bytes arrived intact; it says
// nothing about who produced them, and a feed served over the same channel as
// the file it describes can have both rewritten together. Signing is
// ed25519's job and it is not built yet -- see okf/concepts/distribution.md.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mago {

// Lowercase hex, 64 characters.
std::string sha256_hex(std::string_view bytes);

// The file's hash, or nullopt if it cannot be read. Streamed, so a large
// installer does not have to fit in memory.
std::optional<std::string> sha256_file(const std::filesystem::path& path);

}  // namespace mago
