// `family.json` -- the half of the family that no manifest can hold.
//
// Every `void.json` is ONE SIDE'S ACCOUNT of an edge. Void Core says it meets
// Void Palabra at the "c-abi" seam; Void Palabra says the seam is "state
// document + command journal". Both are internally consistent, both are
// authored honestly, and `mago doctor` has been reporting the contradiction
// since its first run -- because a fact about a PAIR belongs to neither
// repository, and there was nowhere to write it down.
//
// This is that place. One document, held by the project whose job is the
// family, carrying the sentences a person writes about pairs:
//
//     "Void Reyna emits a transcript Void Hormiga replays. Neither links the
//      other. The agreed name of the seam is `transcript`."
//
// Two properties are deliberate:
//
//   IT IS AUTHORED, NOT DERIVED. Nothing in here is computed, and Mago never
//   writes a sentence into it on its own. Everything derived lives in
//   `mago matrix`, which reads this document as one input among several.
//
//   IT IS SHAPED LIKE A GRAPH. Projects and pairs, not rows and columns --
//   because the same content has to become a table, a node canvas, and
//   eventually a Void Core mantle that Void Palabra can merge across two
//   machines. A table is one projection of a graph; a graph is not a
//   projection of a table.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mago {

inline constexpr const char* kFamilyName = "family.json";
inline constexpr const char* kFamilyFormat = "family/0.1";

// What a person wrote about one unordered pair.
//
// `a` and `b` are stored in canonical (sorted) order, which is what makes the
// document's identity of a pair the same as the matrix's: there is exactly one
// record for {a,b}, and asking for {b,a} finds it.
struct AuthoredPairing {
    std::string a;
    std::string b;
    std::string note;                 // the sentence; the whole point of the file
    std::string seam;                 // the AGREED name, when the two ends disagree
    std::vector<std::string> shares;  // what actually crosses: a vocabulary, a folder
    std::string status;               // agreed | proposed | disputed | ""
    std::string authored;             // YYYY-MM-DD, as written

    bool empty() const {
        return note.empty() && seam.empty() && shares.empty() && status.empty();
    }
};

// What a person wrote about one project -- the diagonal of the matrix, where a
// multiplication table keeps its squares and an interaction table has nothing
// to put.
struct AuthoredProject {
    std::string name;
    std::string note;
    std::vector<std::string> tags;
};

class Family {
public:
    // A missing file is an EMPTY family, not an error. The authored half is
    // optional by construction: Mago computed useful answers before this
    // document existed and still does without it.
    static Family load(const std::filesystem::path& path);

    // `<the voidmago checkout>/family.json` -- the document lives with the
    // project whose job is the family, so it is version-controlled and travels
    // with the tool rather than sitting unversioned above the checkouts.
    static std::filesystem::path default_path(const std::filesystem::path& mago_repo);

    // Sorted, so {b,a} and {a,b} are the same record.
    static std::pair<std::string, std::string> canonical(const std::string& x,
                                                         const std::string& y);

    const AuthoredPairing* pairing(const std::string& x, const std::string& y) const;
    const AuthoredProject* project(const std::string& name) const;

    const std::vector<AuthoredPairing>& pairings() const { return pairings_; }
    const std::vector<AuthoredProject>& projects() const { return projects_; }

    // Upsert. An entry whose fields are all empty is REMOVED rather than
    // stored blank -- an empty record and no record mean the same thing, and
    // keeping both would make the document's size a function of how often
    // somebody changed their mind.
    void set_pairing(const AuthoredPairing& p);
    void set_project(const AuthoredProject& p);

    std::string to_json(int indent = 2) const;

    // Written whole, sorted, every time. The document is small and hand-edited
    // as often as it is tool-edited, so a stable byte order matters more than
    // an incremental write.
    void save(const std::filesystem::path& path) const;

private:
    std::vector<AuthoredPairing> pairings_;
    std::vector<AuthoredProject> projects_;
};

}  // namespace mago
