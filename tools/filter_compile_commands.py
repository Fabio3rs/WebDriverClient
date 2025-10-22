#!/usr/bin/env python3
import json
import sys
from pathlib import Path

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Usage: filter_compile_commands.py <in> <out>')
        sys.exit(2)
    inp = Path(sys.argv[1])
    out = Path(sys.argv[2])
    data = json.loads(inp.read_text())
    bad_flags = ['-Wduplicated-branches', '-Wduplicated-cond', '-Wlogical-op', '-Wuseless-cast']

    def filter_args(argv):
        new = []
        for a in argv:
            skip = False
            for b in bad_flags:
                if a.startswith(b):
                    skip = True
                    break
            if not skip:
                new.append(a)
        return new

    for entry in data:
        if 'arguments' in entry:
            entry['arguments'] = filter_args(entry['arguments'])
        elif 'command' in entry:
            # naive split - keep simple
            parts = entry['command'].split()
            parts = filter_args(parts)
            entry['command'] = ' '.join(parts)
    out.write_text(json.dumps(data, indent=2))
    print(f'Wrote filtered compilation database to {out}')
