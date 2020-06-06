#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Reads the contents of a manifest file generated by the build and verifies
   that there are no collisions among destination paths.
   '''

import argparse
import collections
import json
import sys

Entry = collections.namedtuple('Entry', ['source', 'destination', 'label'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to original manifest', required=True)
    parser.add_argument(
        '--output', help='Path to the updated manifest', required=True)
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        entries = json.load(input_file)

    entries = [Entry(**e) for e in entries]
    entries_by_dest = {
        d: set(e for e in entries if e.destination == d) for d in set(
            e.destination for e in entries)
    }
    conflicts = {d: e for d, e in entries_by_dest.iteritems() if len(e) > 1}
    if conflicts:
        for destination in conflicts:
            print('Conflicts for path ' + destination + ':')
            for conflict in conflicts[destination]:
                print(' - ' + conflict.source)
                print('   from ' + conflict.label)
        print('Error: conflicting distribution entries!')
        return 1

    with open(args.output, 'w') as output_file:
        json.dump(
            sorted(e._asdict() for e in entries),
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))

    return 0


if __name__ == '__main__':
    sys.exit(main())
