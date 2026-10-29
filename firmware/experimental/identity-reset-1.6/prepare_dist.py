#!/usr/bin/env python3
"""Package the identity reset workflow without invoking any hardware tool."""
import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SERVICE = 'VGL_Patrol_1.6_IDENTITY_RESET_SERVICE_FULL_S132_BOOTLOADER.hex'
APPROVED = 'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(base_dir, zip_path=None):
    dist = HERE / 'dist'
    assert (dist / SERVICE).is_file(), 'Build the service firmware first'
    for p in (HERE / 'windows').iterdir():
        if p.is_file() and p.suffix.lower() in {'.bat', '.ps1', '.md'}:
            shutil.copyfile(p, dist / p.name)
    for name in [APPROVED, 'VGL_Patrol_1.6_1400ms_RF5ms.bin',
                 's132_nrf52_6.1.0_licence-agreement.txt']:
        shutil.copyfile(base_dir / name, dist / name)
    shutil.copyfile(HERE / 'README_RU.md', dist / 'README_RU.md')
    shutil.copyfile(HERE / 'README.md', dist / 'TECHNICAL.md')
    for name in ['validation_summary.json', 'mapping_reset.json']:
        if (HERE / name).is_file():
            shutil.copyfile(HERE / name, dist / name)
    payloads = sorted(p for p in dist.iterdir() if p.is_file()
                      and p.name not in {'SHA256SUMS', 'flash_manifest.json'})
    (dist / 'SHA256SUMS').write_text(''.join(
        f'{digest(p)}  {p.name}\n' for p in payloads), 'ascii')
    files = sorted(p for p in dist.iterdir() if p.is_file()
                   and p.name != 'flash_manifest.json')
    manifest = {
        'package': 'VGL Patrol 1.6 persistent identity reset',
        'status': 'experimental_requires_first_board_validation',
        'hardware_validated': False,
        'firmware_file': SERVICE,
        'approved_restore_file': APPROVED,
        'reset_scope': 'activation byte and 22 ASCII-zero secret/serial bytes; other external data retained',
        'files': [{'file': p.name, 'size_bytes': p.stat().st_size,
                   'sha256': digest(p)} for p in files],
    }
    manifest_path = dist / 'flash_manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', 'ascii')
    if zip_path:
        zip_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as z:
            for p in files + [manifest_path]:
                info = zipfile.ZipInfo(p.name, (2026, 10, 29, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                z.writestr(info, p.read_bytes())
        with zipfile.ZipFile(zip_path) as z:
            assert z.testzip() is None
            assert len(z.namelist()) == len(files) + 1
            for p in files + [manifest_path]:
                assert z.read(p.name) == p.read_bytes()
    print(json.dumps({'files': len(files) + 1,
                      'zip': str(zip_path) if zip_path else None,
                      'zip_sha256': digest(zip_path) if zip_path else None}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--base-dir', type=Path, required=True)
    parser.add_argument('--zip', type=Path)
    args = parser.parse_args()
    prepare(args.base_dir, args.zip)
