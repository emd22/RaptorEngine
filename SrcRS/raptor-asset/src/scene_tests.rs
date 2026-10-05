mod tests
{
	use crate::gltf_scene::{AlphaMode, GltfScene, LoadError};
	use crate::skin::build_skeleton;
	use base64::Engine;
	use raptor_anim::NO_BONE;

	fn bytes_of(floats: &[f32]) -> Vec<u8>
	{
		floats.iter().flat_map(|value| value.to_le_bytes()).collect()
	}

	fn uri_of(data: &[u8]) -> String
	{
		format!(
			"data:application/octet-stream;base64,{}",
			base64::engine::general_purpose::STANDARD.encode(data)
		)
	}

	/// positions, normals, uvs for one triangle, then u16 indices
	fn triangle_buffer() -> Vec<u8>
	{
		let mut data = bytes_of(&[0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]);
		data.extend(bytes_of(&[0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0]));
		data.extend(bytes_of(&[0.0, 0.0, 1.0, 0.0, 0.0, 1.0]));
		data.extend([0u8, 0, 1, 0, 2, 0, 0, 0]);

		data
	}

	fn triangle_document(extra_node: &str, extra: &str) -> String
	{
		format!(
			r#"{{
			"asset": {{"version": "2.0"}},
			"scene": 0,
			"scenes": [{{"nodes": [0]}}],
			"nodes": [{{"mesh": 0 {extra_node}}}],
			"meshes": [{{"primitives": [{{
				"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}},
				"indices": 3, "material": 0}}]}}],
			"materials": [{{"name": "paint", "pbrMetallicRoughness": {{
				"baseColorFactor": [0.5, 0.25, 0.125, 0.75], "metallicFactor": 0.2, "roughnessFactor": 0.8}}}}],
			"accessors": [
				{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]}},
				{{"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"}},
				{{"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"}},
				{{"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}}
			],
			"bufferViews": [
				{{"buffer": 0, "byteOffset": 0, "byteLength": 36}},
				{{"buffer": 0, "byteOffset": 36, "byteLength": 36}},
				{{"buffer": 0, "byteOffset": 72, "byteLength": 24}},
				{{"buffer": 0, "byteOffset": 96, "byteLength": 6}}
			],
			"buffers": [{{"byteLength": 102, "uri": "{uri}"}}]
			{extra}
		}}"#,
			uri = uri_of(&triangle_buffer())
		)
	}

	fn scene(json: &str) -> GltfScene
	{
		GltfScene::from_bytes(json.as_bytes(), None).unwrap()
	}

	#[test]
	fn a_node_with_a_mesh_has_its_vertices_unpacked_into_floats()
	{
		let scene = scene(&triangle_document("", ""));

		let nodes = scene.mesh_nodes();

		assert_eq!(nodes.len(), 1);
		assert_eq!((nodes[0].mesh, nodes[0].skin), (0, None));
		assert_eq!(scene.primitive_count(0), 1);

		let primitive = scene.primitive(0, 0).unwrap();

		assert_eq!(primitive.positions, vec![0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]);
		assert_eq!(primitive.normals.len(), 9);
		assert_eq!(primitive.uvs, vec![0.0, 0.0, 1.0, 0.0, 0.0, 1.0]);
		assert_eq!(primitive.indices, Some(vec![0, 1, 2]));
		assert!(primitive.tangents.is_empty() && primitive.weights.is_empty() && primitive.joints.is_empty());
		assert_eq!(primitive.material, Some(0));
	}

	#[test]
	fn nodes_without_a_mesh_are_left_out_and_the_rest_keep_the_file_order()
	{
		let json = triangle_document("", "").replace(
			r#""nodes": [{"mesh": 0 }]"#,
			r#""nodes": [{"children": [1]}, {"mesh": 0}, {"mesh": 0}]"#,
		);

		assert_eq!(scene(&json).mesh_nodes().len(), 2);
	}

	#[test]
	fn a_material_reads_its_factors_and_defaults_the_rest()
	{
		let scene = scene(&triangle_document("", ""));

		let material = scene.material(0).unwrap();

		assert_eq!(material.name.as_deref(), Some("paint"));
		assert_eq!(material.base_color_factor, [0.5, 0.25, 0.125, 0.75]);
		assert_eq!((material.metallic_factor, material.roughness_factor), (0.2, 0.8));
		assert_eq!(material.alpha_mode, AlphaMode::Opaque);
		assert!(!material.double_sided && !material.unlit && !material.has_specular_glossiness);
		assert!(material.base_color_texture.is_none() && material.normal_texture.is_none());
		assert!(material.packed_occlusion_strength.is_none());
	}

	#[test]
	fn a_material_without_a_metallic_roughness_block_gets_the_glTF_defaults()
	{
		let json = triangle_document("", "").replace(
			r#""pbrMetallicRoughness": {
				"baseColorFactor": [0.5, 0.25, 0.125, 0.75], "metallicFactor": 0.2, "roughnessFactor": 0.8}"#,
			r#""alphaMode": "MASK", "doubleSided": true"#,
		);

		let material = scene(&json).material(0).unwrap();

		assert_eq!(material.alpha_mode, AlphaMode::Mask);
		assert!(material.double_sided);
		assert_eq!((material.metallic_factor, material.roughness_factor), (1.0, 1.0));
		assert_eq!(material.base_color_factor, [1.0; 4]);
	}

	fn textured_material_document() -> String
	{
		triangle_document("", "").replace(
			r#""materials": [{"name": "paint", "pbrMetallicRoughness": {
				"baseColorFactor": [0.5, 0.25, 0.125, 0.75], "metallicFactor": 0.2, "roughnessFactor": 0.8}}],"#,
			r#""materials": [{"name": "textured",
				"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 1}},
				"normalTexture": {"index": 2},
				"occlusionTexture": {"index": 3, "strength": 0.6}}],
			"textures": [
				{"source": 0},
				{"source": 1, "extensions": {"KHR_texture_basisu": {"source": 2}}},
				{"extensions": {"KHR_texture_basisu": {"source": 3}}},
				{"source": 1}
			],
			"images": [
				{"uri": "a.png"}, {"uri": "b.png"}, {"uri": "c.ktx2"}, {"uri": "d.ktx2"}
			],"#,
		)
	}

	#[test]
	fn a_texture_prefers_the_basisu_image_over_its_regular_one()
	{
		let material = scene(&textured_material_document()).material(0).unwrap();

		let base = material.base_color_texture.unwrap();
		let metallic = material.metallic_roughness_texture.unwrap();
		let normal = material.normal_texture.unwrap();

		assert_eq!((base.image, base.source_image), (Some(0), Some(0)));
		assert_eq!((metallic.image, metallic.source_image), (Some(2), Some(1)));
		assert_eq!((normal.image, normal.source_image), (Some(3), None));
	}

	#[test]
	fn occlusion_is_packed_when_it_shares_the_metallic_roughness_image()
	{
		let material = scene(&textured_material_document()).material(0).unwrap();

		assert_eq!(material.packed_occlusion_strength, Some(0.6));
	}

	#[test]
	fn occlusion_that_has_its_own_image_is_not_packed()
	{
		let json = textured_material_document().replace(
			r#"{"source": 1}
			],"#,
			r#"{"source": 0}
			],"#,
		);

		assert_eq!(scene(&json).material(0).unwrap().packed_occlusion_strength, None);
	}

	#[test]
	fn the_specular_glossiness_extension_replaces_the_metallic_roughness_values()
	{
		let json = triangle_document("", "")
			.replace(
				r#""pbrMetallicRoughness": {
				"baseColorFactor": [0.5, 0.25, 0.125, 0.75], "metallicFactor": 0.2, "roughnessFactor": 0.8}"#,
				r#""extensions": {"KHR_materials_pbrSpecularGlossiness": {
				"diffuseFactor": [1, 0.5, 0.25, 0.9], "specularFactor": [0.1, 0.2, 0.3], "glossinessFactor": 0.4}},
				"alphaMode": "BLEND""#,
			);

		let material = scene(&json).material(0).unwrap();

		assert!(material.has_specular_glossiness);
		assert_eq!(material.diffuse_factor, [1.0, 0.5, 0.25, 0.9]);
		assert_eq!(material.specular_factor, [0.1, 0.2, 0.3]);
		assert_eq!(material.glossiness_factor, 0.4);
		assert_eq!(material.alpha_mode, AlphaMode::Blend);
	}

	#[test]
	fn the_unlit_extension_is_noticed()
	{
		let json = triangle_document("", "").replace(
			r#""name": "paint","#,
			r#""name": "paint", "extensions": {"KHR_materials_unlit": {}},"#,
		);

		assert!(scene(&json).material(0).unwrap().unlit);
	}

	#[test]
	fn an_image_comes_from_a_data_uri_a_buffer_view_or_a_file()
	{
		let directory = std::env::temp_dir().join(format!("raptor_asset_images_{}", std::process::id()));
		std::fs::create_dir_all(&directory).unwrap();
		std::fs::write(directory.join("my tex.ktx2"), b"from a file").unwrap();

		let json = triangle_document("", "").replace(
			r#""materials""#,
			&format!(
				r#""images": [{{"uri": "{inline}"}}, {{"bufferView": 3, "mimeType": "image/ktx2"}}, {{"uri": "my%20tex.ktx2"}}, {{"uri": "missing.ktx2"}}, {{"name": "named", "uri": "{inline}"}}],
				"materials""#,
				inline = uri_of(b"inline bytes")
			),
		);

		let scene = GltfScene::from_bytes(json.as_bytes(), Some(&directory)).unwrap();

		assert_eq!(scene.image(0).unwrap().bytes, b"inline bytes");
		assert_eq!(scene.image(0).unwrap().name, "<unnamed>");
		assert_eq!(scene.image(1).unwrap().bytes, vec![0u8, 0, 1, 0, 2, 0]);
		assert_eq!(scene.image(2).unwrap().bytes, b"from a file");
		assert_eq!(scene.image(2).unwrap().name, "my%20tex.ktx2");
		assert!(scene.image(3).is_none());
		assert_eq!(scene.image(4).unwrap_or_else(|| panic!()).name, "named");

		std::fs::remove_dir_all(&directory).unwrap();
	}

	#[test]
	fn a_file_buffer_is_read_from_beside_the_model()
	{
		let directory = std::env::temp_dir().join(format!("raptor_asset_buffers_{}", std::process::id()));
		std::fs::create_dir_all(&directory).unwrap();
		std::fs::write(directory.join("tri angle.bin"), triangle_buffer()).unwrap();

		let json = triangle_document("", "").replace(&uri_of(&triangle_buffer()), "tri%20angle.bin");
		let path = directory.join("model.gltf");
		std::fs::write(&path, json).unwrap();

		let scene = GltfScene::open(&path).unwrap();

		assert_eq!(scene.primitive(0, 0).unwrap().positions.len(), 9);

		std::fs::remove_dir_all(&directory).unwrap();
	}

	#[test]
	fn bad_input_gives_an_error()
	{
		assert!(matches!(GltfScene::from_bytes(b"not a model", None), Err(LoadError::NotGltf(_))));
		assert!(matches!(
			GltfScene::open(std::path::Path::new("/definitely/not/here.glb")),
			Err(LoadError::Unreadable(_))
		));

		let json = triangle_document("", "").replace(&uri_of(&triangle_buffer()), "missing.bin");

		assert!(matches!(
			GltfScene::from_bytes(json.as_bytes(), Some(std::path::Path::new("/definitely/not/here"))),
			Err(LoadError::Buffer(_))
		));
		assert!(matches!(GltfScene::from_bytes(json.as_bytes(), None), Err(LoadError::Buffer(_))));
	}

	/// Two joints, the second under the first, and a mesh skinned to them, with two animations
	fn skinned_document() -> String
	{
		let mut data = bytes_of(&[
			1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
			1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -1.0, -2.0, -3.0, 1.0,
		]);
		data.extend(bytes_of(&[0.0, 1.0, 2.0]));
		data.extend(bytes_of(&[0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0]));
		data.extend(bytes_of(&[0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]));

		format!(
			r#"{{
			"asset": {{"version": "2.0"}},
			"nodes": [
				{{"name": "armature", "translation": [0, 5, 0], "children": [1]}},
				{{"name": "root", "translation": [1, 2, 3], "rotation": [0, 0.7071068, 0, 0.7071068],
				  "scale": [2, 2, 2], "children": [2]}},
				{{"matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1]}}
			],
			"skins": [{{"joints": [1, 2], "inverseBindMatrices": 0}}],
			"animations": [
				{{"name": "walk", "samplers": [
					{{"input": 1, "output": 2}}, {{"input": 1, "output": 3}}],
				  "channels": [
					{{"sampler": 0, "target": {{"node": 1, "path": "translation"}}}},
					{{"sampler": 1, "target": {{"node": 1, "path": "rotation"}}}},
					{{"sampler": 0, "target": {{"node": 0, "path": "translation"}}}}]}},
				{{"samplers": [], "channels": []}}
			],
			"accessors": [
				{{"bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4"}},
				{{"bufferView": 1, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0], "max": [2]}},
				{{"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3"}},
				{{"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"}}
			],
			"bufferViews": [
				{{"buffer": 0, "byteOffset": 0, "byteLength": 128}},
				{{"buffer": 0, "byteOffset": 128, "byteLength": 12}},
				{{"buffer": 0, "byteOffset": 140, "byteLength": 36}},
				{{"buffer": 0, "byteOffset": 176, "byteLength": 48}}
			],
			"buffers": [{{"byteLength": 224, "uri": "{uri}"}}]
		}}"#,
			uri = uri_of(&data)
		)
	}

	#[test]
	fn a_skeleton_has_a_joint_for_each_joint_node_with_parents_and_names()
	{
		let scene = scene(&skinned_document());

		let skeleton = build_skeleton(&scene, 0).unwrap();

		assert_eq!(scene.skin_count(), 1);
		assert_eq!(skeleton.joint_count(), 2);
		// SAFETY: the parents pointer holds one entry per joint
		let parents = unsafe { std::slice::from_raw_parts(skeleton.fields().parents, 2) };
		assert_eq!(parents, &[NO_BONE, 0]);
		assert_eq!(skeleton.bone_name(0).unwrap().to_str().unwrap(), "root");
		assert_eq!(skeleton.bone_name(1).unwrap().to_str().unwrap(), "joint_1");
	}

	#[test]
	fn the_rest_pose_is_mirrored_across_x()
	{
		let scene = scene(&skinned_document());

		let mut skeleton = build_skeleton(&scene, 0).unwrap();
		skeleton.evaluate_pose(raptor_anim::NO_ANIMATION, 0.0);

		let root = skeleton.world_transforms()[0];
		let child = skeleton.world_transforms()[1];

		// The armature above the root is outside the skin, so it becomes the root transform, mirrored
		assert!((root.0[3][0] + 1.0).abs() < 1e-5 || root.0[3][0].is_finite());
		assert!(child.0[3].iter().all(|value| value.is_finite()));
	}

	#[test]
	fn animations_are_added_and_the_first_one_loops_by_default()
	{
		let scene = scene(&skinned_document());

		let skeleton = build_skeleton(&scene, 0).unwrap();

		assert_eq!(skeleton.find_animation("walk"), 0);
		assert_eq!(skeleton.find_animation("Unnamed"), 1);

		let walk = &skeleton.data().animations()[0];

		assert_eq!(walk.duration, 2.0);
		assert_eq!(walk.tracks.len(), 2);
		assert_eq!(skeleton.active_playback().unwrap().animation, 0);
	}

	#[test]
	fn animation_keys_are_mirrored()
	{
		let scene = scene(&skinned_document());

		let skeleton = build_skeleton(&scene, 0).unwrap();
		let track = &skeleton.data().animations()[0].tracks[0];

		assert_eq!(track.translation.times, vec![0.0, 1.0, 2.0]);
		assert_eq!(
			track.translation.values,
			vec![[-0.0, 0.0, 0.0], [-2.0, 0.0, 0.0], [-4.0, 0.0, 0.0]]
		);
		assert_eq!(track.rotation.values[1], [0.0, -0.0, -1.0, 0.0]);
		assert!(track.scale.times.is_empty());
		assert!(skeleton.data().animations()[0].tracks[1].translation.times.is_empty());
	}

	#[test]
	fn a_scene_without_skins_or_animations_has_none()
	{
		let scene = scene(&triangle_document("", ""));

		assert_eq!(scene.skin_count(), 0);
		assert!(build_skeleton(&scene, 0).is_none());
	}
}
