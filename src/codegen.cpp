#include "lexicon/codegen.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace lex {
namespace {

void flatten(const FormulaRef& formula, std::vector<FormulaRef>& parts) {
    if (!formula) return;
    if (formula->kind == Formula::Kind::And) {
        for (const FormulaRef& sub : formula->subs) flatten(sub, parts);
        return;
    }
    parts.push_back(formula);
}

// Variables and entities mentioned by the atoms of one level, without
// descending into a negation or a quantifier: those open their own scope and
// declare their own tables.
void mentioned(const std::vector<FormulaRef>& parts, std::vector<int>& vars,
               std::vector<std::string>& entities) {
    for (const FormulaRef& part : parts) {
        if (part->kind != Formula::Kind::Atom) continue;
        for (const Term& term : part->atom.args) {
            if (term.kind == Term::Kind::Variable) {
                if (std::find(vars.begin(), vars.end(), term.var) == vars.end())
                    vars.push_back(term.var);
            } else if (term.kind == Term::Kind::Entity) {
                if (std::find(entities.begin(), entities.end(), term.entity) == entities.end())
                    entities.push_back(term.entity);
            }
        }
    }
}

std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) out += separator;
        out += item;
    }
    return out;
}

// ---------------------------------------------------------------------------
// SQL
// ---------------------------------------------------------------------------

class SqlWriter {
public:
    SqlWriter(const Schema& schema, const Query& query) : schema_(schema), query_(query) {}

    std::string run() {
        std::vector<FormulaRef> parts;
        flatten(query_.body, parts);

        std::vector<std::string> from;
        std::vector<std::string> conditions;
        open_scope(parts, from, conditions, false);

        std::string select;
        if (query_.boolean) {
            std::string inner = "SELECT 1";
            if (!from.empty()) inner += "\n  FROM " + join(from, ", ");
            if (!conditions.empty()) inner += "\n  WHERE " + join(conditions, "\n    AND ");
            return "SELECT EXISTS (\n  " + inner + "\n) AS answer;";
        }
        if (query_.result_vars.empty()) throw std::runtime_error("the query projects nothing");
        const int result = query_.result_vars.front();
        const std::string column = label_column(result);
        select = query_.count ? "SELECT COUNT(DISTINCT " + key_reference(result) + ") AS answer"
                              : "SELECT DISTINCT " + column + " AS answer";

        std::string sql = select;
        if (!from.empty()) sql += "\nFROM " + join(from, ", ");
        if (!conditions.empty()) sql += "\nWHERE " + join(conditions, "\n  AND ");
        if (!query_.count) sql += "\nORDER BY 1";
        return sql + ";";
    }

private:
    const Schema& schema_;
    const Query& query_;
    std::map<int, std::string> var_alias_;
    std::map<std::string, std::string> entity_alias_;
    int counter_ = 0;

    const TypeSpec& type_of_var(int var) const {
        const auto found = query_.var_types.find(var);
        if (found == query_.var_types.end())
            throw std::runtime_error("variable without a type in the logical form");
        const TypeSpec* spec = schema_.type(found->second);
        if (spec == nullptr) throw std::runtime_error("unknown type " + found->second);
        if (spec->scalar)
            throw std::runtime_error("a value type cannot be a variable in the SQL generator");
        return *spec;
    }

    std::string key_reference(int var) const {
        return var_alias_.at(var) + "." + type_of_var(var).key;
    }

    std::string label_column(int var) const {
        return var_alias_.at(var) + "." + type_of_var(var).label;
    }

    // The column that stands for a term when it is used as a value.
    std::string reference(const Term& term) const {
        switch (term.kind) {
            case Term::Kind::Variable: return key_reference(term.var);
            case Term::Kind::Entity: return entity_alias_.at(term.entity) + ".id";
            case Term::Kind::Number: return std::to_string(term.number);
        }
        return "NULL";
    }

    // The alias of the table a term lives in, needed by attribute relations.
    std::string owner(const Term& term) const {
        if (term.kind == Term::Kind::Variable) return var_alias_.at(term.var);
        if (term.kind == Term::Kind::Entity) return entity_alias_.at(term.entity);
        throw std::runtime_error("a number has no table of its own");
    }

    // `nested` says whether the aliases declared here go out of scope again. The
    // outermost scope keeps them, because the projection is written afterwards.
    void open_scope(const std::vector<FormulaRef>& parts, std::vector<std::string>& from,
                    std::vector<std::string>& conditions, bool nested) {
        std::vector<int> vars;
        std::vector<std::string> entities;
        mentioned(parts, vars, entities);

        // Declare what this level introduces; anything already declared belongs
        // to an enclosing scope and is referenced, which is what makes a nested
        // NOT EXISTS correlated.
        std::vector<int> declared_vars;
        std::vector<std::string> declared_entities;
        for (const int var : vars) {
            if (var_alias_.count(var) != 0) continue;
            const TypeSpec& spec = type_of_var(var);
            const std::string alias = "t" + std::to_string(counter_++);
            var_alias_.emplace(var, alias);
            declared_vars.push_back(var);
            from.push_back(spec.table + " AS " + alias);
        }
        for (const std::string& entity : entities) {
            if (entity_alias_.count(entity) != 0) continue;
            const std::string alias = "e" + std::to_string(counter_++);
            entity_alias_.emplace(entity, alias);
            declared_entities.push_back(entity);
            from.push_back(entity_table(entity) + " AS " + alias);
            conditions.push_back(alias + ".id = '" + entity + "'");
        }

        for (const FormulaRef& part : parts) render(part, from, conditions);

        if (!nested) return;
        for (const int var : declared_vars) var_alias_.erase(var);
        for (const std::string& entity : declared_entities) entity_alias_.erase(entity);
    }

    // An entity constant carries its type in the atoms that mention it. The
    // knowledge base is not available here, so the type is read off the relation
    // the entity appears in, which the schema does know.
    std::string entity_table(const std::string& entity) const {
        const std::string type = entity_type(query_.body, entity);
        const TypeSpec* spec = schema_.type(type);
        if (spec == nullptr) throw std::runtime_error("cannot type the entity " + entity);
        return spec->table;
    }

    std::string entity_type(const FormulaRef& formula, const std::string& entity) const {
        if (!formula) return {};
        if (formula->kind == Formula::Kind::Atom) {
            const Atom& atom = formula->atom;
            if (atom.args.size() == 1 && atom.args[0].kind == Term::Kind::Entity &&
                atom.args[0].entity == entity)
                return atom.relation;
            const RelationSpec* spec = schema_.relation(atom.relation);
            if (spec == nullptr) return {};
            for (std::size_t i = 0; i < atom.args.size() && i < spec->args.size(); ++i)
                if (atom.args[i].kind == Term::Kind::Entity && atom.args[i].entity == entity)
                    return spec->args[i];
            return {};
        }
        for (const FormulaRef& sub : formula->subs) {
            const std::string found = entity_type(sub, entity);
            if (!found.empty()) return found;
        }
        return {};
    }

    void render(const FormulaRef& part, std::vector<std::string>& from,
                std::vector<std::string>& conditions) {
        switch (part->kind) {
            case Formula::Kind::Atom: {
                const Atom& atom = part->atom;
                if (atom.args.size() == 1) return;  // the type is carried by the table
                const RelationSpec* spec = schema_.relation(atom.relation);
                if (spec == nullptr)
                    throw std::runtime_error("no relation " + atom.relation + " in the schema");
                if (spec->store == "link") {
                    const std::string alias = "r" + std::to_string(counter_++);
                    from.push_back(spec->table + " AS " + alias);
                    conditions.push_back(alias + "." + spec->cols.at(0) + " = " +
                                         reference(atom.args.at(0)));
                    conditions.push_back(alias + "." + spec->cols.at(1) + " = " +
                                         reference(atom.args.at(1)));
                } else {
                    conditions.push_back(owner(atom.args.at(0)) + "." + spec->cols.at(1) + " = " +
                                         reference(atom.args.at(1)));
                }
                return;
            }
            case Formula::Kind::Not: {
                conditions.push_back("NOT EXISTS (" + subquery(part->subs.at(0)) + ")");
                return;
            }
            case Formula::Kind::Forall: {
                // Relational division: there is no witness of the restriction
                // that fails the body.
                std::vector<FormulaRef> restriction;
                flatten(part->subs.at(0), restriction);
                std::vector<std::string> inner_from;
                std::vector<std::string> inner_conditions;

                std::vector<int> vars;
                std::vector<std::string> entities;
                mentioned(restriction, vars, entities);
                std::vector<int> declared_vars;
                std::vector<std::string> declared_entities;
                for (const int var : vars) {
                    if (var_alias_.count(var) != 0) continue;
                    const std::string alias = "t" + std::to_string(counter_++);
                    var_alias_.emplace(var, alias);
                    declared_vars.push_back(var);
                    inner_from.push_back(type_of_var(var).table + " AS " + alias);
                }
                for (const std::string& entity : entities) {
                    if (entity_alias_.count(entity) != 0) continue;
                    const std::string alias = "e" + std::to_string(counter_++);
                    entity_alias_.emplace(entity, alias);
                    declared_entities.push_back(entity);
                    inner_from.push_back(entity_table(entity) + " AS " + alias);
                    inner_conditions.push_back(alias + ".id = '" + entity + "'");
                }
                for (const FormulaRef& sub : restriction) render(sub, inner_from, inner_conditions);
                inner_conditions.push_back("NOT EXISTS (" + subquery(part->subs.at(1)) + ")");

                std::string text = "SELECT 1";
                if (!inner_from.empty()) text += " FROM " + join(inner_from, ", ");
                if (!inner_conditions.empty()) text += " WHERE " + join(inner_conditions, " AND ");
                conditions.push_back("NOT EXISTS (" + text + ")");

                for (const int var : declared_vars) var_alias_.erase(var);
                for (const std::string& entity : declared_entities) entity_alias_.erase(entity);
                return;
            }
            case Formula::Kind::And: {
                std::vector<FormulaRef> subs;
                flatten(part, subs);
                for (const FormulaRef& sub : subs) render(sub, from, conditions);
                return;
            }
        }
    }

    std::string subquery(const FormulaRef& formula) {
        std::vector<FormulaRef> parts;
        flatten(formula, parts);
        std::vector<std::string> from;
        std::vector<std::string> conditions;
        open_scope(parts, from, conditions, true);
        std::string text = "SELECT 1";
        if (!from.empty()) text += " FROM " + join(from, ", ");
        if (!conditions.empty()) text += " WHERE " + join(conditions, " AND ");
        return text;
    }
};

// ---------------------------------------------------------------------------
// SPARQL
// ---------------------------------------------------------------------------

class SparqlWriter {
public:
    SparqlWriter(const Schema& schema, const Query& query) : schema_(schema), query_(query) {}

    std::string run() const {
        const std::string patterns = block(query_.body, "  ");
        std::string head;
        if (query_.boolean) {
            head = "ASK";
        } else if (query_.count) {
            head = "SELECT (COUNT(DISTINCT " + term(Term::variable(query_.result_vars.front())) +
                   ") AS ?answer)";
        } else {
            head = "SELECT DISTINCT " + term(Term::variable(query_.result_vars.front()));
        }
        std::string text = "PREFIX : <http://example.org/film#>\n" + head + " WHERE {\n" + patterns +
                           "}";
        if (!query_.boolean && !query_.count)
            text += "\nORDER BY " + term(Term::variable(query_.result_vars.front()));
        return text;
    }

private:
    const Schema& schema_;
    const Query& query_;

    std::string term(const Term& value) const {
        switch (value.kind) {
            case Term::Kind::Variable: return "?x" + std::to_string(value.var);
            case Term::Kind::Entity: return ":" + value.entity;
            case Term::Kind::Number: return std::to_string(value.number);
        }
        return "?";
    }

    std::string block(const FormulaRef& formula, const std::string& indent) const {
        std::vector<FormulaRef> parts;
        flatten(formula, parts);
        std::string out;
        for (const FormulaRef& part : parts) {
            switch (part->kind) {
                case Formula::Kind::Atom: {
                    const Atom& atom = part->atom;
                    if (atom.args.size() == 1) {
                        const TypeSpec* spec = schema_.type(atom.relation);
                        out += indent + term(atom.args[0]) + " a :" +
                               (spec != nullptr ? spec->iri : atom.relation) + " .\n";
                        break;
                    }
                    const RelationSpec* spec = schema_.relation(atom.relation);
                    if (spec == nullptr)
                        throw std::runtime_error("no relation " + atom.relation + " in the schema");
                    out += indent + term(atom.args.at(0)) + " :" + spec->iri + " " +
                           term(atom.args.at(1)) + " .\n";
                    break;
                }
                case Formula::Kind::Not:
                    out += indent + "FILTER NOT EXISTS {\n" + block(part->subs.at(0), indent + "  ") +
                           indent + "}\n";
                    break;
                case Formula::Kind::Forall:
                    out += indent + "FILTER NOT EXISTS {\n" +
                           block(part->subs.at(0), indent + "  ") + indent +
                           "  FILTER NOT EXISTS {\n" + block(part->subs.at(1), indent + "    ") +
                           indent + "  }\n" + indent + "}\n";
                    break;
                case Formula::Kind::And:
                    out += block(part, indent);
                    break;
            }
        }
        return out;
    }
};

std::string quote(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out;
}

}  // namespace

std::string to_sql(const Schema& schema, const Query& query) {
    return SqlWriter(schema, query).run();
}

std::string to_sparql(const Schema& schema, const Query& query) {
    return SparqlWriter(schema, query).run();
}

std::string sql_schema_and_data(const Schema& schema, const KnowledgeBase& kb) {
    std::string out;
    // Entity tables carry their own attribute columns, so the attribute
    // relations are collected first and written as columns.
    std::map<std::string, std::vector<const RelationSpec*>> attributes;
    for (const RelationSpec& spec : schema.relations())
        if (spec.store == "attr") attributes[spec.args.at(0)].push_back(&spec);

    for (const TypeSpec& type : schema.types()) {
        if (type.scalar) continue;
        out += "CREATE TABLE " + type.table + " (\n  " + type.key + " TEXT PRIMARY KEY,\n  " +
               type.label + " TEXT NOT NULL";
        for (const RelationSpec* spec : attributes[type.name]) {
            const TypeSpec* value = schema.type(spec->args.at(1));
            out += ",\n  " + spec->cols.at(1) + (value != nullptr && value->scalar ? " INTEGER" : " TEXT");
        }
        out += "\n);\n";
    }
    for (const RelationSpec& spec : schema.relations()) {
        if (spec.store != "link") continue;
        out += "CREATE TABLE " + spec.table + " (\n  " + spec.cols.at(0) + " TEXT NOT NULL,\n  " +
               spec.cols.at(1) + " TEXT NOT NULL\n);\n";
    }

    for (const TypeSpec& type : schema.types()) {
        if (type.scalar) continue;
        for (const std::string& id : kb.entities_of_type(type.name)) {
            std::vector<std::string> columns{type.key, type.label};
            const std::string* label = kb.label_of(id);
            std::vector<std::string> values{"'" + quote(id) + "'",
                                            "'" + quote(label != nullptr ? *label : id) + "'"};
            for (const RelationSpec* spec : attributes[type.name]) {
                std::string value = "NULL";
                const std::vector<std::vector<Value>>* tuples = kb.extension(spec->name);
                if (tuples != nullptr) {
                    for (const std::vector<Value>& tuple : *tuples) {
                        if (tuple.size() != 2 || tuple[0].id != id) continue;
                        value = tuple[1].numeric ? tuple[1].str() : "'" + quote(tuple[1].id) + "'";
                        break;
                    }
                }
                columns.push_back(spec->cols.at(1));
                values.push_back(value);
            }
            out += "INSERT INTO " + type.table + " (" + join(columns, ", ") + ") VALUES (" +
                   join(values, ", ") + ");\n";
        }
    }
    for (const RelationSpec& spec : schema.relations()) {
        if (spec.store != "link") continue;
        const std::vector<std::vector<Value>>* tuples = kb.extension(spec.name);
        if (tuples == nullptr) continue;
        for (const std::vector<Value>& tuple : *tuples) {
            out += "INSERT INTO " + spec.table + " (" + spec.cols.at(0) + ", " + spec.cols.at(1) +
                   ") VALUES ('" + quote(tuple.at(0).str()) + "', '" + quote(tuple.at(1).str()) +
                   "');\n";
        }
    }
    return out;
}

std::string turtle_data(const Schema& schema, const KnowledgeBase& kb) {
    std::string out = "@prefix : <http://example.org/film#> .\n";
    out += "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n";
    for (const auto& entry : kb.types()) {
        const TypeSpec* type = schema.type(entry.first);
        if (type == nullptr) continue;
        for (const std::string& id : entry.second) {
            const std::string* label = kb.label_of(id);
            out += ":" + id + " a :" + type->iri + " ; rdfs:label \"" +
                   (label != nullptr ? *label : id) + "\" .\n";
        }
    }
    out += "\n";
    for (const RelationSpec& spec : schema.relations()) {
        const std::vector<std::vector<Value>>* tuples = kb.extension(spec.name);
        if (tuples == nullptr) continue;
        for (const std::vector<Value>& tuple : *tuples) {
            if (tuple.size() != 2) continue;
            const std::string object =
                tuple[1].numeric ? tuple[1].str() : ":" + tuple[1].id;
            out += ":" + tuple[0].id + " :" + spec.iri + " " + object + " .\n";
        }
    }
    return out;
}

}  // namespace lex
