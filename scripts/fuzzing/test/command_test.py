#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

import test_env
import lib.command as command
from test_case import TestCase


class CommandTest(TestCase):

    # Unit tests

    def test_list_fuzzers(self):
        # No match
        args = self.parse_args('list', 'no/match')
        command.list_fuzzers(args, self.factory)
        self.assertLogged('No matching fuzzers.')

        # Multiple matches
        args = self.parse_args('list', 'fake-package1')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 3 matching fuzzers:',
            '  fake-package1/fake-target1',
            '  fake-package1/fake-target2',
            '  fake-package1/fake-target3',
        )

        # Exact match
        args = self.parse_args('list', 'fake-package1/fake-target1')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 1 matching fuzzers:',
            '  fake-package1/fake-target1',
        )

    def test_start_fuzzer(self):
        # In the foreground
        output = 'output'
        self.cli.mkdir(output)
        args = self.parse_args(
            'start',
            '--foreground',
            '--output',
            output,
            'fake-package1/fake-target1',
        )
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            'Starting fake-package1/fake-target1.',
            'Outputs will be written to: output',
        )

        # In the background
        args = self.parse_args(
            'start', '--output', output, 'fake-package1/fake-target1')
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            'Starting fake-package1/fake-target1.',
            'Outputs will be written to: output',
            'Check status with "fx fuzz check fake-package1/fake-target1".',
            'Stop manually with "fx fuzz stop fake-package1/fake-target1".',
        )
        cmd = [
            'python',
            sys.argv[0],
            'start',
            '--monitor',
            '--output',
            output,
            'fake-package1/fake-target1',
        ]
        self.assertRan(*cmd)

        args = self.parse_args(
            'start',
            '--monitor',
            '--output',
            output,
            'fake-package1/fake-target1',
        )
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target1 has stopped.',
            'Output written to: output.',
        )

    def test_check_fuzzer(self):
        # No name, none running
        args = self.parse_args('check')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'No fuzzers are running.',
            'Include \'name\' to check specific fuzzers.',
        )

        # No name, some running
        self.set_running('fake-package1', 'fake-target1')
        self.set_running('fake-package1', 'fake-target2')
        self.set_running('fake-package1', 'fake-target3')
        args = self.parse_args('check')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target1: RUNNING',
            '    Output path:  fuchsia_dir/local/fake-package1_fake-target1',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:    0',
            'fake-package1/fake-target2: RUNNING',
            '    Output path:  fuchsia_dir/local/fake-package1_fake-target2',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:    0',
            'fake-package1/fake-target3: RUNNING',
            '    Output path:  fuchsia_dir/local/fake-package1_fake-target3',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:    0',
        )

        # Name provided, running
        args = self.parse_args('check', 'fake-package1/fake-target2')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target2: RUNNING',
            '    Output path:  fuchsia_dir/local/fake-package1_fake-target2',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:    0',
        )

        # Name provided, not running
        args = self.parse_args('check', 'fake-package2/fake-target1')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package2/fake-target1: STOPPED',
            '    Output path:  fuchsia_dir/local/fake-package2_fake-target1',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:    0',
        )

    def test_stop_fuzzer(self):
        # Not running
        args = self.parse_args('stop', 'fake-package1/fake-target3')
        command.stop_fuzzer(args, self.factory)
        self.assertLogged('fake-package1/fake-target3 is already stopped.')

        # Running
        self.set_running('fake-package1', 'fake-target3')
        args = self.parse_args('stop', 'fake-package1/fake-target3')
        command.stop_fuzzer(args, self.factory)
        self.assertLogged('Stopping fake-package1/fake-target3.')

    def test_repro_units(self):
        unit = 'crash-deadbeef'
        self.cli.touch(unit)
        args = self.parse_args('repro', 'fake-package1/fake-target3', unit)
        command.repro_units(args, self.factory)

    def test_analyze_fuzzer(self):
        args = self.parse_args('analyze', '-l', 'fake-package1/fake-target3')
        command.analyze_fuzzer(args, self.factory)

        # We shouldn't have copied anything
        self.assertFalse([cmd for cmd in self.cli.processes if 'gs' in cmd])

        # Invalid corpus
        corpus1 = 'corpus1'
        corpus2 = 'corpus2'
        local_dict = 'local_dict'
        package = 'fake-package1'
        executable = 'fake-target3'
        args = self.parse_args(
            'analyze', '-c', corpus1, '-c', corpus2, '-d', local_dict,
            '{}/{}'.format(package, executable))
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such directory: {}'.format(corpus1))

        self.cli.mkdir(corpus1)
        foo = os.path.join(corpus1, 'foo')
        bar = os.path.join(corpus1, 'bar')
        self.cli.touch(foo)
        self.cli.touch(bar)
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such directory: {}'.format(corpus2))

        # Invalid dictionary
        self.cli.mkdir(corpus2)
        baz = os.path.join(corpus2, 'baz')
        self.cli.touch(baz)
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such file: {}'.format(local_dict))

        self.cli.touch(local_dict)
        with self.cli.temp_dir() as temp_dir:
            # Make it appear as if something was retrieved from GCS.
            qux = os.path.join(temp_dir.pathname, 'qux')
            self.cli.touch(qux)
        command.analyze_fuzzer(args, self.factory)
        gcs_url = 'gs://corpus.internal.clusterfuzz.com/libFuzzer/fuchsia_{}-{}'.format(
            package, executable)
        with self.cli.temp_dir() as temp_dir:
            cmd = ['gsutil', '-m', 'cp', gcs_url + '/*', temp_dir.pathname]
            self.assertRan(*cmd)

        abspath = self.fuzzer.ns.abspath(self.fuzzer.corpus.nspath)
        self.assertScpTo(bar, foo, abspath)
        self.assertScpTo(baz, abspath)
        self.assertEqual(
            self.fuzzer.dictionary.nspath, self.fuzzer.ns.data(local_dict))
        self.assertEqual(self.cli.log[0], 'Analyzing fuzzer...')


if __name__ == '__main__':
    unittest.main()
