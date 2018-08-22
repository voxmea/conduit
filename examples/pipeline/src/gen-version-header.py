
#!/usr/bin/env python

import sys
import os
import re
import subprocess
from pprint import pprint

checkout_version = subprocess.Popen('git rev-parse HEAD', shell=True, stdout=subprocess.PIPE).stdout.read()
checkout_diff = subprocess.Popen('git diff -w', shell=True, stdout=subprocess.PIPE).stdout.read()[:256]
variables = {}
variables['checkout'] = '"{0}"'.format(checkout_version.decode('utf-8').strip())
# order on the replacements is important, replace original backslash first.
variables['diff'] = '"\\n\\"{0}\\"\\n"'.format(checkout_diff.decode('utf-8').strip().replace('\\', '\\\\').replace('"', '\\"').replace('\r', '').replace('\n', '\\n'))
variables['build_path'] = '"{0}"'.format(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../').replace('\\', '/'))

if not os.path.exists(os.path.dirname(sys.argv[1])):
    os.makedirs(os.path.dirname(sys.argv[1]))

pprint(variables)

outfile = open(sys.argv[1], 'w+')
outfile.write('''
#ifndef CONDUIT_PROJECT_VERSION_H_
#define CONDUIT_PROJECT_VERSION_H_

#define CONDUIT_PROJECT_VERSION {checkout}
#define CONDUIT_PROJECT_BUILD_PATH {build_path}
#define CONDUIT_PROJECT_DIFF {diff}

#endif

'''.format(**variables))


