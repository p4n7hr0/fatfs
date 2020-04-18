fatfs
======
`fatfs` is a simple, low memory and posix-like code created for study purposes. It aims to be compatible with fat12/16/32 variants in a minimal sense - only the features needed to parse, read from and write to the filesystem will be implemented (extensions such as long names may be implemented in the future).

`WARNING: This is not a serious filesystem development, nor aim to be a replacement for any good implementation. ` 

Status
------
#### volume functions
  - *mount, umount, getlabel* (completed)
#### directory functions
  - *getroot, opendir, readdir, closedir, rewinddir* (completed)
#### file functions
  - *open, read, seek, close* (completed)
  - *write, truncate* (on going)
#### other
  - *testing tools* (on going)
  - *file creation, directory creation* (future)
  - *data/time, long name, volume id* (future)

License
-------

    Copyright (C) 2020 p4n7hr0

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
