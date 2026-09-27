#!/usr/bin/env python3
"""Validate an external corpus without adding copyrighted charts to this repo."""
import argparse
import collections
import gzip
import json
import math
import pathlib
import subprocess
import tempfile


def run(cli, path, flags):
    result = subprocess.run([str(cli), "parse", str(path), *flags], capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(f"{path}: {result.stderr.decode(errors='replace')}")
    return json.loads(result.stdout)


def validate(score):
    notes = score["notes"]
    assert score["note_count"] == len(notes)
    ids = {n["id"] for n in notes}
    assert len(ids) == len(notes)
    assert [n["tick"] for n in notes] == sorted(n["tick"] for n in notes)
    assert score["full_combo_count"] == sum(n["judgement"] for n in notes)
    by_id = {n["id"]: n for n in notes}
    for i, line in enumerate(score["lines"]):
        assert line["id"] == i
        assert line["begin_note_id"] in line["members"]
        assert line["end_note_id"] in line["members"]
        assert len(line["members"]) == len(set(line["members"]))
        assert all(n in ids for n in line["members"])
        members = [by_id[n] for n in line["members"]]
        assert [n["tick"] for n in members] == sorted(n["tick"] for n in members)
    for note in notes:
        for key in ("parent_note_id", "pair_note_id", "hidden_for_note_id"):
            assert note[key] == -1 or note[key] in ids, (key, note)
        for key in ("lane_start_float", "lane_end_float", "width"):
            assert math.isfinite(note[key]), (key, note)
        assert note["time_ms"] >= 0
        assert note["width"] >= 0
        assert note["source_geometry"]["width"] >= 0
        if note["fever_event_index"] >= 0:
            event = score["events"][note["fever_event_index"]]
            assert event["type"] == 1
            assert event["time_ms"] <= note["time_ms"] <= event["end_time_ms"]
    for event in score["events"]:
        if event["type"] == 2:
            expected = [(i + 1) / len(event["values"])
                        for i, value in enumerate(event["values"]) if value == 1]
            assert len(event["rhythms"]) == len(expected)
            assert all(abs(a - b) < 1e-7 for a, b in zip(event["rhythms"], expected))
        else:
            assert not event["rhythms"]
    assert all(value is not None and value >= 0 for value in score["bar_line_times_ms"])
    assert set(score["last_timing_note_ids"]) <= ids


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cli", type=pathlib.Path)
    parser.add_argument("corpus", type=pathlib.Path)
    args = parser.parse_args()
    cli = args.cli.resolve()
    stats = collections.Counter()
    failures = []
    flags = ["--combo-unit", "8", "--flick-hidden"]
    with tempfile.TemporaryDirectory(prefix="moenotes-corpus-") as directory:
        zipped = pathlib.Path(directory) / "chart.json.gz"
        for path in sorted(args.corpus.rglob("*")):
            if not path.is_file():
                continue
            try:
                data = path.read_bytes()
                if data.lstrip().startswith(b"#"):
                    result = subprocess.run([str(cli), "parse", str(path)], capture_output=True, timeout=30)
                    assert result.returncode != 0 and b"unsupported" in result.stderr
                    stats["unsupported_legacy_text"] += 1
                    continue
                json.loads(data)
                base = run(cli, path, [])
                derived = run(cli, path, flags)
                mirror = run(cli, path, [*flags, "--mirror"])
                validate(base)
                validate(derived)
                validate(mirror)
                assert derived == run(cli, path, flags)
                zipped.write_bytes(gzip.compress(data, mtime=0))
                assert derived == run(cli, zipped, flags)
                # Integer rounding can affect endpoint merging under mirror.
                mirrored = {n["id"]: n for n in mirror["notes"]}
                for n in derived["notes"]:
                    if n["id"] not in mirrored:
                        continue
                    m = mirrored[n["id"]]
                    expected = 24 - n["lane_start_float"] - n["width"]
                    assert abs(m["lane_start_float"] - expected) < 2e-4
                    assert abs(m["width"] - n["width"]) < 2e-4
                    assert (m["ease_left"], m["ease_right"]) == (n["ease_left"], n["ease_right"])
                stats["json_charts"] += 1
                stats["parse_runs"] += 5
                stats["base_notes"] += base["note_count"]
                stats["derived_notes"] += derived["note_count"]
                stats["auto_nodes"] += sum(n["pos_auto"] for n in base["notes"])
                stats["combo_nodes"] += sum(n["type"] == 120 for n in derived["notes"])
                stats["combo_skip_nodes"] += sum(n["type"] == 121 for n in derived["notes"])
                stats[f"warnings_{derived['warnings']}"] += 1
            except (AssertionError, ValueError, subprocess.TimeoutExpired) as exc:
                failures.append({"file": str(path.relative_to(args.corpus)), "error": str(exc)})
    print(json.dumps({"stats": dict(stats), "failures": failures}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
