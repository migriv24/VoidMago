#include "mago/manifest.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

extern "C" {
#include "cJSON.h"
}

namespace fs = std::filesystem;

namespace mago {
namespace {

// Sorted by what a USER experiences, not by how the code is shaped.
constexpr std::array<const char*, 3> kKinds = {"runtime", "capability", "application"};

// How a vendored library reaches the binary. A superset of kLinks, because
// third-party code arrives in ways a Void project does not: from the operating
// system, and from the compiler.
constexpr std::array<const char*, 7> kVendorLinks = {
    "source", "static", "shared", "vendored", "header-only", "system", "toolchain"};

// How a requirement is bound into its consumer. This is the field that decides
// whether an upstream change costs a rebuild or only a re-ship.
constexpr std::array<const char*, 5> kLinks = {"shared", "static", "source", "vendored",
                                               "import"};

// The two things a release can be. `mago blast` infers this from the version
// move when it is not written down, and the inference is a guess about a
// promise -- so a manifest that says it outranks the guess.
constexpr std::array<const char*, 2> kChanges = {"compatible", "breaking"};

bool one_of(const std::string& v, const auto& allowed) {
    return std::find(allowed.begin(), allowed.end(), v) != allowed.end();
}

std::string join(const auto& xs, const char* sep = ", ") {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

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

// Names what a value IS, for a refusal that tells the author which of their
// lines to look at rather than only which key.
const char* json_shape(const cJSON* v) {
    if (!v) return "missing";
    if (cJSON_IsString(v)) return "string";
    if (cJSON_IsNumber(v)) return "number";
    if (cJSON_IsBool(v)) return "boolean";
    if (cJSON_IsNull(v)) return "null";
    if (cJSON_IsArray(v)) return "array";
    if (cJSON_IsObject(v)) return "object";
    return "non-object";
}

// cJSON owns raw pointers; this makes the single exit path exception-safe.
struct JsonDoc {
    cJSON* root = nullptr;
    explicit JsonDoc(cJSON* r) : root(r) {}
    ~JsonDoc() { cJSON_Delete(root); }
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
};

Requirement parse_requirement(const cJSON* d, const fs::path& source) {
    Requirement r;
    r.name = str_field(d, "name");
    if (r.name.empty()) {
        throw ManifestError(source.string() + ": a requires[] entry has no name");
    }
    r.kind = str_field(d, "kind");
    r.sibling = str_field(d, "sibling");
    r.cmake_var = str_field(d, "cmake_var");
    r.link = str_field(d, "link");
    r.via = str_field(d, "via");
    r.note = str_field(d, "note");
    r.repository = str_field(d, "repository");
    r.optional = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "optional"));

    if (!r.link.empty() && !one_of(r.link, kLinks)) {
        throw ManifestError(source.string() + ": requires[" + r.name + "].link is \"" + r.link +
                            "\", expected one of " + join(kLinks));
    }

    const std::string version = str_field(d, "version");
    const auto range = Range::parse(version.empty() ? "*" : version);
    if (!range) {
        throw ManifestError(source.string() + ": requires[" + r.name +
                            "].version is not a version range: \"" + version + "\"");
    }
    r.version = *range;
    return r;
}

Vendor parse_vendor(const cJSON* d, const fs::path& source) {
    Vendor v;
    v.name = str_field(d, "name");
    if (v.name.empty()) {
        throw ManifestError(source.string() + ": a vendors[] entry has no name");
    }
    v.version = str_field(d, "version");
    v.license = str_field(d, "license");
    v.upstream = str_field(d, "upstream");
    v.path = str_field(d, "path");
    v.note = str_field(d, "note");
    v.version_source = str_field(d, "version_source");
    v.link = str_field(d, "link");
    if (!v.link.empty() && !one_of(v.link, kVendorLinks)) {
        throw ManifestError(source.string() + ": vendors[" + v.name + "].link is \"" + v.link +
                            "\", expected one of " + join(kVendorLinks));
    }
    return v;
}

Release parse_release(const cJSON* d, const fs::path& source, const Version& manifest_version) {
    Release r;
    r.declared = true;

    // The version defaults to the manifest's own, because a release block that
    // described some OTHER version would be a second, quieter version field.
    const std::string version = str_field(d, "version");
    if (version.empty()) {
        r.version = manifest_version;
    } else if (const auto parsed = Version::parse(version)) {
        r.version = *parsed;
    } else {
        throw ManifestError(source.string() + ": release.version is not a version: \"" + version +
                            "\"");
    }

    if (const std::string previous = str_field(d, "previous"); !previous.empty()) {
        const auto parsed = Version::parse(previous);
        if (!parsed) {
            throw ManifestError(source.string() + ": release.previous is not a version: \"" +
                                previous + "\"");
        }
        r.previous = *parsed;
    }

    r.change = str_field(d, "change");
    if (!r.change.empty() && !one_of(r.change, kChanges)) {
        throw ManifestError(source.string() + ": release.change is \"" + r.change +
                            "\", expected one of " + join(kChanges));
    }

    r.date = str_field(d, "date");
    r.summary = str_field(d, "summary");
    r.adds = str_array(d, "adds");
    r.read_more = str_array(d, "read_more");

    // `behavior_changes` is the one field whose absence nothing else in the
    // manifest can reveal: a dropped `adds` entry is a missing bullet, a
    // dropped behavior change is the warning that never reached the person
    // being asked to update. So every wrong shape here is refused by name.
    // Void Hormiga 0.1.1 shipped with four of these written as strings and a
    // feed that said `"behavior_changes": []` without a word about it.
    const cJSON* changes = cJSON_GetObjectItemCaseSensitive(d, "behavior_changes");
    if (changes && !cJSON_IsNull(changes)) {
        if (!cJSON_IsArray(changes)) {
            throw ManifestError(source.string() + ": release.behavior_changes is a " +
                                json_shape(changes) +
                                "; it is an array of objects, each with \"what\" and optional "
                                "\"who_is_affected\"");
        }
        int index = 0;
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, changes) {
            const std::string at = ": release.behavior_changes[" + std::to_string(index++) + "]";
            if (!cJSON_IsObject(item)) {
                throw ManifestError(source.string() + at + " is a " + json_shape(item) +
                                    "; each entry is an object with \"what\" and optional "
                                    "\"who_is_affected\"");
            }
            BehaviorChange bc;
            bc.what = str_field(item, "what");
            bc.who_is_affected = str_field(item, "who_is_affected");
            if (bc.what.empty()) {
                throw ManifestError(source.string() + at + " has no \"what\"");
            }
            r.behavior_changes.push_back(std::move(bc));
        }
    }
    return r;
}

}  // namespace

std::vector<std::string> Manifest::executables() const {
    std::vector<std::string> out;
    if (!gui.empty()) out.push_back(gui);
    if (!cli.empty()) out.push_back(cli);
    return out;
}

bool Manifest::provides_document(const std::string& document) const {
    for (const auto& [_, value] : documents) {
        if (value == document) return true;
    }
    return false;
}

const Requirement* Manifest::requirement(const std::string& dep) const {
    for (const auto& r : requirements) {
        if (r.name == dep) return &r;
    }
    return nullptr;
}

Manifest load(const fs::path& path_in) {
    fs::path path = path_in;
    std::error_code ec;
    if (fs::is_directory(path, ec)) path /= kManifestName;

    std::ifstream in(path, std::ios::binary);
    if (!in) throw ManifestError(path.string() + ": cannot read");
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    const char* end = nullptr;
    JsonDoc doc(cJSON_ParseWithOpts(text.c_str(), &end, /*require_null_terminated=*/1));
    if (!doc.root) {
        std::string where;
        if (end && end >= text.c_str()) {
            where = " near byte " + std::to_string(end - text.c_str());
        }
        throw ManifestError(path.string() + ": not valid JSON" + where);
    }

    const std::string format = str_field(doc.root, "manifest");
    if (format != kManifestFormat) {
        throw ManifestError(path.string() + ": manifest format \"" + format +
                            "\", this Mago knows \"" + kManifestFormat + "\"");
    }

    Manifest m;
    m.path = path.parent_path();
    m.name = str_field(doc.root, "name");
    m.kind = str_field(doc.root, "kind");
    const std::string version = str_field(doc.root, "version");

    for (const auto& [value, field] : {std::pair{m.name, "name"}, std::pair{m.kind, "kind"},
                                       std::pair{version, "version"}}) {
        if (value.empty()) throw ManifestError(path.string() + ": no \"" + field + "\" field");
    }
    if (!one_of(m.kind, kKinds)) {
        throw ManifestError(path.string() + ": kind is \"" + m.kind + "\", expected one of " +
                            join(kKinds));
    }
    const auto parsed = Version::parse(version);
    if (!parsed) {
        throw ManifestError(path.string() + ": version is not a version: \"" + version + "\"");
    }
    m.version = *parsed;

    m.display_name = str_field(doc.root, "display_name");
    if (m.display_name.empty()) m.display_name = m.name;
    m.license = str_field(doc.root, "license");
    m.repository = str_field(doc.root, "repository");
    m.description = str_field(doc.root, "description");
    m.status = str_field(doc.root, "status");
    m.platforms = str_array(doc.root, "platforms");
    m.fork_of = str_field(doc.root, "fork_of");
    if (const cJSON* id = cJSON_GetObjectItemCaseSensitive(doc.root, "identity"); cJSON_IsObject(id)) {
        m.identity.app_id = str_field(id, "app_id");
        m.identity.publisher = str_field(id, "publisher");
        m.identity.feed = str_field(id, "feed");
        m.identity.belongs_to = str_field(id, "belongs_to");
    }
    if (const cJSON* c = cJSON_GetObjectItemCaseSensitive(doc.root, "contribute"); cJSON_IsObject(c)) {
        m.contribute.guide = str_field(c, "guide");
        m.contribute.security = str_field(c, "security");
        m.contribute.issues = str_field(c, "issues");
    }

    const cJSON* reqs = cJSON_GetObjectItemCaseSensitive(doc.root, "requires");
    if (cJSON_IsArray(reqs)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, reqs) {
            if (cJSON_IsObject(item)) m.requirements.push_back(parse_requirement(item, path));
        }
    }

    const cJSON* vendors = cJSON_GetObjectItemCaseSensitive(doc.root, "vendors");
    if (cJSON_IsArray(vendors)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, vendors) {
            if (cJSON_IsObject(item)) m.vendors.push_back(parse_vendor(item, path));
        }
    }

    const cJSON* inters = cJSON_GetObjectItemCaseSensitive(doc.root, "interacts_with");
    if (cJSON_IsArray(inters)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, inters) {
            if (!cJSON_IsObject(item)) continue;
            Interaction i;
            i.name = str_field(item, "name");
            if (i.name.empty()) i.name = "?";
            i.kind = str_field(item, "kind");
            i.seam = str_field(item, "seam");
            i.direction = str_field(item, "direction");
            i.note = str_field(item, "note");
            m.interacts_with.push_back(std::move(i));
        }
    }

    if (const cJSON* p = cJSON_GetObjectItemCaseSensitive(doc.root, "provides");
        cJSON_IsObject(p)) {
        m.cli = str_field(p, "cli");
        m.gui = str_field(p, "gui");
        m.artifacts = str_array(p, "artifacts");
        m.ships_beside_binary = str_array(p, "ships_beside_binary");
        m.effects = str_array(p, "effects");

        // `contracts` is an open map -- spec, conformance, schema, whatever a
        // project calls the documents that define its seams. Mago never opens
        // them; it only tells a downstream where to read.
        for (const auto& [key, into] : {std::pair{"contracts", &m.contracts},
                                       std::pair{"documents", &m.documents}}) {
            const cJSON* obj = cJSON_GetObjectItemCaseSensitive(p, key);
            if (!cJSON_IsObject(obj)) continue;
            const cJSON* item = nullptr;
            cJSON_ArrayForEach(item, obj) {
                if (cJSON_IsString(item) && item->string && item->valuestring) {
                    into->emplace(item->string, item->valuestring);
                }
            }
        }
    }

    if (const cJSON* vs = cJSON_GetObjectItemCaseSensitive(doc.root, "version_sources");
        cJSON_IsArray(vs)) {
        m.version_sources_declared = true;
        m.version_sources = str_array(doc.root, "version_sources");
    }

    if (const cJSON* r = cJSON_GetObjectItemCaseSensitive(doc.root, "release");
        cJSON_IsObject(r)) {
        m.release = parse_release(r, path, m.version);
    }

    return m;
}

}  // namespace mago
