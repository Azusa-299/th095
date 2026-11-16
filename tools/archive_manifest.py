"""Read-only archive audit; writes decoded resources and hashes under build/."""
import argparse
import csv
import hashlib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    archive = args.archive.resolve()
    executable = ROOT / 'build/th95.exe'
    output = ROOT / 'build' / f'audit-{archive.stem}'
    subprocess.run([str(executable), '--verify', str(archive)], check=True, timeout=30)
    subprocess.run([str(executable), '--extract', str(output), str(archive)],
                   check=True, timeout=30)
    files = sorted(path for path in output.rglob('*') if path.is_file())
    manifest = ROOT / 'build' / f'{archive.stem}-manifest.csv'
    with manifest.open('w', encoding='utf-8', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(('name', 'decoded_bytes', 'sha256'))
        for path in files:
            writer.writerow((path.relative_to(output).as_posix(), path.stat().st_size, digest(path)))
    print(f'Archive SHA256: {digest(archive)}')
    print(f'Manifest: {manifest} ({len(files)} files)')
    print('Hashes fingerprint this decoder output; original-loader byte parity is not established.')


if __name__ == '__main__':
    main()
