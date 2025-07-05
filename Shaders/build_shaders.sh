#!/bin/sh

glslc -fshader-stage=vertex main.vert.glsl -o main.vert.spv
glslc -fshader-stage=fragment main.frag.glsl -o main.frag.spv
