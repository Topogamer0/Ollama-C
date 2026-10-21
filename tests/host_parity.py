#!/usr/bin/env python3
"""
Compares the C host parser with the Python client on the same inputs.

Usage: host_parity.py <host_cli executable>

_parse_host() below is the function of ollama-python 0.6.3 (MIT License,
Copyright (c) Ollama), run with the urllib of this Python. The checks of
current CPython (3.11+, _check_bracketed_netloc) are required: with an older
urllib the test is skipped (exit code 77).
"""

import ipaddress
import random
import re
import subprocess
import sys
import urllib.parse

SKIP = 77


def _parse_host(host):
  host, port = host or '', 11434
  scheme, _, hostport = host.partition('://')
  if not hostport:
    scheme, hostport = 'http', host
  elif scheme == 'http':
    port = 80
  elif scheme == 'https':
    port = 443

  split = urllib.parse.urlsplit(f'{scheme}://{hostport}')
  host = split.hostname or '127.0.0.1'
  port = split.port or port

  try:
    if isinstance(ipaddress.ip_address(host), ipaddress.IPv6Address):
      # Fix missing square brackets for IPv6 from urlsplit
      host = f'[{host}]'
  except ValueError:
    ...

  if path := split.path.strip('/'):
    return f'{scheme}://{host}:{port}/{path}'

  return f'{scheme}://{host}:{port}'


def python(host):
  try:
    return 'OK ' + _parse_host(host)
  except ValueError:
    return 'ERR'


C0_OR_SPACE = ''.join(chr(i) for i in range(33))


def comparable(host):
  """Leaves out the documented divergences."""
  scheme, sep, hostport = host.partition('://')
  if sep and hostport:
    # Python only splits a host after a valid scheme; other schemes give unusable URLs
    clean = scheme.lstrip(C0_OR_SPACE)
    for c in '\t\r\n':
      clean = clean.replace(c, '')
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9+.-]*', clean) or clean != scheme:
      return False
  # IPvFuture literals: the C parser only accepts RFC 3986 characters, and
  # Python versions differ on the case of the 'v'
  try:
    hostname = urllib.parse.urlsplit('http://' + (hostport if sep and hostport else host)).hostname or ''
  except ValueError:
    hostname = ''
  return not re.search(r'\[\s*[vV]', host) and not re.match(r'[vV][0-9a-fA-F]+\.', hostname)


FIXED = [
  '', '1.2.3.4', ':56789', '1.2.3.4:56789', 'http://1.2.3.4', 'https://1.2.3.4:56789', 'example.com',
  'example.com:56789/path/', 'https://example.com:56789/path', '[0001:002:003:0004::1]',
  '[0001:002:003:0004::1]:56789/path/', 'Example.COM', 'user:pass@example.com:1234', 'example.com:1234?x=1',
  'example.com:', 'example.com:0', 'example.com:000080', '[::ffff:1.2.3.4]:8', '[fe80::1%Eth0]',
  '[1:2:3:4:5:6:7::]', '[::1', '::1]', 'http://[nope]:80', '[1.2.3.4]', '[1::2::3]', '[1:2:3:4:5:6:7:8:9]',
  '[::1.2.3.04]', 'example.com:abc', 'example.com:65536', '[::1]:99999', 'http://user:p[w@[::1]:11434',
  'u[x]@[::1]', 'u]@[::1', '[::1]@example.com', 'x[::1]', '[::1]x:80', '[::1]]', '[::1]:80@x',
  '192.168.1.10:11434\r', 'http://localhost:11434\r\n', 'local\thost', '[::1]:11434\r', 'a%B', '[]',
  'http://', 'https://[::1]', '0.0.0.0', 'localhost:11434/api/', '[::]:1', '[::1%]', '[::1%a%b]',
]

HOSTS = ['localhost', 'Example.COM', '1.2.3.4', '0.0.0.0', '::1', '[::1]', '[FE80::1%Eth0]', '[::ffff:1.2.3.4]',
         '[1:2:3:4:5:6:7::]', '[1::2::3]', '[1.2.3.4]', '[]', '[::1', '::1]', 'x[::1]', '[::1]x', '[::1]]', '',
         '[0:0:0:0:0:0:0:0]', '[::1.2.3.256]', '[12345::]', '[::g]', 'a%B', '[::1%25x]']
PORTS = ['', '', ':', ':80', ':11434', ':000080', ':0', ':65535', ':65536', ':abc', ':8\r', ':1:2', ':+1', ': 1']
PATHS = ['', '', '/', '/api', '/a/b/', '?q', '#f', '/x?y#z']
ALPHABET = 'abXY09.-_~%@:[]/?#\t\r\n !'


def random_text(r, n):
  return ''.join(r.choice(ALPHABET) for _ in range(r.randint(0, n)))


def random_host(r):
  scheme = r.choice(['', '', 'http://', 'https://'])
  user = r.choice(['', '', '', 'u@', 'u:p@', '[x]@', random_text(r, 4) + '@'])
  host = r.choice(HOSTS) if r.random() < 0.7 else random_text(r, 8)
  value = scheme + user + host + r.choice(PORTS) + r.choice(PATHS)
  if r.random() < 0.15:
    i = r.randint(0, len(value))
    value = value[:i] + r.choice('\t\r\n') + value[i:]
  return value


def main():
  if len(sys.argv) != 2:
    print(__doc__)
    return 2
  if not hasattr(urllib.parse, '_check_bracketed_netloc'):
    print(f'Python {sys.version.split()[0]} predates the current urlsplit() checks: skipped')
    return SKIP

  r = random.Random(2026)
  hosts = FIXED + [random_host(r) for _ in range(20000)]
  hosts = [h for h in hosts if comparable(h)]
  stdin = ''.join(h.encode('utf-8').hex() + '\n' for h in hosts).encode()
  lines = subprocess.run([sys.argv[1]], input=stdin, capture_output=True, check=True).stdout.decode().splitlines()
  assert len(lines) == len(hosts), (len(lines), len(hosts))

  failures = 0
  for host, line in zip(hosts, lines):
    c = 'OK ' + bytes.fromhex(line[3:]).decode('utf-8') if line.startswith('OK ') else 'ERR'
    py = python(host)
    if c != py:
      failures += 1
      if failures <= 20:
        print(f'{host!r}: python {py!r}, C {c!r}')
  print(f'{len(hosts)} hosts compared, {failures} differences')
  return 1 if failures else 0


if __name__ == '__main__':
  sys.exit(main())
