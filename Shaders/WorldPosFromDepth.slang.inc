vec3 WorldPosFromDepth(float depth) {
    vec4 ndc = vec4(a_UV * 2.0 - 1.0, depth, 1.0);

    vec4 clip = a_PushConsts.InvProjMatrix * ndc;
    vec4 view = a_PushConsts.InvViewMatrix * (clip / clip.w);
    vec3 result = view.xyz;

    return result;
}
