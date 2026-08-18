"""Runs the generated SQL against SQLite and compares it with the evaluator.

The C++ side answers a question by evaluating the logical form directly over the
bundled knowledge base. The SQL generator produces a query against the
relational rendering of the same facts. If the logical form is a real
abstraction then the two must agree on every question, and this script is what
checks that they do.

    python tools/check_sql.py build/lexicon.exe

The only dependency is the sqlite3 module of the standard library.
"""

import os
import subprocess
import sqlite3
import sys


def resolve(binary):
    """Absolute path of the demo binary, with the Windows suffix if needed.

    CreateProcess does not look a relative path up the way a shell does, so
    "build/lexicon" has to be turned into a full path before it is run.
    """
    path = os.path.abspath(binary)
    if not os.path.exists(path) and os.path.exists(path + ".exe"):
        path += ".exe"
    if not os.path.exists(path):
        raise SystemExit(f"no such binary: {binary}. Build it first with cmake --build build")
    return path


def run(binary, *args):
    result = subprocess.run([binary, *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        raise SystemExit(f"{binary} {' '.join(args)} failed: {result.stderr}")
    return result.stdout


def records(text):
    """Splits the --machine output into one record per question."""
    current = None
    sql_lines = None
    for line in text.splitlines():
        if line.startswith("@question "):
            current = {"question": line[len("@question "):], "sql": None, "answer": None}
        elif line.startswith("@answer "):
            current["answer"] = line[len("@answer "):]
        elif line.startswith("@failed "):
            current["failed"] = line[len("@failed "):]
        elif line == "@sql":
            sql_lines = []
        elif line == "@endsql":
            current["sql"] = "\n".join(sql_lines)
            sql_lines = None
        elif line == "@end":
            yield current
            current = None
        elif sql_lines is not None:
            sql_lines.append(line)


def answer_of(cursor, sql):
    cursor.execute(sql)
    rows = cursor.fetchall()
    if sql.startswith("SELECT EXISTS"):
        return "yes" if rows and rows[0][0] else "no"
    if "COUNT(" in sql:
        return str(rows[0][0]) if rows else "0"
    if not rows:
        return "(no rows)"
    return ", ".join(str(row[0]) for row in rows)


def main():
    binary = resolve(sys.argv[1] if len(sys.argv) > 1 else "build/lexicon")
    connection = sqlite3.connect(":memory:")
    connection.executescript(run(binary, "--emit-sql-schema"))
    cursor = connection.cursor()

    checked = 0
    agreed = 0
    skipped = 0
    for record in records(run(binary, "--machine")):
        if record.get("sql") is None:
            skipped += 1
            print(f"SKIP  {record['question']}  ({record.get('failed', 'no query')})")
            continue
        checked += 1
        from_sql = answer_of(cursor, record["sql"])
        if from_sql == record["answer"]:
            agreed += 1
            print(f"OK    {record['question']}  -> {from_sql}")
        else:
            print(f"DIFF  {record['question']}")
            print(f"      evaluator: {record['answer']}")
            print(f"      sqlite:    {from_sql}")

    print(f"\n{agreed}/{checked} generated queries agree with the evaluator, "
          f"{skipped} question(s) produced no query")
    return 0 if agreed == checked else 1


if __name__ == "__main__":
    sys.exit(main())
