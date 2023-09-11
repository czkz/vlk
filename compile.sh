#!/bin/sh
fd -e vert -e frag -e geom -e glsl . shaders/ -x glslc {} -o {}.spv
