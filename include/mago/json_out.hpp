// A small streaming JSON writer.
//
// cJSON is vendored for READING manifests, where a real parser is worth having.
// For writing, this is deliberately separate: the plan and the receipt are
// documents a person reads and a later `mago` parses back, so their shape and
// indentation are part of the contract rather than a printer's preference.
//
// Nesting is tracked so a missed end_object() is a visible structural bug
// rather than silently truncated output.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mago {

class JsonWriter {
public:
    explicit JsonWriter(int indent = 2) : indent_(indent) {}

    JsonWriter& begin_object() { return open('{'); }
    JsonWriter& end_object() { return close('}'); }
    JsonWriter& begin_array() { return open('['); }
    JsonWriter& end_array() { return close(']'); }

    JsonWriter& key(std::string_view k) {
        separate();
        quote(k);
        out_ += indent_ > 0 ? ": " : ":";
        pending_key_ = true;
        return *this;
    }

    JsonWriter& value(std::string_view s) {
        separate();
        quote(s);
        return *this;
    }
    JsonWriter& value(const char* s) { return value(std::string_view(s)); }
    JsonWriter& value(const std::string& s) { return value(std::string_view(s)); }
    JsonWriter& value(long long n) {
        separate();
        out_ += std::to_string(n);
        return *this;
    }
    JsonWriter& value(bool b) {
        separate();
        out_ += b ? "true" : "false";
        return *this;
    }
    JsonWriter& null_value() {
        separate();
        out_ += "null";
        return *this;
    }

    // A string field, or null when empty -- the shape the plan documents use
    // for "this project did not say".
    JsonWriter& field(std::string_view k, std::string_view s) {
        key(k);
        return s.empty() ? null_value() : value(s);
    }

    JsonWriter& string_array(std::string_view k, const std::vector<std::string>& xs) {
        key(k).begin_array();
        for (const auto& x : xs) value(x);
        return end_array();
    }

    std::string str() const { return out_; }

private:
    JsonWriter& open(char c) {
        separate();
        out_ += c;
        depth_.push_back(0);
        return *this;
    }

    JsonWriter& close(char c) {
        const bool had_items = !depth_.empty() && depth_.back() > 0;
        if (!depth_.empty()) depth_.pop_back();
        if (had_items) newline();
        out_ += c;
        if (!depth_.empty()) depth_.back()++;
        return *this;
    }

    // Emit the comma and newline that must precede the next item, unless we
    // are the value half of a key that was just written.
    void separate() {
        if (pending_key_) {
            pending_key_ = false;
            return;
        }
        if (depth_.empty()) return;
        if (depth_.back() > 0) out_ += ',';
        newline();
        depth_.back()++;
    }

    void newline() {
        if (indent_ <= 0) return;
        out_ += '\n';
        out_.append(static_cast<std::size_t>(indent_) * depth_.size(), ' ');
    }

    void quote(std::string_view s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                default:
                    if (c < 0x20) {
                        static const char* hex = "0123456789abcdef";
                        out_ += "\\u00";
                        out_ += hex[(c >> 4) & 0xF];
                        out_ += hex[c & 0xF];
                    } else {
                        out_ += static_cast<char>(c);
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<int> depth_;  // item count per open container
    int indent_ = 2;
    bool pending_key_ = false;
};

}  // namespace mago
