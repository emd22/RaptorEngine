#!/bin/sh

glslc -fshader-stage=vertex main.vert.glsl -o main.vert.spv
glslc -fshader-stage=fragment main.frag.glsl -o main.frag.spv

glslc -fshader-stage=vertex composition.vert.glsl -o composition.vert.spv
glslc -fshader-stage=fragment composition.frag.glsl -o composition.frag.spv
