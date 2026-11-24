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
mutators = (
    ('mkdir', 'const char *path', 'path', False),
    ('rmdir', 'const char *path', 'path', False),
    ('unlink', 'const char *path', 'path', False),
    ('open', 'const char *path, uint8_t mode', 'path, mode', True),
    ('write', 'int fd, const void *buf, size_t len', 'fd, buf, len', False),
)
for name, parameters, arguments, read_only_open in mutators:
    original = 'int vfs_{}({})'.format(name, parameters)
    if text.count(original) != 1:
        raise RuntimeError('CherryUSB DFS API changed: ' + original)
    text = text.replace(original,
                        '#ifdef RTCARPLAY_OTA\nstatic int stage_vfs_{}({})\n#else\n{}\n#endif'
                        .format(name, parameters, original))
text += '\n#ifdef RTCARPLAY_OTA\n#include "ota_lubanlite.h"\n'
for name, parameters, arguments, read_only_open in mutators:
    text += '\nint vfs_{}({})\n{{\n'.format(name, parameters)
    if read_only_open:
        text += '    if (mode == VFS_O_RDONLY)\n        return stage_vfs_open(path, mode);\n'
    text += ('    if (!carplay_ota_file_write_begin())\n'
             '        return VFS_ERR_AGAIN;\n'
             '    int result = stage_vfs_{}({});\n'
             '    carplay_ota_file_write_end();\n'
             '    return result;\n}}\n').format(name, arguments)
text += '#endif\n'
directory = os.path.dirname(sys.argv[2])
if not os.path.isdir(directory):
    os.makedirs(directory)
with io.open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as stream:
    stream.write(text)
