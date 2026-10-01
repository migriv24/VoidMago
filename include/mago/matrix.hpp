// The interaction matrix: every pair of Void projects, in one table.
//
// The founding ask:
//
//     i would really like a nice interface to tell me if there are conflicts
//     across any of the void based applications
//
// and the shape asked for: a multiplication table. Projects down the side,
// projects across the top, and every cell the relationship between the two --
// with the observation that made the design, which is correct and load-bearing:
//
//     it will be very symmetrical cuz like the interaction between void
//     allomone and void maiz is the same as the interaction between void maiz
//     and void allomone (like AB = BA type of logic here)
//
// # 1. Symmetric, except for the part that is not
//
// The relationship IS symmetric. The direction is not: Void Maiz requires Void
// Core and Void Core does not require Void Maiz. Both facts are true of the
// same cell, and the standard decomposition of a square matrix says exactly
// how to hold them at once --
//
//     M = S + A,   S symmetric (S[i][j] = S[j][i]),
//                  A antisymmetric (A[i][j] = -A[j][i], A[i][i] = 0)
//
// -- so **the relationship is the symmetric part and the direction is the
// antisymmetric part.** `Pairing` is S: one record per unordered pair, stored
// once. `Cell::direction` is A: computed on read, negated when you look at the
// cell from the other side. That is the "AB = BA" rule holding literally, with
// the one asymmetry the family really has preserved rather than flattened.
//
// The consequence for storage: only the STRICT UPPER TRIANGLE exists.
// N(N-1)/2 records, not N^2. Seven projects is 21 pairs rather than 49 cells,
// and the twenty-one is the number a person actually has to author.
//
// # 2. The lattice, and why symmetry is a theorem here rather than a rule
//
// Two projects can be related in several ways at once -- Void Core both
// `requires`-adjacent to Void Hormiga and named in its `interacts_with`, which
// is the duplication `mago doctor` reports. So a cell is a JOIN over evidence,
// on the product of two chains:
//
//     bond:  none  <  authored  <  seam  <  requires
//     cost:  untouched < retest < reship < rebuild < blocked
//
// Each is a total order, so each is a lattice, and their product ordered
// componentwise is a lattice. A cell's value is the least upper bound of every
// piece of evidence about that pair.
//
// Because join is commutative, associative and idempotent, and because the
// evidence for {a,b} is gathered from BOTH manifests before it is joined, the
// result cannot depend on which project you asked about first. Symmetry is
// therefore a property of the computation and not a convention anybody has to
// remember -- which is the whole reason to build it this way rather than
// filling in two triangles and hoping they agree.
//
// # 3. What the diagonal is for
//
// In a multiplication table the diagonal holds the squares. Here M[i][i] is not
// an interaction -- a project does not have a seam with itself -- so the
// diagonal holds the PROJECT: its kind, version, how many pairs it is in, and
// whatever a person authored about it. It reads as the table's legend, down
// the middle, which is where a reader's eye already goes.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "mago/blast.hpp"
#include "mago/family.hpp"
#include "mago/registry.hpp"

namespace mago {

// How tightly two projects touch. A chain: the join of two bonds is the
// greater of them.
enum class Bond {
    None = 0,       // nothing in either manifest, nothing authored
    Authored = 1,   // a person wrote something; no manifest records an edge
    Seam = 2,       // data crosses, nothing links
    Requires = 3,   // a real dependency edge
};

const char* bond_name(Bond b);

// One unordered pair -- the symmetric part of the matrix.
struct Pairing {
    std::string a;  // canonical order: a <= b
    std::string b;

    Bond bond = Bond::None;

    // The antisymmetric part, as stored: +1 means A DEPENDS ON B, -1 means b
    // depends on a, 0 means neither (a seam, or nothing). Read it through
    // `Cell::direction`, which orients it to the cell you asked for.
    int direction = 0;

    std::string seam;     // the seam's name, agreed or claimed
    std::string link;     // requires[].link, when the bond is Requires
    std::string version;  // the constraint text, when the bond is Requires

    // What an ADDITIVE change to either end costs the other. The pairwise
    // cost only -- `mago blast` is the transitive question, and a cell that
    // tried to answer it would be reporting a path in a table of edges.
    Verdict cost = Verdict::Untouched;

    std::string note;              // authored, from family.json
    bool disputed = false;         // the two manifests name the seam differently
    std::vector<std::string> why;  // one sentence per piece of evidence

    bool authored() const { return !note.empty(); }
};

// A cell as seen FROM (i, j). Same `Pairing` from either side; the direction
// is the half that flips.
struct Cell {
    const Pairing* pairing = nullptr;
    int direction = 0;  // oriented i -> j: +1 means i depends on j

    bool empty() const { return pairing == nullptr || pairing->bond == Bond::None; }
};

// A project's own row -- the diagonal.
struct Diagonal {
    std::string name;
    std::string display_name;
    std::string kind;
    Version version;
    std::string status;
    std::string note;         // authored
    std::size_t degree = 0;   // pairs with a bond above None
    std::size_t requires_count = 0;
    std::size_t seam_count = 0;
};

// The whole table. Built once from a registry and the authored document, then
// read; nothing here mutates.
class Matrix {
public:
    static Matrix build(const Registry& reg, const Family& family);

    // Row and column order: the names, sorted. The same order for both axes,
    // because a matrix whose axes disagree is two tables in a trench coat.
    const std::vector<std::string>& names() const { return names_; }
    const std::vector<Diagonal>& diagonal() const { return diagonal_; }
    std::size_t size() const { return names_.size(); }

    // Every pair exactly once: the strict upper triangle, N(N-1)/2 of them.
    const std::vector<Pairing>& pairings() const { return upper_; }

    Cell at(std::size_t i, std::size_t j) const;
    Cell find(const std::string& a, const std::string& b) const;

    std::size_t index_of(const std::string& name) const;  // size() if absent

    // How much of the table is filled. The family's whole design bet is that
    // this number stays small: loosely coupled projects are the reason a Core
    // patch does not mean rebuilding thirty applications.
    std::size_t cells() const { return size() * (size() - 1) / 2; }
    std::size_t bonded() const;
    std::size_t authored() const;

    std::string to_json(int indent = 2) const;

    // The terminal rendering: the grid, its legend, and the pairs that carry
    // something worth reading.
    std::string render(bool show_empty = false) const;

private:
    std::vector<std::string> names_;
    std::vector<Diagonal> diagonal_;
    std::vector<Pairing> upper_;  // row-major strict upper triangle
};

}  // namespace mago
