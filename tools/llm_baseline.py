#!/usr/bin/env python3
"""Baseline comparison: a local language model against the rule based pipeline.

Why a local model and not a hosted one. The report deferred this comparison for
one stated reason: it needs a fixed model, a fixed version and fixed settings,
because without them the number is not reproducible. A hosted endpoint cannot
give that, it changes underneath you and the old behaviour is gone. A model
pulled by digest and run at temperature zero with a fixed seed can, so the
local model is not a substitute for the intended experiment, it is the version
of it that satisfies the condition the report set.

What is compared. Both sides are asked for one thing: SQL that answers the
question against the bundled schema. The comparison is by execution, not by
string. Two different queries that return the same rows are both right, and an
exact match criterion would have scored correct answers as wrong.

What this cannot show. The model here is small enough to fit a 6 GB card. A
result against it says what one small quantised local model does on this
workload. It says nothing about what a frontier model would do, and the write
up must not claim otherwise.

Usage:
    python tools/llm_baseline.py --model qwen2.5-coder:7b
    python tools/llm_baseline.py --model qwen2.5-coder:7b --out docs/measurements/llm_baseline.txt
"""
import argparse
import json
import re
import sqlite3
import subprocess
import sys
import urllib.request

OLLAMA = "http://127.0.0.1:11434/api/generate"


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if p.returncode != 0:
        sys.exit("command failed: %s\n%s" % (" ".join(cmd), p.stderr[:400]))
    return p.stdout


def load_machine_output(binary):
    """Parse `lexicon --machine` into [(question, answer, sql)]."""
    text = run([binary, "--machine"])
    out, q, ans, sql, in_sql = [], None, None, [], False
    for line in text.splitlines():
        if line.startswith("@question "):
            q, ans, sql, in_sql = line[10:].strip(), None, [], False
        elif line.startswith("@answer "):
            ans = line[8:].strip()
        elif line.strip() == "@sql":
            in_sql = True
        elif line.strip() == "@endsql":
            in_sql = False
        elif line.strip() == "@end":
            if q is not None:
                out.append((q, ans, "\n".join(sql).strip()))
            q = None
        elif in_sql:
            sql.append(line)
    return out


def build_db(binary):
    ddl = run([binary, "--emit-sql-schema"])
    con = sqlite3.connect(":memory:")
    con.executescript(ddl)
    return con


def execute(con, sql):
    """Result set as an order independent multiset, or an error string."""
    if not sql:
        return ("error", "no sql produced")
    try:
        rows = con.execute(sql).fetchall()
    except Exception as exc:  # noqa: BLE001 - any SQL error is just a failure here
        return ("error", str(exc)[:120])
    return ("rows", sorted(str(r) for r in rows))


def ask_model(model, schema, question, seed, labels=""):
    # The label values go in the instruction rather than into the schema as a
    # SQL comment. The rule based side is handed a lexicon mapping surface forms
    # onto entities, so withholding every label from the model measures a
    # different thing than the pipeline is being compared on.
    guidance = ""
    if labels:
        guidance = ("These are the exact label values stored in the database. "
                    "Match on these strings exactly, not on a shortened form of "
                    "a name:" + chr(10) + labels + chr(10))
    prompt = (
        "You are given a SQLite schema. Write one SQL query answering the question.\n"
        "Return only the SQL. No explanation, no markdown fence.\n"
        "The query must return a single column of human readable labels, "
        "such as a title or a name, not identifiers.\n\n"
        + guidance +
        "Schema:\n%s\n\nQuestion: %s\nSQL:" % (schema, question)
    )
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0, "seed": seed, "num_predict": 400},
    }).encode()
    req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=600) as resp:
        text = json.load(resp)["response"]
    text = re.sub(r"^```[a-zA-Z]*\s*|\s*```$", "", text.strip(), flags=re.M).strip()
    return text.split(";")[0].strip() + ";" if ";" in text else text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--binary", default="./build/lexicon.exe")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default="")
    ap.add_argument("--with-labels", action="store_true",
                    help="also give the model the label values held in the database. "
                         "The rule based side is given a lexicon mapping surface forms "
                         "onto entities, so withholding every label from the model is "
                         "not a like for like comparison. Running both conditions "
                         "separates failures of query construction from failures of "
                         "entity linking.")
    a = ap.parse_args()

    digest = ""
    for line in run(["ollama", "list"]).splitlines():
        if line.split(" ")[0] == a.model:
            digest = line.split()[1]

    schema = run([a.binary, "--emit-sql-schema"])
    ddl_only = "\n".join(l for l in schema.splitlines() if not l.startswith("INSERT"))
    label_block = ""
    if a.with_labels:
        con0 = build_db(a.binary)
        parts = []
        for table, col in (("film", "title"), ("person", "name"),
                           ("country", "name"), ("studio", "name")):
            vals = [r[0] for r in con0.execute("SELECT %s FROM %s ORDER BY 1" % (col, table))]
            parts.append("%s.%s: %s" % (table, col, ", ".join(vals)))
        label_block = chr(10).join(parts)
    cases = load_machine_output(a.binary)
    con = build_db(a.binary)

    lines = []
    agree = same_rows = model_err = 0
    for i, (q, ans, sql) in enumerate(cases, 1):
        want = execute(con, sql)
        got_sql = ask_model(a.model, ddl_only, q, a.seed, label_block)
        got = execute(con, got_sql)
        ok = want == got and want[0] == "rows"
        if got[0] == "error":
            model_err += 1
        if ok:
            agree += 1
            same_rows += 1
        lines.append("%2d. %s\n    lexicon : %s\n    model   : %s\n    verdict : %s" % (
            i, q,
            "rows=%s" % (want[1] if want[0] == "rows" else "ERROR " + want[1]),
            "rows=%s" % (got[1] if got[0] == "rows" else "ERROR " + got[1]),
            "same result set" if ok else "differs"))
        lines.append("    model sql: %s" % got_sql.replace("\n", " "))

    head = [
        "Baseline comparison, local language model against the rule based pipeline",
        "",
        "model      : %s" % a.model,
        "digest     : %s" % (digest or "unknown"),
        "settings   : temperature 0, seed %d, num_predict 400" % a.seed,
        "runner     : ollama, %s" % run(["ollama", "--version"]).strip(),
        "criterion  : execution equivalence against the bundled database.",
        "             Both queries run, the result sets are compared as unordered",
        "             multisets. Query text is not compared.",
        "questions  : %d" % len(cases),
        "condition  : %s" % ("schema plus the label values held in the data"
                             if a.with_labels else "schema only, no data shown"),
        "",
        "agreements : %d of %d" % (agree, len(cases)),
        "model SQL that did not execute: %d" % model_err,
        "",
    ]
    report = "\n".join(head + lines) + "\n"
    if a.out:
        open(a.out, "w", encoding="utf-8").write(report)
    print(report)


if __name__ == "__main__":
    main()
