#include "mago/home.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "mago/json_out.hpp"
#include "mago/manifest.hpp"

extern "C" {
#include "cJSON.h"
}

namespace fs = std::filesystem;

namespace mago {
namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

fs::path normalise(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (ec) abs = p;
    return abs.lexically_normal();
}

struct JsonDoc {
    cJSON* root = nullptr;
    explicit JsonDoc(cJSON* r) : root(r) {}
    ~JsonDoc() { cJSON_Delete(root); }
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
};

std::string str_field(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? std::string(v->valuestring) : std::string();
}

}  // namespace

fs::path Home::dir() {
    if (const std::string explicit_home = env("MAGO_HOME"); !explicit_home.empty()) {
        return fs::path(explicit_home);
    }
    std::string profile = env("USERPROFILE");  // Windows
    if (profile.empty()) profile = env("HOME");
    if (profile.empty()) return fs::path(".mago");
    return fs::path(profile) / ".mago";
}

fs::path Home::roots_file() {
    return dir() / "roots.json";
}

Home Home::load() {
    Home home;
    const fs::path path = roots_file();
    std::error_code ec;
    if (!fs::exists(path, ec)) return home;

    std::ifstream in(path, std::ios::binary);
    if (!in) return home;
    std::ostringstream buf;
    buf << in.rdbuf();

    JsonDoc doc(cJSON_Parse(buf.str().c_str()));
    if (!doc.root) throw ManifestError(path.string() + ": not valid JSON");

    const cJSON* roots = cJSON_GetObjectItemCaseSensitive(doc.root, "roots");
    if (!cJSON_IsArray(roots)) return home;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, roots) {
        Root r;
        if (cJSON_IsString(item) && item->valuestring) {
            r.path = normalise(item->valuestring);
        } else if (cJSON_IsObject(item)) {
            const std::string p = str_field(item, "path");
            if (p.empty()) continue;
            r.path = normalise(p);
            r.label = str_field(item, "label");
            r.added = str_field(item, "added");
        } else {
            continue;
        }
        // Never stored: whether it is still there is a fact about now.
        r.exists = fs::is_directory(r.path, ec);
        home.roots_.push_back(std::move(r));
    }
    return home;
}

bool Home::add(const fs::path& path, const std::string& label, const std::string& date) {
    const fs::path target = normalise(path);
    for (const auto& r : roots_) {
        if (r.path == target) return false;
    }
    Root r;
    r.path = target;
    r.label = label;
    r.added = date;
    std::error_code ec;
    r.exists = fs::is_directory(target, ec);
    roots_.push_back(std::move(r));
    std::sort(roots_.begin(), roots_.end(),
              [](const Root& l, const Root& r2) { return l.path < r2.path; });
    return true;
}

bool Home::remove(const fs::path& path) {
    const fs::path target = normalise(path);
    const auto at = std::find_if(roots_.begin(), roots_.end(),
                                 [&](const Root& r) { return r.path == target; });
    if (at == roots_.end()) return false;
    roots_.erase(at);
    return true;
}

std::vector<fs::path> Home::paths() const {
    std::vector<fs::path> out;
    for (const auto& r : roots_) {
        if (r.exists) out.push_back(r.path);
    }
    return out;
}

void Home::save() const {
    const fs::path path = roots_file();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    JsonWriter w(2);
    w.begin_object();
    w.field("home", "mago-home/0.1");
    w.field("note",
            "Folders holding Void checkouts. A list of where to look, and nothing else -- "
            "the repositories remain the source of truth and everything derived is "
            "recomputed on every run.");
    w.key("roots").begin_array();
    for (const auto& r : roots_) {
        w.begin_object();
        w.field("path", r.path.generic_string());
        w.field("label", r.label);
        w.field("added", r.added);
        w.end_object();
    }
    w.end_array().end_object();

    std::ofstream out(path, std::ios::binary);
    if (!out) throw ManifestError("cannot write " + path.string());
    out << w.str() << "\n";
}

}  // namespace mago
