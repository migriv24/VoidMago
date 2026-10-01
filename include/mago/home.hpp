// `MAGO_HOME` -- the folders this machine knows about, remembered between runs.
//
// `--root` is a flag, and a flag is retyped. A person maintaining thirty
// applications has their checkouts in two or three places, and re-stating that
// on every invocation is the kind of friction that ends with a shell alias
// nobody else has.
//
// So Mago keeps a home, exactly as Void Hormiga and FaultSack keep theirs
// (`FAULTSACK_HOME`), holding one thing: the list of roots to scan. Not a
// cache, not a mirror of the manifests, not a second source of truth --
// **the repositories are still the truth, and this is only a list of where to
// look for them.** Everything derived is recomputed on every run, because a
// registry that remembered what a manifest said would be a registry that could
// be wrong about it.
//
// `--root DIR` still wins, and a machine with no home behaves exactly as it
// did before there was one.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mago {

// One folder holding Void checkouts.
struct Root {
    std::filesystem::path path;
    std::string label;  // optional, for a person reading a list
    std::string added;  // YYYY-MM-DD

    // Recomputed on load, never stored: a root that has been moved or unplugged
    // should say so rather than quietly contributing nothing.
    bool exists = false;
};

class Home {
public:
    // `$MAGO_HOME`, else `~/.mago`. Never created until something is written.
    static std::filesystem::path dir();
    static std::filesystem::path roots_file();

    // A missing or unreadable home is an EMPTY home, not an error. Mago has to
    // run on a machine where nothing is set up; that is the whole job.
    static Home load();

    const std::vector<Root>& roots() const { return roots_; }
    bool empty() const { return roots_.empty(); }

    // Absolute, lexically normal, and de-duplicated. Returns false if the
    // folder was already registered.
    bool add(const std::filesystem::path& path, const std::string& label,
             const std::string& date);
    bool remove(const std::filesystem::path& path);

    void save() const;

    // The roots to scan, as paths. Empty when nothing is registered, which is
    // the signal to fall back to the built-in default.
    std::vector<std::filesystem::path> paths() const;

private:
    std::vector<Root> roots_;
};

}  // namespace mago
