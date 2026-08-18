// A grammatical feature bundle: a small sorted list of key=value pairs.
// Sorted so that two bundles carrying the same information compare equal and
// print the same way, which the parse forest and the tests both rely on.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lex {

class Features {
public:
    void set(std::string key, std::string value);
    const std::string* get(std::string_view key) const;
    bool has(std::string_view key, std::string_view value) const;

    // A constraint bundle may offer alternatives: vform=base|past matches both.
    bool satisfies(const Features& constraint) const;

    // "num=pl vform=base", with '-' accepted as the empty bundle.
    static Features parse(std::string_view text);
    std::string str() const;

    bool empty() const { return items_.empty(); }
    const std::vector<std::pair<std::string, std::string>>& items() const { return items_; }
    bool operator==(const Features& other) const { return items_ == other.items_; }

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// Whitespace split, used by every loader in the project.
std::vector<std::string> split_ws(std::string_view text);
std::string trim(std::string_view text);
std::string to_lower_ascii(std::string_view text);

// Absolute path of a bundled data file. The compiled in directory is the one
// configured by CMake; the LEXICON_DATA_DIR environment variable overrides it so
// the binaries stay usable after being copied elsewhere.
std::string data_path(std::string_view file);

// Reads a text file, dropping '#' comments and blank lines. Throws on failure,
// because every caller needs the file and none of them can carry on without it.
std::vector<std::string> read_config_lines(const std::string& path);

}  // namespace lex
