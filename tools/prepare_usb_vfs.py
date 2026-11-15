# SPDX-License-Identifier: GPL-3.0-or-later
"""Adapt CherryUSB's DFS bridge to newlib without editing vendor sources."""
from __future__ import print_function
import io
import os
import sys

with io.open(sys.argv[1], 'r', encoding='utf-8') as stream:
    text = stream.read()
include = '#include "usb_vfs.h"'
assignment = 'buf->st_mtime = st.st_mtime;'
if text.count(include) != 1 or text.count(assignment) != 1:
    raise RuntimeError('CherryUSB DFS bridge changed; review the st_mtime adaptation')
text = text.replace(include, '''/* Application compatibility with the current newlib stat layout. */
#ifdef st_mtime
#undef st_mtime
#define RTCARPLAY_STAT_MTIME(value) ((value).st_mtim.tv_sec)
#else
#define RTCARPLAY_STAT_MTIME(value) ((value).st_mtime)
#endif
''' + include)
text = text.replace(assignment, 'buf->st_mtime = RTCARPLAY_STAT_MTIME(st);')
directory = os.path.dirname(sys.argv[2])
if not os.path.isdir(directory):
    os.makedirs(directory)
with io.open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as stream:
    stream.write(text)
