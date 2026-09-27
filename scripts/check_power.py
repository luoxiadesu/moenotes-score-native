#!/usr/bin/env python3
"""Public-master power sweep against the independent Python reconstruction.

Requires a built internal test_power_driver executable; reads public master only.
Writes cases solely inside TemporaryDirectory and never uses account snapshots.
"""
from __future__ import annotations
import argparse
from copy import deepcopy
import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--master', type=Path, default=ROOT.parent / 'moenotes-masterdata/hk-tw-mo')
    parser.add_argument('--python-source', type=Path, default=ROOT.parent / 'moenotes-score/src')
    args = parser.parse_args()
    sys.path.insert(0, str(args.python_source.resolve()))
    resolve_deck = importlib.import_module('moenotes_score.player').resolve_deck
    required = set(json.loads((ROOT / 'tests/fixtures/power.json').read_text())['master_tables'])
    tables = {name: json.loads((args.master / (name + '.json')).read_text())['_allData'] for name in required}
    cards = tables['MasterMemberCard']
    supports = tables['MasterSupportCard']
    song = next(row for row in tables['MasterLiveMusic'] if row['_expertID'] > 0)
    score = song['_expertID']
    requests = []
    labels = []
    def request(card, level, awake, rank, support=None, support_level=None, support_rank=None):
        group = card['_memberCardLevelGroup']
        lr = next(r for r in tables['MasterMemberCardLevel'] if r['_group'] == group and r['_level'] == level)
        player = {'memberCards': [{'masterId': card['_id'], 'exp': lr['_exp'], 'awakeCount': awake,
                  'cardRank': rank, 'liveSkillLevel': 1, 'performanceSkillLevel': 1}],
                  'supportCards': [], 'decks': [{'id': 1, 'cards': [
                      {'slotIndex': i, 'performanceOrderIndex': i, 'memberCardId': card['_id'] if i == 2 else -1,
                       'supportCardId': support['_id'] if i == 2 and support else -1} for i in range(5)]}]}
        if support:
            sl = next(r for r in tables['MasterSupportCardLevel'] if r['_group'] == support['_supportCardLevelGroup'] and r['_level'] == support_level)
            player['supportCards'] = [{'masterId': support['_id'], 'exp': sl['_exp'], 'cardRank': support_rank}]
        return {'music_score_id': score, 'deck_id': 1, 'player_data': player}
    for card in cards:
        ranks = [r['_rank'] for r in tables['MasterMemberCardRank'] if r['_group'] == card['_memberCardRankGroup']]
        awakes = [r['_awakeCount'] for r in tables['MasterMemberCardAwake'] if r['_group'] == card['_memberCardAwakeGroup']]
        for awake in awakes:
            limit = next(r['_limitLevel'] for r in tables['MasterMemberCardLevelLimit'] if r['_rarity'] == card['_rarity'] and r['_awakeCount'] == awake)
            for rank in ranks:
                for level in sorted({1, limit}):
                    requests.append(request(card, level, awake, rank))
                    labels.append(f'member {card["_id"]}, level {level}, awake {awake}, rank {rank}')
    for support in supports:
        for rank in (r for r in tables['MasterSupportCardRank'] if r['_group'] == support['_supportCardRankGroup']):
            for level in sorted({1, rank['_limitLevel']}):
                requests.append(request(cards[0], 1, 1, 1, support, level, rank['_rank']))
                labels.append(f'support {support["_id"]}, level {level}, rank {rank["_rank"]}')
    with tempfile.TemporaryDirectory(prefix='mns-public-power-') as temp:
        input_path = Path(temp) / 'input.json'
        input_path.write_text(json.dumps({'master_tables': tables, 'requests': requests}, separators=(',', ':')))
        output = subprocess.check_output([str(args.driver.resolve()), str(input_path)], text=True)
        actual = json.loads(output)
    assert len(actual) == len(requests)
    for label, request_row, found in zip(labels, requests, actual, strict=True):
        expected = resolve_deck(request_row['player_data'], tables, 1, score).to_dict()
        found.pop('source')
        expected.pop('source')
        expected = json.loads(json.dumps(expected))
        if found != expected:
            differing = [k for k in expected if expected[k] != found.get(k)]
            raise AssertionError(f'{label}: differs in {differing}')
    print(json.dumps({'public_master': args.master.name, 'member_count': len(cards), 'support_count': len(supports),
                      'differential_cases': len(requests), 'differences': 0}))


if __name__ == '__main__':
    main()
