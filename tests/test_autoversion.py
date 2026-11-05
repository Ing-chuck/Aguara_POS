#!/usr/bin/env python3
"""autoVersion.py on a throwaway repository: run by tests/run-tests.sh."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / 'autoVersion.py'

VERSION_H = '#define VERSION_MAJOR 1\r\n#define VERSION_MINOR 2\r\n#define VERSION_PATCH 3\r\n'
API_H = '// fingerprint: 0\n#define AGUARA_API_VERSION "4"\n'
SERVICE_H = '// what it does\nstruct Sale { int total; };\n#define NAME "a  b // c"\n'

GIT_IDENTITY = {'GIT_AUTHOR_NAME': 'test', 'GIT_AUTHOR_EMAIL': 'test@example.com',
                'GIT_COMMITTER_NAME': 'test', 'GIT_COMMITTER_EMAIL': 'test@example.com'}


class AutoVersion(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.interfaces = self.root / 'src' / 'interfaces'
        self.interfaces.mkdir(parents=True)
        shutil.copy(str(TOOL), str(self.root))
        self.put('version.h', VERSION_H)
        self.put('apiversion.h', API_H)
        self.put('service.h', SERVICE_H)
        # a level that matches the headers, as a repository starts out
        self.assertEqual(self.tool('api').returncode, 0)
        self.put('apiversion.h', self.get('apiversion.h').replace('"5"', '"4"'))
        self.git('init', '-q')

    def put(self, name, text):
        (self.interfaces / name).write_bytes(text.encode())

    def get(self, name):
        return (self.interfaces / name).read_bytes().decode()

    def tool(self, *args):
        return subprocess.run([sys.executable, '-B', str(self.root / 'autoVersion.py')] + list(args),
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, env=dict(os.environ, **GIT_IDENTITY))

    def git(self, *args):
        subprocess.run(['git', '-C', str(self.root)] + list(args), check=True,
                       env=dict(os.environ, **GIT_IDENTITY))

    def commit(self):
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'change')

    def test_comments_and_spacing_are_not_the_api(self):
        self.put('service.h', '/* new note */\r\nstruct  Sale {\r\n  int total;  // the sum\r\n};\r\n'
                              '#define NAME "a  b // c"\r\n')
        self.assertEqual(self.tool('check').returncode, 0)

    def test_changing_a_data_type_needs_the_level_raised(self):
        self.put('service.h', SERVICE_H.replace('int total;', 'int total; int tax;'))
        failed = self.tool('check')
        self.assertEqual(failed.returncode, 1)
        self.assertIn('"python autoVersion.py api"', failed.stdout)

        self.assertEqual(self.tool('api').returncode, 0)
        self.assertIn('"5"', self.get('apiversion.h'))
        self.assertEqual(self.tool('check').returncode, 0)

    def test_text_inside_a_string_is_the_api(self):
        self.put('service.h', SERVICE_H.replace('a  b // c', 'a b // d'))
        self.assertEqual(self.tool('check').returncode, 1)

    def test_a_new_header_is_the_api(self):
        self.put('other.h', 'struct Other {};\n')
        self.assertEqual(self.tool('check').returncode, 1)

    def test_the_level_stays_when_nothing_changed(self):
        self.assertEqual(self.tool('api').returncode, 1)
        self.assertIn('"4"', self.get('apiversion.h'))

    def test_bumps_reset_what_comes_after_and_keep_line_endings(self):
        self.tool('bump', 'patch')
        self.assertIn('VERSION_PATCH 4\r\n', self.get('version.h'))
        self.tool('bump', 'minor')
        self.assertIn('VERSION_MINOR 3\r\n#define VERSION_PATCH 0\r\n', self.get('version.h'))
        self.tool('bump', 'major')
        self.assertEqual(self.get('version.h'),
                         '#define VERSION_MAJOR 2\r\n#define VERSION_MINOR 0\r\n#define VERSION_PATCH 0\r\n')

    def test_no_release_with_the_api_unrecorded(self):
        self.put('service.h', SERVICE_H.replace('int total;', 'long total;'))
        self.assertEqual(self.tool('bump', 'patch').returncode, 1)
        self.assertIn('VERSION_PATCH 3', self.get('version.h'))

    def test_a_tag_needs_a_clean_tree(self):
        self.assertEqual(self.tool('tag').returncode, 1)      # nothing committed yet
        self.commit()
        self.assertEqual(self.tool('tag').returncode, 0)
        self.assertEqual(self.tool('tag').returncode, 1)      # already there

    def test_a_released_version_cant_build_other_code(self):
        self.commit()
        self.tool('tag')
        self.assertEqual(self.tool('check', '--release').returncode, 0)

        (self.root / 'fix.cpp').write_text('int fix;\n')
        self.assertEqual(self.tool('check', '--release').returncode, 1)
        self.commit()
        self.assertEqual(self.tool('check', '--release').returncode, 1)

        self.tool('bump', 'patch')
        self.assertEqual(self.tool('check', '--release').returncode, 0)

    def test_the_real_tree_is_consistent(self):
        done = subprocess.run([sys.executable, '-B', str(TOOL), 'check'], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, universal_newlines=True)
        self.assertEqual(done.returncode, 0, done.stdout)


if __name__ == '__main__':
    unittest.main()
