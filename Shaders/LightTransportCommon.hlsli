/// Runtime bounce of the lights that were not baked into the lightmap, over the cluster links that mapc stores in the
/// .fxmap. Layouts match CompiledMapTransport* in Src/Map/CompiledMap.hpp and LightTransportConsts in
/// Src/Renderer/LightTransport.hpp.

struct LightTransportChart
{
	uint2 vMin;
	uint2 vSize;
	uint2 vBlocks;
	uint1 uiFirstCluster;
	uint1 uiPadding;
};

struct LightTransportCluster
{
	float3 vAlbedo;
	uint1 uiFirstLink;
	uint1 uiLinkCount;
	uint1 uiFirstSample;
	uint1 uiSampleCount;
	uint1 uiPadding;
};

struct LightTransportSample
{
	float4 vPosition;
	float4 vNormal;
};

struct LightTransportLink
{
	uint1 uiSource;
	float1 fWeight;
};

#define LIGHT_TRANSPORT_FLAG_SHOW 0x1

/// No chart covers the texel
#define LIGHT_TRANSPORT_NO_CHART 0xFFFF

/// Clusters per row of bounce groups, so maps with more than 65535 clusters still fit in a dispatch
#define LIGHT_TRANSPORT_BOUNCE_ROW 1024

struct LightTransportConsts
{
	uint uiFirstLight;
	uint uiLightCount;
	uint uiClusterCount;
	uint uiClusterSize;

	/// Offsets into the state buffer of last frame's indirect light and this frame's
	uint uiReadOffset;
	uint uiWriteOffset;
	uint uiFlags;
	uint uiPadding;

	uint2 vLightmapSize;
	uint2 vPadding;
};
