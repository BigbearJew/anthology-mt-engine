"""Run native DLL integration checks against the shipped Anomaly DXML scripts."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAYLOAD = ROOT / 'anthology-compat/gamedata'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', type=Path, default=ROOT/'build-x64/bin/RelWithDebInfo')
    args = parser.parse_args()
    binary = args.bin_dir.resolve()/'anthology_compat_tests.exe'
    fixture = args.bin_dir.resolve()/'anthology-check-fixture'
    (fixture / 'configs/ui').mkdir(parents=True, exist_ok=True)
    (fixture / 'scripts').mkdir(exist_ok=True)
    (fixture / 'scripts/mixedcase_probe.script').write_text('value=19\n', encoding='ascii')
    (fixture / 'scripts/path_probe.script').write_text("source = debug.getinfo(1, 'S').source\n", encoding='ascii')
    (fixture / 'scripts/global_probe.script').write_text("ixray_namespace_probe = (ixray_namespace_probe or 0) + 1\n", encoding='ascii')
    (fixture / 'configs/unlocalizers').mkdir(exist_ok=True)
    (fixture / 'configs/unlocalizers/probe.ltx').write_text(
        '[Unlocalizer_Probe]\nparameters\nfirst\npending\nexposed\n', encoding='ascii')
    shutil.copy2(ROOT / 'tools/tests/unlocalizer_probe.script', fixture / 'scripts/unlocalizer_probe.script')
    (fixture / 'appdata/logs').mkdir(parents=True, exist_ok=True)
    (fixture / 'fsgame_test.ltx').write_text(
        '$app_data_root$ = true | false | $fs_root$ | appdata\\\n'
        '$logs$ = true | false | $app_data_root$ | logs\\\n'
        '$game_scripts$ = true | false | $fs_root$ | scripts\\\n'
        '$game_config$ = true | false | $fs_root$ | configs\\\n', encoding='ascii')
    (fixture / 'configs/ui/dxml_probe.xml').write_text('<root><value>1</value></root>', encoding='ascii')
    (fixture / 'configs/ui/mod_dxml_probe_native.xml').write_text(
        '<root><value override="replace">5</value></root>', encoding='ascii')
    (fixture / 'configs/ui/legacy_declaration.xml').write_bytes(
        b'\xef\xbb\xbf<!-- Catspaw-style addon credits -->\n<!-- second comment -->\n'
        b'<?xml version="1.0" encoding="windows-1251"?>\n'
        b'<root><value>17</value><text>\xd1\xed\xe5\xe3</text></root>')
    (fixture / 'configs/ui/nested_declaration.xml').write_text(
        '<root><!-- invalid declaration inside the document --><?xml version="1.0"?></root>', encoding='ascii')
    (fixture / 'configs/ui/comment_only.xml').write_text('<!-- no root -->', encoding='ascii')
    (fixture / 'configs/ui/xml_text.xml').write_text(
        '<root><text><![CDATA[<!-- credits --><?xml version="1.0"?>]]></text></root>', encoding='ascii')
    reference = ROOT / 'tools/tests/fixtures/anthology'
    hashes = {}
    for name in ('dxml_core.script', 'slaxml.script'):
        source = PAYLOAD / 'scripts' / name
        if not source.exists():
            source = reference / name
        hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        shutil.copy2(source, fixture / name)
    shutil.copy2(ROOT / 'tools/tests/marshal_test.lua', fixture / 'marshal_test.lua')
    shutil.copy2(ROOT / 'tools/tests/dxml_cache_test.lua', fixture / 'dxml_cache_test.lua')
    shutil.copy2(ROOT / 'tools/tests/mcm_native_test.lua', fixture / 'mcm_native_test.lua')
    shutil.copy2(PAYLOAD / 'scripts/anthology_ixray_mcm.script', fixture / 'anthology_ixray_mcm.script')
    result = subprocess.run([str(binary), '--lua'],
        cwd=fixture, capture_output=True, timeout=30)
    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
    report = dict(exit=result.returncode, passed=output.count('PASS:'), fixture_sha256=hashes, output=output)
    (fixture / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(output)
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
