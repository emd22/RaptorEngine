# Prototypes

Raptor has a concept called prototypes, which are a cross between the functionality of a blockout and a brush system. This is to build the basis of maps quickly and compile them into efficient meshes and colliders to add detail over.

There are a two main types of brushes:

- Vis Brushes (shortened to V in the editor)
- Data Brushes (shortened to D in the editor)

Vis brushes are visible to the player and in final baked maps. They have a materials, collision, and react to lighting (unless marked unlit).

Data brushes are solely for storing data and bounds, or portraying an abstract concept in the engine. Some examples of data brushes include:

- Spawn locations
- Probe volumes + Reflection probes
- Lights
- DBounds

As well as these two categories of brushes, there are two other brushes that are special to the engine.

- DBounds – A brush that have no visibility and no collision, but mark an end of a map to the map compiler.
- nonvis – A Non-Visible Brush that has collision but is omitted from mesh construction during map compilation. This is somewhat equivalent to a caulk brush in Quake/CoD radiant. Note that nonvis brushes still cast shadows, but do not contribute to the indirect lighting.

# Maps

Maps are compiled through the `mapc` tool, which comes with the `mapc-gui` frontend. Use these tools to build out final versions of maps and build the indirect lighting grid. Note that after compilation, you still need to bake the probes in-engine using the `World > Rebake Probes` option.

Compiled maps store the following information:

- Lightmaps with irradiance and shadows included.
- Shadows for level geometry, split per world tile. This is used for the froxel volumetric lighting.
- The level geometry
- Points that irradiance and reflection probes should be placed, and a link to an external probe file.
- The area graph for portal culling.
- Lights, meshes, renderer options, and other miscellaneous information.
