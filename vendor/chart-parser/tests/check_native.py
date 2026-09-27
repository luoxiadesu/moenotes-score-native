#!/usr/bin/env python3
"""Compare CLI output to externally supplied native snapshots.

No game code, chart data, or expected snapshots are included in this repository.
Snapshot schema and normalization rules: docs/native-differential.md.
"""
import argparse
import collections
import json
import pathlib
import struct
import subprocess


def bits(value):
    return struct.pack("<f", value)


def compare(raw, native, score):
    errors = []

    def check(actual, expected, field, key=None):
        if actual != expected:
            if isinstance(key, tuple):
                key = [v.hex() if isinstance(v, bytes) else v for v in key]
            errors.append({"field": field, "key": key, "actual": repr(actual),
                           "expected": repr(expected)})

    origins = {int(k): tuple(v) for k, v in native["origins"].items()}
    branches = {int(k): v for k, v in native["branch_map"].items()}
    ngroups, cgroups = collections.defaultdict(list), collections.defaultdict(list)
    for n in native["after"]:
        if n["type"] in (120, 121):
            key = ("generated", branches[n["line_ids"][0]], n["position"][0],
                   bits(n["position"][3]))
        else:
            key = ("source", *origins[n["id"]])
        ngroups[key].append(n)
    occurrences = collections.Counter()
    for n in score["notes"]:
        if n["generated"]:
            key = ("generated", n["source_index"], n["bar"], bits(n["bar_progress"]))
        else:
            source = raw["notes"][n["source_index"]]
            nodes = source.get("node", [source])
            indices = [i for i, v in enumerate(nodes) if v.get("t", 0) == n["tick"]]
            occurrence = occurrences[n["source_index"], n["tick"]]
            occurrences[n["source_index"], n["tick"]] += 1
            if occurrence >= len(indices):
                raise ValueError("source provenance cannot be matched")
            key = ("source", n["source_index"], indices[occurrence])
        cgroups[key].append(n)
    mapped, matches = {}, []
    for key in ngroups.keys() | cgroups.keys():
        a, b = ngroups[key], cgroups[key]
        check(len(b), len(a), "multiplicity", key)
        if len(a) == len(b):
            for n, c in zip(a, b):
                mapped[n["id"]] = c["id"]
                matches.append((key, n, c))
    fever_indices = [i for i, e in enumerate(score["events"]) if e["type"] == 1]
    for key, n, c in matches:
        check(c["type"], n["type"], "type", key)
        check(c["bar"], n["position"][0], "bar", key)
        check(c["time_ms"], n["position"][4], "time_ms", key)
        check(bits(c["bar_progress"]), bits(n["position"][3]), "progress", key)
        for field, index in [("lane_start", 0), ("lane_end", 1)]:
            check(c[field], n["geometry"][index], field, key)
        for field, index in [("lane_start_float", 2), ("lane_end_float", 3), ("width", 4)]:
            check(bits(c[field]), bits(n["geometry"][index]), field, key)
        for field in ("critical", "direction", "slide_along"):
            check(c[field], n[field], field, key)
        check((c["ease_left"], c["ease_right"]), tuple(n["ease"]), "ease", key)
        if key[0] == "source":
            check((c["rhythm"], c["rhythmic_unit"]), tuple(n["position"][1:3]),
                  "source_rhythm", key)
            pair = -1 if n["pair"] < 0 else mapped.get(n["pair"], "unmatched")
            check(c["pair_note_id"], pair, "pair", key)
        if "fever" in n:
            fever = -1 if n["fever"] < 0 else fever_indices[n["fever"]]
            check(c["fever_event_index"], fever, "fever", key)
    clines = {n["source_index"]: n for n in score["lines"]}
    for line_id, n in native["lines"].items():
        source = branches[int(line_id)]
        c = clines[source]
        for field in ("begin", "end"):
            check(c[field + "_note_id"], mapped.get(n[field], "unmatched"), field, source)
        check(c["members"], [mapped.get(i, "unmatched") for i in n["members"]],
              "line_member_order", source)
    if "events" in native:
        for kind, typ in (("skill", 0), ("fever", 1), ("call", 2)):
            expected = native["events"][kind]
            actual = [e for e in score["events"] if e["type"] == typ]
            check(len(actual), len(expected), kind + "_count")
            for i, (c, n) in enumerate(zip(actual, expected)):
                check(c["time_ms"], n["position"][4], kind + "_time", i)
                if typ == 1:
                    check(c["end_time_ms"], n["end"][4], "fever_end", i)
                if typ == 2:
                    check([bits(v) for v in c["rhythms"]], [bits(v) for v in n["rhythms"]],
                          "call_rhythms", i)
        for kind, cli_kind in (("bpm", "bpms"), ("sig", "signatures")):
            actual, expected = score[cli_kind], native["events"][kind]
            check(len(actual), len(expected), kind + "_count")
            for i, (c, n) in enumerate(zip(actual, expected)):
                value = c["bpm"] if kind == "bpm" else c["numerator"] * 4 / c["denominator"]
                check(bits(value), bits(n["value"]), kind + "_value", i)
                if kind == "bpm":
                    check(c["time_ms"], n["position"][4], "bpm_time", i)
        check(score["bar_line_times_ms"], native["bar_times"], "bar_times")
        check(set(score["last_timing_note_ids"]),
              {mapped.get(i, "unmatched") for i in native["last_ids"]}, "last_timing_members")
    check(len(score["notes"]), len(native["after"]), "note_count")
    check(len(score["lines"]), len(native["lines"]), "line_count")
    return len(matches), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cli", type=pathlib.Path)
    parser.add_argument("corpus", type=pathlib.Path)
    parser.add_argument("snapshots", type=pathlib.Path)
    args = parser.parse_args()
    rows, nodes, failure_records = [], 0, 0
    for p in sorted(args.snapshots.glob("*.json")):
        if p.name.endswith(".failure.json"):
            failure_records += 1
            continue
        d = json.loads(p.read_text())
        path = (args.corpus / d["file"]).resolve()
        if not path.is_relative_to(args.corpus.resolve()):
            raise ValueError("snapshot file is outside corpus")
        raw = json.loads(path.read_bytes())
        raw = raw.get("score", raw)
        flags = ["--combo-unit", "8", *(["--mirror"] if d["mirror"] else [])]
        try:
            out = subprocess.run([str(args.cli.resolve()), "parse", str(path), *flags],
                                 check=True, capture_output=True, timeout=30)
            matched, differences = compare(raw, d["native"], json.loads(out.stdout))
            nodes += matched
            rows.append({"file": d["file"], "mirror": d["mirror"],
                         "matched_nodes": matched, "differences": differences})
        except (ValueError, KeyError, IndexError, subprocess.SubprocessError) as exc:
            rows.append({"file": d["file"], "mirror": d["mirror"], "error": str(exc)})
    failed = [r for r in rows if r.get("differences") or "error" in r]
    print(json.dumps({"runs": len(rows), "matched_nodes": nodes,
                      "native_failure_records": failure_records, "failures": failed}, indent=2))
    return int(not rows or bool(failed) or failure_records > 0)


if __name__ == "__main__":
    raise SystemExit(main())
