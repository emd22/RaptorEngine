#!/bin/sh

glslc -fshader-stage=vertex main.vert.glsl -o main.vert.spv
glslc -fshader-stage=fragment main.frag.glsl -o main.frag.spv

glslc -fshader-stage=vertex composition.vert.glsl -o composition.vert.spv
glslc -fshader-stage=fragment composition.frag.glsl -o composition.frag.spv

glslc -fshader-stage=vertex lighting.vert.glsl -o lighting.vert.spv
glslc -fshader-stage=fragment lighting.frag.glsl -o lighting.frag.spv

glslc -fshader-stage=vertex Skybox/Skybox.vs -o Skybox.vs.spv
glslc -fshader-stage=fragment Skybox/Skybox.fs -o Skybox.fs.spv
