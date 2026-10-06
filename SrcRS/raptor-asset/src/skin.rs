use gltf::animation::util::ReadOutputs;
use raptor_anim::math::multiply;
use raptor_anim::{Animation, BoneTrack, Mat4, NO_BONE, RestPose, Skeleton, SkeletonData, Track};

use crate::gltf_scene::GltfScene;

const SCALE_EPSILON: f32 = 1e-8;

/// The mirror across X that takes glTF's coordinates to the engine's. Everything read out of a skin is
/// conjugated by it, to stay in step with the vertex buffers.
fn reflection() -> Mat4 {
	let mut matrix = Mat4::IDENTITY;
	matrix.0[0][0] = -1.0;
	matrix
}

fn lengthwise(v: [f32; 3]) -> f32 {
	// Added pairwise, the way the engine's vector length is
	((v[0] * v[0] + v[1] * v[1]) + (v[2] * v[2] + 0.0)).sqrt()
}

/// The local matrix of a node as glTF's own transform code builds it, with the products and sums fused
/// the way the compiler fuses them there.
fn local_matrix(node: &gltf::Node) -> [f32; 16] {
	match node.transform() {
		gltf::scene::Transform::Matrix { matrix } => {
			let mut flat = [0.0; 16];

			for (index, value) in matrix.iter().flatten().enumerate() {
				flat[index] = *value;
			}

			flat
		}
		gltf::scene::Transform::Decomposed {
			translation,
			rotation,
			scale,
		} => {
			let [tx, ty, tz] = translation;
			let [qx, qy, qz, qw] = rotation;
			let [sx, sy, sz] = scale;

			// `1 - 2*a*a - 2*b*b`
			let diagonal = |a: f32, b: f32| (-(2.0 * b)).mul_add(b, (-(2.0 * a)).mul_add(a, 1.0));
			// `2*a*b + 2*c*d`
			let sum = |a: f32, b: f32, c: f32, d: f32| (2.0 * a).mul_add(b, (2.0 * c) * d);
			// `2*a*b - 2*c*d`
			let difference =
				|a: f32, b: f32, c: f32, d: f32| (2.0 * a).mul_add(b, -((2.0 * c) * d));

			[
				diagonal(qy, qz) * sx,
				sum(qx, qy, qz, qw) * sx,
				difference(qx, qz, qy, qw) * sx,
				0.0,
				difference(qx, qy, qz, qw) * sy,
				diagonal(qx, qz) * sy,
				sum(qy, qz, qx, qw) * sy,
				0.0,
				sum(qx, qz, qy, qw) * sz,
				difference(qy, qz, qx, qw) * sz,
				diagonal(qx, qy) * sz,
				0.0,
				tx,
				ty,
				tz,
				1.0,
			]
		}
	}
}

/// The world matrix of a node, built by walking up through its parents
fn world_matrix(node: &gltf::Node, parent_of: &[Option<usize>], nodes: &[gltf::Node]) -> [f32; 16] {
	let mut lm = local_matrix(node);
	let mut parent = parent_of[node.index()];

	while let Some(index) = parent {
		let pm = local_matrix(&nodes[index]);

		for row in 0..4 {
			let (l0, l1, l2) = (lm[row * 4], lm[row * 4 + 1], lm[row * 4 + 2]);

			let r0 = l2.mul_add(pm[8], l0.mul_add(pm[0], l1 * pm[4]));
			let r1 = l2.mul_add(pm[9], l0.mul_add(pm[1], l1 * pm[5]));
			let r2 = l2.mul_add(pm[10], l0.mul_add(pm[2], l1 * pm[6]));

			lm[row * 4] = r0;
			lm[row * 4 + 1] = r1;
			lm[row * 4 + 2] = r2;
		}

		lm[12] += pm[12];
		lm[13] += pm[13];
		lm[14] += pm[14];

		parent = parent_of[index];
	}

	lm
}

fn mat4_from_flat(flat: &[f32; 16]) -> Mat4 {
	let mut matrix = Mat4::IDENTITY;

	for (row, chunk) in matrix.0.iter_mut().zip(flat.as_chunks::<4>().0) {
		*row = *chunk;
	}

	matrix
}

/// The local transform a joint sits at when an animation does not drive it, mirrored across X like
/// the animation channels are (X negated on translations, Y and Z on rotations).
fn rest_pose(node: &gltf::Node) -> RestPose {
	let mut rest = RestPose::default();

	match node.transform() {
		gltf::scene::Transform::Matrix { matrix } => {
			let m: Vec<f32> = matrix.iter().flatten().copied().collect();

			let mut axis_x = [m[0], m[1], m[2]];
			let mut axis_y = [m[4], m[5], m[6]];
			let mut axis_z = [m[8], m[9], m[10]];

			let scale_x = lengthwise(axis_x);
			let scale_y = lengthwise(axis_y);
			let scale_z = lengthwise(axis_z);

			rest.scale = [scale_x, scale_y, scale_z, 0.0];
			rest.translation = [m[12], m[13], m[14], 0.0];

			if scale_x > SCALE_EPSILON && scale_y > SCALE_EPSILON && scale_z > SCALE_EPSILON {
				for value in &mut axis_x {
					*value /= scale_x;
				}

				for value in &mut axis_y {
					*value /= scale_y;
				}

				for value in &mut axis_z {
					*value /= scale_z;
				}

				let trace = (axis_x[0] + axis_y[1]) + axis_z[2];

				rest.rotation = if trace > 0.0 {
					let w = (trace + 1.0).sqrt() * 0.5;
					let inv = 0.25 / w;

					[
						(axis_y[2] - axis_z[1]) * inv,
						(axis_z[0] - axis_x[2]) * inv,
						(axis_x[1] - axis_y[0]) * inv,
						w,
					]
				} else if axis_x[0] > axis_y[1] && axis_x[0] > axis_z[2] {
					let x = (1.0 + axis_x[0] - axis_y[1] - axis_z[2]).sqrt() * 0.5;
					let inv = 0.25 / x;

					[
						x,
						(axis_x[1] + axis_y[0]) * inv,
						(axis_z[0] + axis_x[2]) * inv,
						(axis_y[2] - axis_z[1]) * inv,
					]
				} else if axis_y[1] > axis_z[2] {
					let y = (1.0 + axis_y[1] - axis_x[0] - axis_z[2]).sqrt() * 0.5;
					let inv = 0.25 / y;

					[
						(axis_x[1] + axis_y[0]) * inv,
						y,
						(axis_y[2] + axis_z[1]) * inv,
						(axis_z[0] - axis_x[2]) * inv,
					]
				} else {
					let z = (1.0 + axis_z[2] - axis_x[0] - axis_y[1]).sqrt() * 0.5;
					let inv = 0.25 / z;

					[
						(axis_z[0] + axis_x[2]) * inv,
						(axis_y[2] + axis_z[1]) * inv,
						z,
						(axis_x[1] - axis_y[0]) * inv,
					]
				};
			}
		}
		gltf::scene::Transform::Decomposed {
			translation,
			rotation,
			scale,
		} => {
			rest.translation = [translation[0], translation[1], translation[2], 0.0];
			rest.rotation = rotation;
			rest.scale = [scale[0], scale[1], scale[2], 0.0];
		}
	}

	rest.translation[0] = -rest.translation[0];
	rest.rotation = [
		rest.rotation[0],
		-rest.rotation[1],
		-rest.rotation[2],
		rest.rotation[3],
	];

	rest
}

fn build_animation(
	scene: &GltfScene,
	animation: &gltf::Animation,
	joint_nodes: &[usize],
) -> Animation {
	let buffers = scene.buffers();

	let mut out = Animation {
		name: animation.name().unwrap_or("Unnamed").to_owned(),
		duration: 0.0,
		tracks: vec![BoneTrack::default(); joint_nodes.len()],
	};

	for channel in animation.channels() {
		let Some(joint) = joint_nodes
			.iter()
			.position(|node| *node == channel.target().node().index())
		else {
			continue;
		};

		let reader = channel.reader(|buffer| buffers.get(buffer.index()).map(Vec::as_slice));

		let Some(times) = reader.read_inputs().map(Iterator::collect::<Vec<f32>>) else {
			continue;
		};

		if let Some(last) = times.last() {
			out.duration = out.duration.max(*last);
		}

		let Some(outputs) = reader.read_outputs() else {
			continue;
		};

		let stride =
			if channel.sampler().interpolation() == gltf::animation::Interpolation::CubicSpline {
				3
			} else {
				1
			};

		let pick = |index: usize| index * stride + usize::from(stride == 3);
		let track = &mut out.tracks[joint];

		match outputs {
			ReadOutputs::Translations(values) => {
				let values: Vec<[f32; 3]> = values.collect();

				track.translation = Track {
					values: (0..times.len())
						.filter_map(|key| values.get(pick(key)))
						.map(|value| [-value[0], value[1], value[2]])
						.collect(),
					times,
				};
			}
			ReadOutputs::Rotations(values) => {
				let values: Vec<[f32; 4]> = values.into_f32().collect();

				track.rotation = Track {
					values: (0..times.len())
						.filter_map(|key| values.get(pick(key)))
						.map(|value| [value[0], -value[1], -value[2], value[3]])
						.collect(),
					times,
				};
			}
			ReadOutputs::Scales(values) => {
				let values: Vec<[f32; 3]> = values.collect();

				track.scale = Track {
					values: (0..times.len())
						.filter_map(|key| values.get(pick(key)))
						.copied()
						.collect(),
					times,
				};
			}
			ReadOutputs::MorphTargetWeights(_) => {}
		}
	}

	out
}

/// Makes the skeleton of a skin, with every animation in the file, and the first of them set to loop
/// when nothing else is playing.
pub fn build_skeleton(scene: &GltfScene, skin_index: usize) -> Option<Skeleton> {
	let document = scene.document();
	let skin = document.skins().nth(skin_index)?;

	let nodes: Vec<gltf::Node> = document.nodes().collect();

	let mut parent_of = vec![None; nodes.len()];

	for node in &nodes {
		for child in node.children() {
			parent_of[child.index()] = Some(node.index());
		}
	}

	let joint_nodes: Vec<usize> = skin.joints().map(|joint| joint.index()).collect();
	let joint_count = joint_nodes.len();

	let reflection = reflection();

	let reader = skin.reader(|buffer| scene.buffers().get(buffer.index()).map(Vec::as_slice));

	let inverse_bind = reader.read_inverse_bind_matrices().and_then(|matrices| {
		let matrices: Vec<Mat4> = matrices
			.map(|matrix| {
				let flat: Vec<f32> = matrix.iter().flatten().copied().collect();
				let flat: [f32; 16] = flat.try_into().expect("a matrix has sixteen values");

				multiply(&multiply(&reflection, &mat4_from_flat(&flat)), &reflection)
			})
			.collect();

		(matrices.len() == joint_count).then_some(matrices)
	});

	let mut parents = Vec::with_capacity(joint_count);
	let mut names = Vec::with_capacity(joint_count);
	let mut rest_poses = Vec::with_capacity(joint_count);
	let mut root_transforms = Vec::with_capacity(joint_count);

	for (index, joint_node) in joint_nodes.iter().enumerate() {
		let node = &nodes[*joint_node];

		let parent_joint = parent_of[*joint_node]
			.and_then(|parent| joint_nodes.iter().position(|joint| *joint == parent));

		names.push(
			node.name()
				.map_or_else(|| format!("joint_{index}"), str::to_owned),
		);
		parents.push(parent_joint.map_or(NO_BONE, |parent| parent as u32));
		rest_poses.push(rest_pose(node));

		root_transforms.push(match (parent_joint, parent_of[*joint_node]) {
			(None, Some(parent)) => {
				let node_world = mat4_from_flat(&world_matrix(&nodes[parent], &parent_of, &nodes));

				multiply(&multiply(&reflection, &node_world), &reflection)
			}
			_ => Mat4::IDENTITY,
		});
	}

	let data = SkeletonData::new(
		joint_count as u32,
		inverse_bind,
		parents,
		Some(rest_poses),
		Some(root_transforms),
		names,
	);

	let mut skeleton = Skeleton::new(data);

	let mut first = None;

	for animation in document.animations() {
		let built = build_animation(scene, &animation, &joint_nodes);

		if let Ok(id) = skeleton.add_animation(built) {
			first.get_or_insert(id);
		}
	}

	if let Some(first) = first {
		skeleton.set_rest_animation(first, 1.0);
	}

	Some(skeleton)
}
