
#!/usr/bin/env python

import sys
import os
import re
import subprocess
from pprint import pprint

variables = {} 
variables['version'] = '1.0'
variables['build_path'] = '"{0}"'.format(os.path.abspath(os.path.join(os.path.dirname(sys.argv[1]), '../')).replace('\\', '/'))
variables['source_path'] = '"{0}"'.format(os.getcwd())

if not os.path.exists(os.path.dirname(sys.argv[1])):
    os.makedirs(os.path.dirname(sys.argv[1]))

pprint(variables)

outfile = open(sys.argv[1], 'w+')
outfile.write('''
#ifndef PIPELINE_VERSION_H_
#define PIPELINE_VERSION_H_

#define PIPELINE_VERSION {version}
#define PIPELINE_BUILD_PATH {build_path}
#define PIPELINE_SOURCE_PATH {source_path}

#endif

'''.format(**variables))


