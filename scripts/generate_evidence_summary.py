#!/usr/bin/env python3
"""Generate a distributable count/hash manifest without copying game data.

Inputs are a public master checkout and the local retained verification reports.
Only aggregate counts, numeric rule types, proof boundaries and SHA256 values are
written. No master rows, recovered instructions, account input, or external file
paths enter the generated manifest. Run after the final source/test changes.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def read(path: Path):
    return json.loads(path.read_text())


def snapshot(directory: Path):
    used = {}
    cache = {}

    def rows(name):
        if name not in cache:
            raw = (directory / (name + '.json')).read_bytes()
            table = json.loads(raw)
            value = table['_allData'] if isinstance(table, dict) else table
            if not isinstance(value, list) or any(not isinstance(row, dict) for row in value):
                raise ValueError(f'{name}: invalid table')
            cache[name] = value
            used[name] = digest(raw)
        return cache[name]

    sets = {}
    for row in rows('MasterSkillConditionSet'):
        sets.setdefault(row['_group'], []).extend(row['_conditionIds'])
    conditions = {row['_id']: row for row in rows('MasterSkillCondition')}
    targets = {row['_id']: row for row in rows('MasterSkillTarget')}
    cumulative = {row['_id']: row for row in rows('MasterSkillCumulativeCondition')}
    members = rows('MasterMemberCard')
    supports = rows('MasterSupportCard')
    support_ranks = rows('MasterSupportCardRank')
    member_ranks = rows('MasterMemberCardRank')
    configurations = []
    for category, table, key, card_key in (
        ('ordinary_member', 'MasterLiveSkillEffect', '_liveSkillID', '_liveSkillID'),
        ('gekisou_member', 'MasterGekisouSkillEffect', '_gekisouSkillID', '_gekisouSkillID'),
        ('ordinary_support', 'MasterSupportSkillEffect', '_supportSkillID', '_supportSkillId'),
        ('gekisou_support', 'MasterGekisouSupportSkillEffect', '_gekisouSupportSkillID', '_gekisouSupportSkillId'),
        ('leader', 'MasterLeaderSkillEffect', '_leaderSkillID', '_leaderSkillID'),
    ):
        all_rows = rows(table)
        references = set()
        configurations_count = 0
        if category.endswith('member'):
            levels = {}
            for row in all_rows:
                levels.setdefault(row[key], set()).add(row['_level'])
            for card in members:
                for level in levels.get(card[card_key], ()):
                    references.add((card[card_key], level))
                    configurations_count += 1
        elif category == 'leader':
            for card in members:
                for rank in member_ranks:
                    if rank['_group'] == card['_memberCardRankGroup'] and card[card_key]:
                        references.add((card[card_key], rank['_leaderSkillLevel']))
                        configurations_count += 1
        else:
            prefix = '_gekisouSupportSkill' if category == 'gekisou_support' else '_supportSkill'
            for card in supports:
                for rank in support_ranks:
                    if rank['_group'] != card['_supportCardRankGroup']:
                        continue
                    for suffix in ('01', '02'):
                        if card[card_key + suffix]:
                            references.add((card[card_key + suffix], rank[prefix + suffix + 'Level']))
                    configurations_count += 1
        selected = [row for row in all_rows if (row[key], row['_level']) in references]
        missing = references - {(row[key], row['_level']) for row in selected}
        if missing:
            raise ValueError(f'{table}: selected skill/level references have no effects')
        types = []
        for effect_type in sorted({row['_skillEffectType'] for row in selected}):
            matching = [row for row in selected if row['_skillEffectType'] == effect_type]
            branches = Counter()
            target_types = Counter()
            cumulative_types = Counter()
            for row in matching:
                for role, field in [('condition', '_skillConditionGroup'), ('trigger', '_skillTriggerConditionGroup'),
                                    ('release', '_skillReleaseConditionGroup'), ('reset', '_effectExecuteLimitResetConditionGroup')]:
                    group = row.get(field, 0)
                    if group and group not in sets:
                        raise ValueError(f'{table}: missing condition group')
                    for ident in sets.get(group, ()):
                        condition = conditions[ident]
                        branches[(role, condition['_conditionType'], condition['_isPositive'])] += 1
                        for target in condition['_conditionTargetIDs']:
                            target_types[targets[target]['_skillTargetType']] += 1
                for target in row.get('_skillTargetIDs', ()):
                    target_types[targets[target]['_skillTargetType']] += 1
                if row.get('_skillCumulativeConditionID', 0):
                    condition = cumulative[row['_skillCumulativeConditionID']]
                    cumulative_types[condition['_skillCumulativeConditionType']] += 1
            native = 'not_independently_compared_with_cpp'
            boundary = 'Python runtime differential; historical native components do not prove the joint scheduler.'
            if category == 'ordinary_member':
                native = 'passed_component_chain_900_cases'
                boundary = 'Native trigger, life predicate, duration updater and score controller; condition inversion and judgement target expansion are fixtures.'
            elif category == 'gekisou_member' and effect_type in (12000, 13000, 13002):
                native = 'passed_static_count_replay_270_cases'
                boundary = 'Native count and cumulative replay; 30 dynamic member/level configurations excluded; trigger/lifecycle phase supplied.'
            elif category == 'gekisou_support' and effect_type == 2001:
                native = 'passed_applier_chain_300_effect_rows'
                boundary = 'Native applier Begin/Stay/End and controller Add/Get/Disable; phase, cumulative values and accepted band gates supplied.'
            types.append({'effect_type': effect_type, 'referenced_rows': len(matching),
                          'conditions': [{'role': role, 'type': typ, 'positive': positive, 'references': count}
                                         for (role, typ, positive), count in sorted(branches.items())],
                          'target_types': dict(sorted(target_types.items())),
                          'cumulative_types': dict(sorted(cumulative_types.items())),
                          'independent_native_cpp': native, 'native_boundary': boundary})
        configurations.append({'subsystem': category, 'card_configurations': configurations_count,
                               'master_rows': len(all_rows), 'referenced_rows': len(selected),
                               'unreferenced_rows': len(all_rows) - len(selected), 'effect_types': types})
    instruments = rows('MasterBandItemSkillEffect')
    memory = {name: len(rows(name)) for name in ('MasterMemoryMemberLevel', 'MasterMemorySupportLevel',
                                               'MasterMemoryMusic', 'MasterMemoryMusicBonus', 'MasterMemoryMusicGroup')}
    return {'member_cards': len(members), 'support_cards': len(supports),
            'member_rank_rows': len(member_ranks), 'support_rank_rows': len(support_ranks),
            'rule_matrix': configurations,
            'instrument_effect_types': dict(sorted(Counter(row['_skillEffectType'] for row in instruments).items())),
            'memory_table_rows': memory, 'master_table_sha256': dict(sorted(used.items())),
            'master_manifest_sha256': digest(canonical(used))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master-root', type=Path, required=True)
    parser.add_argument('--research-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/evidence-summary.json')
    args = parser.parse_args()
    regions = {region: snapshot(args.master_root / region) for region in ('kr', 'en', 'hk-tw-mo')}
    root = args.research_root
    # Whitelist aggregates; copying entire reports could accidentally publish
    # native paths/instructions or full input trajectories added in the future.
    power = read(root / 'power-differential.json')
    core = read(root / 'core-differential.json')
    support = read(root / 'support-native-differential.json')
    counts = read(root / 'native-counter-report.json')
    ordinary = read(root / 'native-rule-coverage.json')
    checks = {
        'power_python': {'cases': power['differential_cases'], 'differences': power['differences'],
                         'scope': 'All current card ranks/awakening and endpoint growth levels; public master.'},
        'core_python': {'cases': core['cases'], 'ranking_conditions': core['ranking_conditions_per_case'],
                        'differences': core['differences'], 'scope': 'Fixed runtime trajectories, per-note and dual-track scores, contributions.'},
        'ordinary_member_native': {'cases': ordinary['cpp_member_cases'], 'differences': ordinary['differences'], 'scope': 'Component chain; supplied interfaces/conditions as described in rule matrix.'},
        'gekisou_counter_native': {'cpp_cases': counts['cpp_fixture_cases'], 'native_replays': counts['native_replays'],
                                  'excluded_dynamic_card_levels': len(counts['excluded_dynamic']), 'status': counts['status']},
        'cumulative_support_native': {'effect_rows': support['master_effect_rows'], 'cpp_cases': support['cpp_runtime_cases'],
                                     'native_applier_calls': support['native_applier_calls'], 'extra_lifecycle_calls': support['extra_lifecycle_calls'],
                                     'differences': support['differences']},
    }
    summaries = ['power-differential.json', 'core-differential.json', 'support-native-differential.json',
                 'native-counter-report.json', 'api-review.json']
    logs = {}
    for path in sorted(root.glob('*comparison.log')):
        matches = re.findall(r'passed\s+(\d+)\s+failed\s+(\d+)', path.read_text())
        if matches:
            passed, failed = map(int, matches[-1])
            logs[path.stem] = {'passed': passed, 'failed': failed, 'report_sha256': digest(path.read_bytes())}
    source_files = set()
    for directory in ('src', 'include', 'tests', 'scripts', '.github'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                source_files.add(path)
    for name in ('CMakeLists.txt', 'web/index.ts', 'web/worker.mjs', 'web/package.json', 'web/package-lock.json'):
        source_files.add(ROOT / name)
    source_manifest = {path.relative_to(ROOT).as_posix(): digest(path.read_bytes()) for path in sorted(source_files)}
    output = {'schema_version': 1, 'release_status': 'development_acceptance_incomplete',
              'counts_are_overlapping_not_additive': True,
              'source_manifest_contract': 'Hashes describe the current repository files; retained report hashes identify evidence produced at their recorded revisions. Refreshing this manifest does not rerun validation.',
              'snapshots': regions, 'executed_checks': checks, 'comparison_reports': logs,
              'retained_report_sha256': {Path(name).stem: digest((root / name).read_bytes()) for name in summaries},
              'source_manifest': source_manifest, 'source_manifest_sha256': digest(canonical(source_manifest)),
              'pending': ['Complete native LiveExecutor/SkillExecutor joint scheduling and condition phase observation',
                          'Native dynamic Combo lifecycle, probability/Luck and all support interactions',
                          'Real nonempty Memory and active event context evidence',
                          'Windows/MSVC and macOS runtime verification'],
              'privacy': 'Aggregate counts, rule type identifiers and hashes only; no game data, recovered native instructions, account data or external paths.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + '\n')
    print(f'generated {args.output.name}: {len(regions)} snapshots, {len(source_manifest)} repository source/fixture hashes')


if __name__ == '__main__':
    main()
