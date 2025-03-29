Bzip2
=====

This project is a macOS specific clone of [GitLab.com](https://gitlab.com/bzip2/bzip2).

This is Bzip2/libbz2; a program and library for lossless, block-sorting data
compression.

The documentation here may differ from that on the Bzip2 1.0.x project page
maintained by Mark Wielaard on [sourceware.org](https://sourceware.org/bzip2/).

The document of the Bzip2 feature development effort hosted on
[GitLab.com](https://gitlab.com/bzip2/bzip2) differ from this project.

Copyright (C) 1996-2010 Julian Seward <jseward@acm.org>

Copyright (C) 2019-2020 Federico Mena Quintero <federico@gnome.org>

Copyright (C) 2021 [Micah Snyder](https://gitlab.com/micahsnyder).

Please read the [WARNING](#warning), [DISCLAIMER](#disclaimer) and
[PATENTS](#patents) sections in this file for important information.

This program is released under the terms of the license contained in the
[COPYING](COPYING) file.

------------------------------------------------------------------

This version is not fully compatible with the previous public releases.

A documentation is available in a plain-text version of the
manual page is available as bzip2.txt.

## Contributing to Bzip2's development

This Bzip2 project is hosted on GitHub for specific macOS development work.
It can be found at https://github.com/bastie/BZipClone.


## Report a Bug

Please report bugs via [GitHub Issues](https://github.com/bastie/BZipClone/issues).

Before you create a new issue, please verify that no one else has already reported the same issue.

## Compiling Bzip2 and libbz2

```sh
xcodebuild -alltargets
```

## WARNING

This program and library (attempts to) compress data by performing several
non-trivial transformations on it. Unless you are 100% familiar with *all* the
algorithms contained herein, and with the consequences of modifying them, you
should NOT meddle with the compression or decompression machinery.
Incorrect changes can and very likely *will* lead to disastrous loss of data.

**Please contact the maintainers if you want to modify the algorithms.**

## DISCLAIMER

**I TAKE NO RESPONSIBILITY FOR ANY LOSS OF DATA ARISING FROM THE USE OF THIS
PROGRAM/LIBRARY, HOWSOEVER CAUSED.**

Every compression of a file implies an assumption that the compressed file can
be decompressed to reproduce the original. Great efforts in design, coding and
testing have been made to ensure that this program works correctly.

However, the complexity of the algorithms, and, in particular, the presence of
various special cases in the code which occur with very low but non-zero
probability make it impossible to rule out the possibility of bugs remaining in
the program.

DO NOT COMPRESS ANY DATA WITH THIS PROGRAM UNLESS YOU ARE PREPARED TO ACCEPT
THE POSSIBILITY, HOWEVER SMALL, THAT THE DATA WILL NOT BE RECOVERABLE.

That is not to say this program is inherently unreliable.
Indeed, I very much hope the opposite is true.
Bzip2/libbz2 has been carefully constructed and extensively tested.

## PATENTS

To the best of my knowledge, Bzip2/libbz2 does not use any patented algorithms.
However, I do not have the resources to carry out a patent search.
Therefore I cannot give any guarantee of the above statement.

## Maintainers

As of June 2021, [Micah Snyder](https://gitlab.com/micahsnyder) is the
maintainer of Bzip2/libbz2 for feature development work (I.e. versions 1.1+).

The Bzip2 feature development project is hosted on GitLab and can be found at
https://gitlab.com/bzip2/bzip2

Bzip2 version 1.0 is maintained by [Mark Wielaard](https://www.klomp.org/mark/)
at Sourceware and can be found at https://sourceware.org/git/?p=bzip2.git

### Special thanks

Thanks to Julian Seward, the original author of Bzip2/libbz2, for creating the
program and making it a very compelling alternative to previous compression
programs back in the early 2000's. Thanks to Julian also for letting Federico,
Mark, and Micah carry on with the maintainership of the program.
