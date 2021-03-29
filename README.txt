TextureDefrag

A reference implementation for the paper ``Texture Defragmentation for
Photo-Reconstructed 3D Models'' by Andrea Maggiordomo, Paolo Cignoni and
Marco Tarini.

Copyright (C) 2021 - Andrea Maggiordomo, Paolo Cignoni and Marco Tarini


Dependencies: QT5

Building using qmake on Linux with GCC should be straightforward:

  mkdir build && cd build
  qmake ../texture-defrag/texture-defrag.pro -spec linux-g++
  make

The executable takes as arguments the input mesh file and optional parameters
to control the various steps of the texture defragmentation algorithm.
Execute the binary without arguments for usage info and further details.

