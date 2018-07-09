
import os
import sys
import argparse
from pprint import pprint
import subprocess
from itertools import chain
import tempfile

def valid_directory(s):
    if os.path.exists(s) and os.path.isdir(s):
        return os.path.abspath(s)
    raise argparse.ArgumentTypeError('{0} is not a valid path'.format(s))

parser = argparse.ArgumentParser()
parser.add_argument('build_path', help='Path to a completed build.', type=valid_directory)
parser.add_argument('files', nargs='+', action='append', help='Source files to analyze.')
parser.add_argument('-o', '--output', help='save pdf output to file instead of displaying')
args = parser.parse_args(sys.argv[1:])
pprint(parser.parse_args(sys.argv[1:]))

# grab include paths from the build directory
try:
    output = subprocess.check_output('cmake --build . --target get_include_paths', cwd=args.build_path, shell=True, universal_newlines=True)
except subprocess.CalledProcessError as ex:
    print(ex)
    sys.exit(ex.returncode)

strip_str = '\x1b[32m \x1b[0'
# find lines that start with -I
lines = [l.strip(strip_str).split(' ', 1)[1][1:-1] for l in output.split('\n') if l.strip(strip_str).startswith('-I')]
# pprint(lines)

source_path = os.path.abspath(os.path.join(lines[0], '..', '..'))
# print(source_path)

cmd = [os.path.join(os.path.dirname(os.path.abspath(__file__)), 'analyze.exe')] + \
      [f for f in '-fms-extensions -fms-compatibility -fms-compatibility-version=19 -std=c++14 -D NOMINMAX -D USE_FIXVEC_POOL -D E2PM'.split(' ')] + \
      list(chain.from_iterable((('-I', '"{}"'.format(p)) for p in lines))) + \
      ['"{}"'.format(os.path.join(source_path, 'src', f)) for f in args.files[0]]
# print(' '.join(cmd))
try:
    dot = subprocess.check_output(' '.join(cmd), stderr=subprocess.PIPE)
except subprocess.CalledProcessError as ex:
    print(ex.stderr)
    print(ex)
    sys.exit(ex.returncode)
except FileNotFoundError as ex:
    print(ex)
    sys.exit(-1)

# print(dot)
try:
    p = subprocess.Popen('dot -T pdf', stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    pdf, err = p.communicate(dot)
    #pdf = subprocess.check_output('dot -T pdf', input=dot, stderr=subprocess.PIPE, shell=True)
except subprocess.CalledProcessError as ex:
    print(ex.stderr)
    print(ex)
    sys.exit(ex.returncode)

tmp = tempfile.NamedTemporaryFile(suffix='.pdf', delete=False)
tmp.write(pdf)
name = tmp.name
tmp.close()

if args.output:
    open(args.output, 'wb+').write(pdf)
else:
    try:
        if sys.platform == 'win32':
            if os.path.exists(r'C:\Program Files (x86)\Adobe\Acrobat Reader DC\Reader\AcroRd32.exe'):
                subprocess.call(r'"C:\Program Files (x86)\Adobe\Acrobat Reader DC\Reader\AcroRd32.exe" {0}'.format(name), shell=True)
            else:
                subprocess.call(name, shell=True)
    finally:
        os.remove(name)
