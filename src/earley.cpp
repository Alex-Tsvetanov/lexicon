#include "lexicon/earley.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <tuple>

namespace lex {
namespace {

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t sum = a + b;
    return sum >= ParseResult::kCountLimit || sum < a ? ParseResult::kCountLimit : sum;
}

std::uint64_t saturating_mul(std::uint64_t a, std::uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > ParseResult::kCountLimit / b) return ParseResult::kCountLimit;
    return a * b;
}

}  // namespace

std::vector<Token> tokenize(const Morphology& morph, std::string_view text) {
    std::vector<Token> tokens;
    std::string current;
    auto flush = [&]() {
        if (current.empty()) return;
        Token token;
        token.text = current;
        token.readings = morph.analyze(current);
        if (token.readings.empty()) {
            const bool numeric = std::all_of(current.begin(), current.end(), [](char c) {
                return c >= '0' && c <= '9';
            });
            if (numeric) {
                Reading reading;
                reading.lemma = current;
                reading.pos = "num";
                reading.feats = Features::parse("type=Year");
                token.readings.push_back(std::move(reading));
            } else {
                token.readings = morph.guess(current);
            }
        }
        tokens.push_back(std::move(token));
        current.clear();
    };
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        const bool word_character =
            byte >= 128 || std::isalnum(byte) != 0 || c == 0x27;
        if (word_character) {
            current += c;
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

ParseResult Parser::parse(const std::vector<Token>& tokens) const {
    const Grammar& grammar = *grammar_;
    const int length = static_cast<int>(tokens.size());

    ParseResult result;
    result.grammar_ = &grammar;
    std::vector<std::vector<int>> chart(static_cast<std::size_t>(length) + 1);
    std::map<std::tuple<int, int, int, int>, int> item_index;  // end, rule, dot, start
    std::map<std::tuple<int, int, int>, int> node_index;       // symbol, start, end

    auto node_for = [&](int symbol, int start, int end, bool terminal, int token,
                        int reading) -> int {
        const auto key = std::make_tuple(symbol, start, end);
        const auto found = node_index.find(key);
        if (found != node_index.end()) return found->second;
        ForestNode node;
        node.symbol = symbol;
        node.start = start;
        node.end = end;
        node.terminal = terminal;
        node.token = token;
        node.reading = reading;
        result.nodes_.push_back(std::move(node));
        const int id = static_cast<int>(result.nodes_.size()) - 1;
        node_index.emplace(key, id);
        return id;
    };

    auto add_item = [&](int rule, int dot, int start, int end, bool has_link, Link link) {
        const auto key = std::make_tuple(end, rule, dot, start);
        const auto found = item_index.find(key);
        int id;
        if (found == item_index.end()) {
            Item item;
            item.rule = rule;
            item.dot = dot;
            item.start = start;
            item.end = end;
            result.items_.push_back(std::move(item));
            id = static_cast<int>(result.items_.size()) - 1;
            item_index.emplace(key, id);
            chart[static_cast<std::size_t>(end)].push_back(id);
        } else {
            id = found->second;
        }
        if (!has_link) return;
        // The same link can be offered twice, once per production that builds the
        // child node. Keeping it once is what stops the derivation count from
        // counting the same reading more than once.
        std::vector<Link>& links = result.items_[static_cast<std::size_t>(id)].links;
        for (const Link& existing : links)
            if (existing.predecessor == link.predecessor && existing.child == link.child) return;
        links.push_back(link);
    };

    for (const int rule : grammar.rules_for(grammar.start()))
        add_item(rule, 0, 0, 0, false, Link{});

    for (int position = 0; position <= length; ++position) {
        for (std::size_t cursor = 0; cursor < chart[static_cast<std::size_t>(position)].size();
             ++cursor) {
            const int item_id = chart[static_cast<std::size_t>(position)][cursor];
            const int rule_id = result.items_[static_cast<std::size_t>(item_id)].rule;
            const int dot = result.items_[static_cast<std::size_t>(item_id)].dot;
            const int start = result.items_[static_cast<std::size_t>(item_id)].start;
            const Rule& rule = grammar.rules()[static_cast<std::size_t>(rule_id)];

            if (static_cast<std::size_t>(dot) < rule.rhs.size()) {
                const int expected = rule.rhs[static_cast<std::size_t>(dot)];
                if (grammar.is_terminal(expected)) {
                    if (position == length) continue;
                    const Token& token = tokens[static_cast<std::size_t>(position)];
                    int reading = -1;
                    for (std::size_t r = 0; r < token.readings.size() && reading < 0; ++r)
                        if (grammar.accepts(expected, token.readings[r]))
                            reading = static_cast<int>(r);
                    if (reading < 0) continue;
                    const int node = node_for(expected, position, position + 1, true, position,
                                              reading);
                    add_item(rule_id, dot + 1, start, position + 1, true, Link{item_id, node});
                } else {
                    for (const int predicted : grammar.rules_for(expected))
                        add_item(predicted, 0, position, position, false, Link{});
                }
                continue;
            }

            // Completion. Productions are never empty, so start < position holds
            // and the chart position being read is already closed.
            const int node = node_for(rule.lhs, start, position, false, -1, -1);
            std::vector<int>& node_items = result.nodes_[static_cast<std::size_t>(node)].items;
            if (std::find(node_items.begin(), node_items.end(), item_id) == node_items.end())
                node_items.push_back(item_id);

            const std::vector<int>& waiting = chart[static_cast<std::size_t>(start)];
            for (std::size_t w = 0; w < waiting.size(); ++w) {
                const int waiting_id = waiting[w];
                const Item& candidate = result.items_[static_cast<std::size_t>(waiting_id)];
                const Rule& other = grammar.rules()[static_cast<std::size_t>(candidate.rule)];
                if (static_cast<std::size_t>(candidate.dot) >= other.rhs.size()) continue;
                if (other.rhs[static_cast<std::size_t>(candidate.dot)] != rule.lhs) continue;
                add_item(candidate.rule, candidate.dot + 1, candidate.start, position, true,
                         Link{waiting_id, node});
            }
        }
    }

    const auto root = node_index.find(std::make_tuple(grammar.start(), 0, length));
    if (root != node_index.end() && !result.nodes_[static_cast<std::size_t>(root->second)].items.empty()) {
        result.root_ = root->second;

        // Derivations are counted on the forest, never by enumerating trees.
        std::vector<std::uint64_t> item_counts(result.items_.size(), 0);
        std::vector<char> item_done(result.items_.size(), 0);
        std::vector<std::uint64_t> node_counts(result.nodes_.size(), 0);
        std::vector<char> node_done(result.nodes_.size(), 0);

        std::function<std::uint64_t(int)> count_item;
        std::function<std::uint64_t(int)> count_node;

        count_item = [&](int id) -> std::uint64_t {
            const std::size_t index = static_cast<std::size_t>(id);
            if (item_done[index] == 2) return item_counts[index];
            if (item_done[index] == 1) return 0;  // guard, the grammar has no cycles
            item_done[index] = 1;
            const Item& item = result.items_[index];
            std::uint64_t total = 0;
            if (item.dot == 0) {
                total = 1;
            } else {
                for (const Link& link : item.links)
                    total = saturating_add(total, saturating_mul(count_item(link.predecessor),
                                                                 count_node(link.child)));
            }
            item_counts[index] = total;
            item_done[index] = 2;
            return total;
        };
        count_node = [&](int id) -> std::uint64_t {
            const std::size_t index = static_cast<std::size_t>(id);
            if (node_done[index] == 2) return node_counts[index];
            if (node_done[index] == 1) return 0;
            node_done[index] = 1;
            const ForestNode& node = result.nodes_[index];
            std::uint64_t total = 0;
            if (node.terminal) {
                total = 1;
            } else {
                for (const int item : node.items) total = saturating_add(total, count_item(item));
            }
            node_counts[index] = total;
            node_done[index] = 2;
            return total;
        };
        result.derivations_ = count_node(result.root_);
    }
    return result;
}

std::size_t ParseResult::ambiguity_points() const {
    std::size_t total = 0;
    for (const ForestNode& node : nodes_)
        if (node.items.size() > 1) ++total;
    for (const Item& item : items_)
        if (item.links.size() > 1) ++total;
    return total;
}

std::string ParseResult::describe_node(const Grammar& grammar, int node) const {
    const ForestNode& record = nodes_.at(static_cast<std::size_t>(node));
    std::string out = grammar.name(record.symbol) + "[" + std::to_string(record.start) + "," +
                      std::to_string(record.end) + "]";
    if (record.terminal) return out;
    out += " {";
    for (std::size_t i = 0; i < record.items.size(); ++i) {
        if (i > 0) out += " | ";
        out += grammar.rule_text(items_[static_cast<std::size_t>(record.items[i])].rule);
    }
    out += "}";
    return out;
}

std::vector<Tree> ParseResult::enumerate(std::size_t limit) const {
    if (root_ < 0 || limit == 0) return {};

    std::function<std::vector<Tree>(int)> from_node;
    std::function<std::vector<std::vector<Tree>>(int)> from_item;

    from_item = [&](int id) -> std::vector<std::vector<Tree>> {
        const Item& item = items_[static_cast<std::size_t>(id)];
        if (item.dot == 0) return {{}};
        std::vector<std::vector<Tree>> result;
        for (const Link& link : item.links) {
            const std::vector<std::vector<Tree>> prefixes = from_item(link.predecessor);
            const std::vector<Tree> children = from_node(link.child);
            for (const std::vector<Tree>& prefix : prefixes) {
                for (const Tree& child : children) {
                    if (result.size() >= limit) return result;
                    std::vector<Tree> extended = prefix;
                    extended.push_back(child);
                    result.push_back(std::move(extended));
                }
            }
        }
        return result;
    };

    from_node = [&](int id) -> std::vector<Tree> {
        const ForestNode& node = nodes_[static_cast<std::size_t>(id)];
        if (node.terminal) {
            Tree leaf;
            leaf.symbol = node.symbol;
            leaf.token = node.token;
            leaf.reading = node.reading;
            return {leaf};
        }
        std::vector<Tree> result;
        for (const int item : node.items) {
            for (std::vector<Tree>& children : from_item(item)) {
                if (result.size() >= limit) return result;
                Tree tree;
                tree.symbol = node.symbol;
                tree.rule = items_[static_cast<std::size_t>(item)].rule;
                tree.children = std::move(children);
                result.push_back(std::move(tree));
            }
        }
        return result;
    };

    return from_node(root_);
}

}  // namespace lex
