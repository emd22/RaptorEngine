F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightTransportCommon.hlsli"

F_StructBuffer(bClusters, LightTransportCluster, 0, 0);
F_StructBuffer(bLinks, LightTransportLink, 1, 0);
F_RWStructBuffer(bState, float4, 2, 0);

F_FrameData(LightTransportConsts, Consts, 1);

#define BOUNCE_GROUP_SIZE 64

groupshared float3 sPartialSums[BOUNCE_GROUP_SIZE];

/// One group per receiving cluster, gathering what its linked clusters send out
[numthreads(BOUNCE_GROUP_SIZE, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
	const uint cluster_index = group.x + group.y * LIGHT_TRANSPORT_BOUNCE_ROW;
	const bool is_cluster = cluster_index < Consts.uiClusterCount;

	const uint safe_index = select(is_cluster, cluster_index, 0u);
	const LightTransportCluster cluster = bClusters[safe_index];
	const uint link_count = select(is_cluster, cluster.uiLinkCount, 0u);

	float3 sum = float3(0.0, 0.0, 0.0);

	for (uint link_index = local.x; link_index < link_count; link_index += BOUNCE_GROUP_SIZE) {
		const LightTransportLink link = bLinks[cluster.uiFirstLink + link_index];
		sum += bState[link.uiSource].rgb * link.fWeight;
	}

	sPartialSums[local.x] = sum;
	GroupMemoryBarrierWithGroupSync();

	[unroll]
	for (uint stride = BOUNCE_GROUP_SIZE / 2; stride > 0; stride /= 2) {
		if (local.x < stride) {
			sPartialSums[local.x] += sPartialSums[local.x + stride];
		}

		GroupMemoryBarrierWithGroupSync();
	}

	if (and(local.x == 0, is_cluster)) {
		bState[Consts.uiWriteOffset + cluster_index] = float4(sPartialSums[0], 0.0);
	}
}
