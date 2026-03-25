#!/bin/sh

# Outputs debug reflection data from slangc.

if [ $# -lt 3 ]; then
  echo "Usage: $0 [ShaderPath] [JsonOutputPath] [vertex|fragment]"
  exit
fi

entrypoint="VertexMain"


if [ "$3" = "fragment" ]; then
    entrypoint="FragmentMain"
fi

slangc $1 -reflection-json $2 -stage $3 -entry "$entrypoint" -target spirv
