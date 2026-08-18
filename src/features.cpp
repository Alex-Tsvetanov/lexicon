#include "lexicon/features.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace lex {

void Features::set(std::string key, std::string value) {
    for (auto& item : items_) {
        if (item.first == key) {
            item.second = std::move(value);
            return;
        }
    }
    items_.emplace_back(std::move(key), std::move(value));
    std::sort(items_.begin(), items_.end());
}

const std::string* Features::get(std::string_view key) const {
    for (const auto& item : items_)
        if (item.first == key) return &item.second;
    return nullptr;
}

bool Features::has(std::string_view key, std::string_view value) const {
    const std::string* found = get(key);
    return found != nullptr && *found == value;
}

bool Features::satisfies(const Features& constraint) const {
    for (const auto& [key, alternatives] : constraint.items()) {
        const std::string* mine = get(key);
        if (mine == nullptr) return false;
        bool ok = false;
        std::size_t begin = 0;
        while (begin <= alternatives.size()) {
            const std::size_t bar = alternatives.find('|', begin);
            const std::size_t end = bar == std::string::npos ? alternatives.size() : bar;
            if (alternatives.compare(begin, end - begin, *mine) == 0) ok = true;
            if (bar == std::string::npos) break;
            begin = bar + 1;
        }
        if (!ok) return false;
    }
    return true;
}

Features Features::parse(std::string_view text) {
    Features result;
    for (const std::string& piece : split_ws(text)) {
        if (piece == "-") continue;
        const std::size_t eq = piece.find('=');
        if (eq == std::string::npos) {
            result.set(piece, "yes");
        } else {
            result.set(piece.substr(0, eq), piece.substr(eq + 1));
        }
    }
    return result;
}

std::string Features::str() const {
    std::string out;
    for (const auto& [key, value] : items_) {
        if (!out.empty()) out += ' ';
        out += key;
        out += '=';
        out += value;
    }
    return out.empty() ? std::string("-") : out;
}

std::vector<std::string> split_ws(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const std::size_t begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > begin) parts.emplace_back(text.substr(begin, i - begin));
    }
    return parts;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

std::string to_lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 128) c = static_cast<char>(std::tolower(uc));
    }
    return out;
}

std::string data_path(std::string_view file) {
    const char* from_env = std::getenv("LEXICON_DATA_DIR");
    std::string dir = from_env != nullptr ? std::string(from_env) : std::string(LEXICON_DATA_DIR);
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir + std::string(file);
}

std::vector<std::string> read_config_lines(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open data file: " + path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const std::string trimmed = trim(line);
        if (!trimmed.empty()) lines.push_back(trimmed);
    }
    return lines;
}

}  // namespace lex
