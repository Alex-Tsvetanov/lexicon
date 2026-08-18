#include "lexicon/morph.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace lex {
namespace {

// One paradigm form: drop `deletions` bytes off the stem, then append `suffix`.
struct ParadigmForm {
    int deletions = 0;
    std::string suffix;
    Features feats;
};

// Affix syntax: "+s", "~1+ies", and "+" or "-" for the bare stem. The deletion
// count keeps orthographic alternation in the data file instead of in the code,
// so a new language needs a new lexicon and no new C++.
ParadigmForm parse_affix(const std::string& text) {
    ParadigmForm form;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '~') {
        ++i;
        int digits = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            digits = digits * 10 + (text[i] - '0');
            ++i;
        }
        form.deletions = digits;
    }
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
    form.suffix = text.substr(i);
    return form;
}

// Deletion counts characters, not bytes. The English lexicon would not notice the
// difference, the Bulgarian one would: a Cyrillic letter is two bytes in UTF-8 and
// a byte wise deletion would cut a letter in half.
std::string drop_last_chars(const std::string& text, int count) {
    std::size_t end = text.size();
    for (int i = 0; i < count; ++i) {
        if (end == 0) throw std::runtime_error("affix deletes more than the stem: " + text);
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    }
    return text.substr(0, end);
}

std::string apply_affix(const std::string& stem, const ParadigmForm& form) {
    return drop_last_chars(stem, form.deletions) + form.suffix;
}

}  // namespace

std::string Reading::str() const {
    std::string out = lemma + "/" + pos;
    if (!feats.empty()) out += "[" + feats.str() + "]";
    if (guessed) out += "?";
    return out;
}

bool Reading::operator==(const Reading& other) const {
    return lemma == other.lemma && pos == other.pos && feats == other.feats &&
           guessed == other.guessed;
}

const LexEntry* Morphology::entry(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) return nullptr;
    return &entries_[static_cast<std::size_t>(index)];
}

std::size_t Morphology::edge_count() const {
    std::size_t total = 0;
    for (const State& state : states_) total += state.edges.size();
    return total;
}

int Morphology::add_path(std::string_view surface) {
    int current = 0;
    for (const char c : surface) {
        int next = -1;
        for (const auto& edge : states_[static_cast<std::size_t>(current)].edges)
            if (edge.first == c) next = edge.second;
        if (next < 0) {
            states_.push_back(State{});
            next = static_cast<int>(states_.size()) - 1;
            states_[static_cast<std::size_t>(current)].edges.emplace_back(c, next);
        }
        current = next;
    }
    return current;
}

void Morphology::add_form(const std::string& surface, const Reading& reading) {
    const int state = add_path(to_lower_ascii(surface));
    outputs_.push_back(reading);
    states_[static_cast<std::size_t>(state)].outputs.push_back(
        static_cast<int>(outputs_.size()) - 1);
    ++form_count_;
}

Morphology Morphology::load(const std::string& path) {
    Morphology morph;
    std::map<std::string, std::vector<ParadigmForm>> paradigms;
    std::string current_paradigm;

    for (const std::string& line : read_config_lines(path)) {
        const std::vector<std::string> parts = split_ws(line);
        if (parts[0] == "PARADIGM") {
            if (parts.size() < 2) throw std::runtime_error("PARADIGM needs a name: " + line);
            current_paradigm = parts[1];
            paradigms[current_paradigm];
        } else if (parts[0] == "FORM") {
            if (current_paradigm.empty()) throw std::runtime_error("FORM outside PARADIGM");
            if (parts.size() < 2) throw std::runtime_error("FORM needs an affix: " + line);
            ParadigmForm form = parse_affix(parts[1]);
            std::string rest;
            for (std::size_t i = 2; i < parts.size(); ++i) rest += parts[i] + " ";
            form.feats = Features::parse(rest);
            paradigms[current_paradigm].push_back(std::move(form));
        } else if (parts[0] == "LEX") {
            if (parts.size() < 5) throw std::runtime_error("LEX needs four fields: " + line);
            LexEntry entry;
            entry.stem = parts[1];
            entry.lemma = parts[2];
            entry.pos = parts[3];
            entry.paradigm = parts[4];
            std::string rest;
            for (std::size_t i = 5; i < parts.size(); ++i) rest += parts[i] + " ";
            entry.sem = Features::parse(rest);
            morph.entries_.push_back(std::move(entry));
        } else {
            throw std::runtime_error("unknown directive in lexicon: " + line);
        }
    }

    // Composition of the lexicon with the affix relation of each paradigm.
    for (std::size_t index = 0; index < morph.entries_.size(); ++index) {
        const LexEntry& entry = morph.entries_[index];
        const auto found = paradigms.find(entry.paradigm);
        if (found == paradigms.end())
            throw std::runtime_error("lexeme " + entry.lemma + " uses unknown paradigm " +
                                     entry.paradigm);
        for (const ParadigmForm& form : found->second) {
            const std::string surface = apply_affix(entry.stem, form);
            Reading reading;
            reading.lemma = entry.lemma;
            reading.pos = entry.pos;
            reading.feats = form.feats;
            reading.entry = static_cast<int>(index);
            morph.add_form(surface, reading);
            morph.generation_.push_back(GenForm{entry.lemma, entry.pos, form.feats, surface});
        }
    }

    // Affix hints for unknown words: every non empty suffix used by a paradigm,
    // paired with the parts of speech that actually inflect by that paradigm.
    std::map<std::string, std::vector<std::string>> pos_by_paradigm;
    for (const LexEntry& entry : morph.entries_) {
        std::vector<std::string>& list = pos_by_paradigm[entry.paradigm];
        if (std::find(list.begin(), list.end(), entry.pos) == list.end())
            list.push_back(entry.pos);
    }
    for (const auto& paradigm : paradigms) {
        for (const ParadigmForm& form : paradigm.second) {
            if (form.suffix.empty()) continue;
            for (const std::string& pos : pos_by_paradigm[paradigm.first])
                morph.hints_.push_back(AffixHint{form.suffix, pos, form.feats});
        }
    }
    std::stable_sort(morph.hints_.begin(), morph.hints_.end(),
                     [](const AffixHint& a, const AffixHint& b) {
                         return a.suffix.size() > b.suffix.size();
                     });
    return morph;
}

std::vector<Reading> Morphology::analyze(std::string_view form) const {
    const std::string key = to_lower_ascii(form);
    int current = 0;
    for (const char c : key) {
        int next = -1;
        for (const auto& edge : states_[static_cast<std::size_t>(current)].edges)
            if (edge.first == c) next = edge.second;
        if (next < 0) return {};
        current = next;
    }
    std::vector<Reading> result;
    for (const int output : states_[static_cast<std::size_t>(current)].outputs)
        result.push_back(outputs_[static_cast<std::size_t>(output)]);
    return result;
}

std::vector<Reading> Morphology::guess(std::string_view form) const {
    const std::string key = to_lower_ascii(form);
    std::vector<Reading> result;
    std::size_t best = 0;
    for (const AffixHint& hint : hints_) {
        if (hint.suffix.size() > key.size()) continue;
        if (key.compare(key.size() - hint.suffix.size(), hint.suffix.size(), hint.suffix) != 0)
            continue;
        if (!result.empty() && hint.suffix.size() < best) break;
        best = hint.suffix.size();
        Reading reading;
        reading.lemma = key.substr(0, key.size() - hint.suffix.size());
        reading.pos = hint.pos;
        reading.feats = hint.feats;
        reading.guessed = true;
        if (std::find(result.begin(), result.end(), reading) == result.end())
            result.push_back(std::move(reading));
    }
    if (result.empty()) {
        Reading reading;
        reading.lemma = key;
        reading.pos = "noun";
        reading.feats = Features::parse("num=sg");
        reading.guessed = true;
        result.push_back(std::move(reading));
    }
    return result;
}

std::optional<std::string> Morphology::generate(const std::string& lemma, const std::string& pos,
                                                const Features& wanted) const {
    for (const GenForm& form : generation_) {
        if (form.lemma != lemma) continue;
        if (!pos.empty() && form.pos != pos) continue;
        if (!form.feats.satisfies(wanted)) continue;
        return form.surface;
    }
    return std::nullopt;
}

}  // namespace lex
