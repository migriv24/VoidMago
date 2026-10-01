#include "mago/family.hpp"

#include <algorithm>
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

std::vector<std::string> str_array(const cJSON* obj, const char* key) {
    std::vector<std::string> out;
    const cJSON* arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(arr)) return out;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsString(item) && item->valuestring) out.emplace_back(item->valuestring);
    }
    return out;
}

}  // namespace

std::pair<std::string, std::string> Family::canonical(const std::string& x,
                                                      const std::string& y) {
    return x <= y ? std::pair{x, y} : std::pair{y, x};
}

fs::path Family::default_path(const fs::path& mago_repo) {
    return mago_repo / kFamilyName;
}

Family Family::load(const fs::path& path) {
    Family family;
    std::error_code ec;
    if (!fs::exists(path, ec)) return family;  // nothing authored yet

    std::ifstream in(path, std::ios::binary);
    if (!in) throw ManifestError(path.string() + ": cannot read");
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    JsonDoc doc(cJSON_Parse(text.c_str()));
    if (!doc.root) throw ManifestError(path.string() + ": not valid JSON");

    const std::string format = str_field(doc.root, "family");
    if (format != kFamilyFormat) {
        throw ManifestError(path.string() + ": family format \"" + format +
                            "\", this Mago knows \"" + kFamilyFormat + "\"");
    }

    const cJSON* pairs = cJSON_GetObjectItemCaseSensitive(doc.root, "pairings");
    if (cJSON_IsArray(pairs)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, pairs) {
            if (!cJSON_IsObject(item)) continue;
            const std::vector<std::string> pair = str_array(item, "pair");
            if (pair.size() != 2) {
                throw ManifestError(path.string() +
                                    ": a pairings[] entry has no two-name \"pair\"");
            }
            AuthoredPairing p;
            std::tie(p.a, p.b) = canonical(pair[0], pair[1]);
            p.note = str_field(item, "note");
            p.seam = str_field(item, "seam");
            p.shares = str_array(item, "shares");
            p.status = str_field(item, "status");
            p.authored = str_field(item, "authored");
            family.set_pairing(p);
        }
    }

    const cJSON* projects = cJSON_GetObjectItemCaseSensitive(doc.root, "projects");
    if (cJSON_IsObject(projects)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, projects) {
            if (!cJSON_IsObject(item) || !item->string) continue;
            AuthoredProject p;
            p.name = item->string;
            p.note = str_field(item, "note");
            p.tags = str_array(item, "tags");
            family.set_project(p);
        }
    }
    return family;
}

const AuthoredPairing* Family::pairing(const std::string& x, const std::string& y) const {
    const auto [a, b] = canonical(x, y);
    for (const auto& p : pairings_) {
        if (p.a == a && p.b == b) return &p;
    }
    return nullptr;
}

const AuthoredProject* Family::project(const std::string& name) const {
    for (const auto& p : projects_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

void Family::set_pairing(const AuthoredPairing& in) {
    AuthoredPairing p = in;
    std::tie(p.a, p.b) = canonical(p.a, p.b);

    const auto at = std::find_if(pairings_.begin(), pairings_.end(), [&](const auto& x) {
        return x.a == p.a && x.b == p.b;
    });
    if (p.empty()) {
        if (at != pairings_.end()) pairings_.erase(at);
        return;
    }
    if (at != pairings_.end()) {
        *at = p;
        return;
    }
    pairings_.push_back(p);
    std::sort(pairings_.begin(), pairings_.end(), [](const auto& l, const auto& r) {
        return l.a != r.a ? l.a < r.a : l.b < r.b;
    });
}

void Family::set_project(const AuthoredProject& p) {
    const auto at = std::find_if(projects_.begin(), projects_.end(),
                                 [&](const auto& x) { return x.name == p.name; });
    if (p.note.empty() && p.tags.empty()) {
        if (at != projects_.end()) projects_.erase(at);
        return;
    }
    if (at != projects_.end()) {
        *at = p;
        return;
    }
    projects_.push_back(p);
    std::sort(projects_.begin(), projects_.end(),
              [](const auto& l, const auto& r) { return l.name < r.name; });
}

std::string Family::to_json(int indent) const {
    JsonWriter w(indent);
    w.begin_object();
    w.field("family", kFamilyFormat);
    w.field("note",
            "The authored half of the family: what a person knows about a PAIR of "
            "projects, which belongs to neither repository's manifest. Nothing here is "
            "computed. `mago matrix` reads it; `mago pair` writes it.");

    w.key("projects").begin_object();
    for (const auto& p : projects_) {
        w.key(p.name).begin_object();
        w.field("note", p.note);
        w.string_array("tags", p.tags);
        w.end_object();
    }
    w.end_object();

    w.key("pairings").begin_array();
    for (const auto& p : pairings_) {
        w.begin_object();
        w.string_array("pair", {p.a, p.b});
        w.field("note", p.note);
        w.field("seam", p.seam);
        w.string_array("shares", p.shares);
        w.field("status", p.status);
        w.field("authored", p.authored);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

void Family::save(const fs::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw ManifestError("cannot write " + path.string());
    out << to_json() << "\n";
}

}  // namespace mago
