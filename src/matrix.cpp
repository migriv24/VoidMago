#include "mago/matrix.hpp"

#include <algorithm>
#include <sstream>

#include "mago/json_out.hpp"

namespace mago {
namespace {

std::string pad(std::string s, std::size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

std::string join(const std::vector<std::string>& xs, const char* sep = ", ") {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

// The join of the bond chain: the tighter of the two.
Bond operator|(Bond l, Bond r) {
    return static_cast<int>(l) >= static_cast<int>(r) ? l : r;
}

// The join of the cost chain. Verdict is already ordered by severity, which is
// what makes "worst wins" a max rather than a table of special cases.
Verdict worst(Verdict l, Verdict r) {
    return static_cast<int>(l) >= static_cast<int>(r) ? l : r;
}

// Two characters per cell: what the relationship is, and whether a person has
// said anything about it.
std::string glyph(const Cell& c) {
    if (!c.pairing) return ".  ";
    const Pairing& p = *c.pairing;
    char relation = '.';
    switch (p.bond) {
        case Bond::None: relation = '.'; break;
        case Bond::Authored: relation = '+'; break;
        case Bond::Seam: relation = '-'; break;
        case Bond::Requires: relation = c.direction > 0 ? '>' : (c.direction < 0 ? '<' : '='); break;
    }
    char mark = ' ';
    if (p.disputed) {
        mark = '!';
    } else if (p.authored()) {
        mark = '*';
    }
    return std::string{relation, mark, ' '};
}

}  // namespace

const char* bond_name(Bond b) {
    switch (b) {
        case Bond::None: return "none";
        case Bond::Authored: return "authored";
        case Bond::Seam: return "seam";
        case Bond::Requires: return "requires";
    }
    return "?";
}

std::size_t Matrix::index_of(const std::string& name) const {
    const auto at = std::lower_bound(names_.begin(), names_.end(), name);
    if (at == names_.end() || *at != name) return names_.size();
    return static_cast<std::size_t>(at - names_.begin());
}

Cell Matrix::at(std::size_t i, std::size_t j) const {
    if (i == j || i >= size() || j >= size()) return {};

    // The strict upper triangle, addressed from either side. Row-major:
    // skipping i full rows minus the i(i+1)/2 cells at or below the diagonal.
    const std::size_t lo = std::min(i, j);
    const std::size_t hi = std::max(i, j);
    const std::size_t n = size();
    const std::size_t k = lo * n - lo * (lo + 1) / 2 + (hi - lo - 1);

    Cell cell;
    cell.pairing = &upper_[k];
    // A, the antisymmetric part: negated when the cell is read from below the
    // diagonal. This one line is the whole of "AB = BA, except for the arrow".
    cell.direction = (i < j) ? upper_[k].direction : -upper_[k].direction;
    return cell;
}

Cell Matrix::find(const std::string& a, const std::string& b) const {
    return at(index_of(a), index_of(b));
}

std::size_t Matrix::bonded() const {
    return static_cast<std::size_t>(std::count_if(
        upper_.begin(), upper_.end(), [](const Pairing& p) { return p.bond != Bond::None; }));
}

std::size_t Matrix::authored() const {
    return static_cast<std::size_t>(
        std::count_if(upper_.begin(), upper_.end(), [](const Pairing& p) { return p.authored(); }));
}

Matrix Matrix::build(const Registry& reg, const Family& family) {
    Matrix m;
    for (const auto& [name, _] : reg.projects()) m.names_.push_back(name);

    for (const auto& name : m.names_) {
        const Manifest& mf = reg.at(name);
        Diagonal d;
        d.name = name;
        d.display_name = mf.display_name;
        d.kind = mf.kind;
        d.version = mf.version;
        d.status = mf.status;
        if (const AuthoredProject* authored = family.project(name)) d.note = authored->note;
        m.diagonal_.push_back(std::move(d));
    }

    const std::size_t n = m.names_.size();
    m.upper_.reserve(n * (n - 1) / 2);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const std::string& a = m.names_[i];
            const std::string& b = m.names_[j];
            const Manifest& ma = reg.at(a);
            const Manifest& mb = reg.at(b);

            Pairing p;
            p.a = a;
            p.b = b;

            // --- evidence 1: the dependency edges, in both directions -------
            const Requirement* a_needs_b = ma.requirement(b);
            const Requirement* b_needs_a = mb.requirement(a);
            for (const auto& [req, forward] :
                 {std::pair{a_needs_b, true}, std::pair{b_needs_a, false}}) {
                if (!req) continue;
                p.bond = p.bond | Bond::Requires;
                p.direction += forward ? 1 : -1;
                p.link = req->link;
                p.version = req->version.text();
                p.cost = worst(p.cost, cost_of_link(req->link, Change::Compatible));
                p.why.push_back((forward ? a : b) + " requires " + (forward ? b : a) + " " +
                                req->version.text() + ", linked " +
                                (req->link.empty() ? "(undeclared)" : req->link));
            }

            // --- evidence 2: the seams, from whichever side wrote them ------
            std::string seam_a;  // what a calls it
            std::string seam_b;  // what b calls it
            for (const auto& i_edge : ma.interacts_with) {
                if (i_edge.name == b) seam_a = i_edge.seam;
            }
            for (const auto& i_edge : mb.interacts_with) {
                if (i_edge.name == a) seam_b = i_edge.seam;
            }
            const bool has_seam = !seam_a.empty() || !seam_b.empty();
            if (has_seam) {
                p.bond = p.bond | Bond::Seam;
                p.seam = seam_a.empty() ? seam_b : seam_a;
                if (p.bond == Bond::Seam) p.cost = worst(p.cost, Verdict::Retest);
                if (!seam_a.empty() && !seam_b.empty() && seam_a != seam_b) {
                    p.disputed = true;
                    p.why.push_back(a + " calls the seam \"" + seam_a + "\"; " + b +
                                    " calls it \"" + seam_b + "\"");
                } else {
                    p.why.push_back(a + " and " + b + " meet at the \"" + p.seam + "\" seam");
                }
                // A seam is named after a document, and a document has an
                // owner. Only the owner's release can move it, which is why
                // reading everybody's manifest does not make Mago fragile to
                // everybody. Worth saying in the cell, since the pair table
                // has no "who changed" to reason from.
                for (const auto& [owner, other] : {std::pair{&ma, &mb}, std::pair{&mb, &ma}}) {
                    if (!p.seam.empty() && owner->provides_document(p.seam)) {
                        p.why.push_back("the \"" + p.seam + "\" document is " + owner->name +
                                        "'s, so only " + owner->name +
                                        " changing it costs " + other->name + " a retest");
                    }
                }
            }

            // Both a requires edge AND a seam is one relationship described
            // twice, which drifts. `mago doctor` says so; the cell shows the
            // tighter of the two, because that is what it costs.
            if (a_needs_b || b_needs_a) {
                if (has_seam) {
                    p.why.push_back(
                        "described twice -- as a requires edge and as a seam; the requires "
                        "is the one Mago counts");
                }
                if (a_needs_b && b_needs_a) {
                    p.direction = 0;
                    p.why.push_back("each requires the other, which is a cycle");
                }
            }

            // --- evidence 3: what a person wrote ----------------------------
            if (const AuthoredPairing* authored = family.pairing(a, b)) {
                p.note = authored->note;
                if (!authored->seam.empty()) {
                    p.seam = authored->seam;
                    if (p.disputed) {
                        p.disputed = false;
                        p.why.push_back("family.json settles the name as \"" + authored->seam +
                                        "\"; the manifests still disagree and one of them "
                                        "should be corrected");
                    }
                }
                if (!authored->shares.empty()) {
                    p.why.push_back("shares " + join(authored->shares));
                }
                p.bond = p.bond | Bond::Authored;
            }

            m.upper_.push_back(std::move(p));
        }
    }

    // Degrees, once the whole triangle exists.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            const Cell c = m.at(i, j);
            if (c.empty()) continue;
            m.diagonal_[i].degree++;
            if (c.pairing->bond == Bond::Requires) m.diagonal_[i].requires_count++;
            if (c.pairing->bond == Bond::Seam) m.diagonal_[i].seam_count++;
        }
    }
    return m;
}

std::string Matrix::to_json(int indent) const {
    JsonWriter w(indent);
    w.begin_object();
    w.field("report", "mago-matrix/0.1");

    // The axis order, once. Every consumer -- a table, a canvas, a diff --
    // indexes into this, so the matrix has exactly one idea of who is row 3.
    w.string_array("axis", names_);

    w.key("projects").begin_array();
    for (const auto& d : diagonal_) {
        w.begin_object();
        w.field("name", d.name);
        w.field("display_name", d.display_name);
        w.field("kind", d.kind);
        w.key("version").value(d.version.str());
        w.field("status", d.status);
        w.field("note", d.note);
        w.key("degree").value(static_cast<long long>(d.degree));
        w.key("requires").value(static_cast<long long>(d.requires_count));
        w.key("seams").value(static_cast<long long>(d.seam_count));
        w.end_object();
    }
    w.end_array();

    // The strict upper triangle only. A consumer that wants cell (j,i) reads
    // (i,j) and negates `direction`; sending both halves would be sending the
    // same fact twice and inviting them to disagree.
    w.key("pairings").begin_array();
    for (const auto& p : upper_) {
        if (p.bond == Bond::None) continue;
        w.begin_object();
        w.string_array("pair", {p.a, p.b});
        w.field("bond", bond_name(p.bond));
        w.key("direction").value(static_cast<long long>(p.direction));
        w.field("seam", p.seam);
        w.field("link", p.link);
        w.field("version", p.version);
        w.field("cost", verdict_name(p.cost));
        w.field("note", p.note);
        w.key("disputed").value(p.disputed);
        w.string_array("why", p.why);
        w.end_object();
    }
    w.end_array();

    w.key("stats").begin_object();
    w.key("projects").value(static_cast<long long>(size()));
    w.key("cells").value(static_cast<long long>(cells()));
    w.key("bonded").value(static_cast<long long>(bonded()));
    w.key("authored").value(static_cast<long long>(authored()));
    w.end_object();

    w.end_object();
    return w.str();
}

std::string Matrix::render(bool show_empty) const {
    std::ostringstream s;
    const std::size_t n = size();

    s << n << " projects, " << cells() << " pairs, " << bonded() << " bonded, " << authored()
      << " with a note\n\n";

    std::size_t label_width = 6;
    for (const auto& d : diagonal_) label_width = std::max(label_width, d.display_name.size());

    s << pad("", label_width + 6);
    for (std::size_t j = 0; j < n; ++j) s << pad(std::to_string(j + 1), 3);
    s << "\n";

    for (std::size_t i = 0; i < n; ++i) {
        s << pad(std::to_string(i + 1), 3) << pad(diagonal_[i].display_name, label_width + 3);
        for (std::size_t j = 0; j < n; ++j) {
            s << (i == j ? "#  " : glyph(at(i, j)));
        }
        s << "\n";
    }

    s << "\n  #  the project itself        >  the row requires the column\n"
         "  .  no edge either way        <  the column requires the row\n"
         "  -  a seam: data crosses, nothing links\n"
         "  +  authored only: a person recorded a relationship no manifest does\n"
         "  *  a note in " << kFamilyName << "        !  the two manifests disagree\n";

    std::vector<const Pairing*> interesting;
    for (const auto& p : upper_) {
        if (show_empty || p.bond != Bond::None) interesting.push_back(&p);
    }
    if (interesting.empty()) return s.str();

    std::size_t pair_width = 12;
    for (const Pairing* p : interesting) {
        pair_width = std::max(pair_width, p->a.size() + p->b.size() + 4);
    }

    s << "\nthe pairs\n";
    for (const Pairing* p : interesting) {
        const char* arrow = p->direction > 0 ? " -> " : (p->direction < 0 ? " <- " : " -- ");
        std::vector<std::string> detail;
        if (!p->link.empty()) detail.push_back(p->link + " " + p->version);
        if (!p->seam.empty()) detail.push_back("seam: " + p->seam);
        if (p->disputed) detail.push_back("DISPUTED");

        s << "  " << pad(p->a + arrow + p->b, pair_width + 2) << pad(bond_name(p->bond), 10)
          << pad(verdict_name(p->cost), 10) << join(detail, "   ") << "\n";
        if (p->authored()) s << pad("", pair_width + 4) << p->note << "\n";
    }

    // The number this table exists to keep small. A family whose matrix fills
    // up has stopped being a family of separable projects.
    const std::size_t empty_cells = cells() - bonded();
    s << "\n" << empty_cells << " of " << cells()
      << " pairs have no edge at all, which is the number this table exists to keep "
         "large.\n";
    return s.str();
}

}  // namespace mago
