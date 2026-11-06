#!/usr/bin/env python3
"""Uploads the symbols of a build to the telemetry server, so that its crash
reports show function names. Standard library only.

    upload-symbols.py http://server:8787 SYMBOLS_KEY aguarapos-simbolos-1.0.3-beta.json.gz

The key is the server's symbols_key, not the one of the tills.
AGUARA_SYMBOLS_KEY can hold it instead: then pass - as the key.
"""
import json
import os
import sys
import urllib.error
import urllib.request


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    url, key, path = sys.argv[1:]
    if key == '-':
        key = os.environ.get('AGUARA_SYMBOLS_KEY', '')
    with open(path, 'rb') as source:
        body = source.read()
    request = urllib.request.Request(url.rstrip('/') + '/api/v1/symbols', data=body)
    request.add_header('Authorization', 'Bearer ' + key)
    request.add_header('Content-Type', 'application/gzip')
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            print('%d modules uploaded.' % json.load(response)['modules'])
    except urllib.error.HTTPError as problem:
        sys.exit('The server refused it (%d): %s' % (problem.code, problem.read().decode('utf-8', 'replace')[:200]))
    except urllib.error.URLError as problem:
        sys.exit('No connection with the server: %s' % problem.reason)


if __name__ == '__main__':
    main()
