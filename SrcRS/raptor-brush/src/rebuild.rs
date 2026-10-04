use std::collections::{HashMap, HashSet};

use raptor_math::{Vec3d, Vec3f, to_f32, to_f64};

use crate::{Brush, Face, Plane};

const ON_PLANE_EPSILON: f64 = 1e-5;
const DISTANCE_TO_MERGE: f64 = 1e-6;
const EXTENT_TOO_BIG: f64 = 1e5;
const MIN_VOLUME: f64 = 1e-6;
const MIN_NORMAL_LENGTH: f64 = 1e-8;
const SAME_NORMAL_EPSILON: f64 = 1e-9;
const BOUNDING_PLANE: i32 = -1;

struct DPlane
{
	normal: Vec3d,
	distance: f64,
	source_index: usize,
}

impl DPlane
{
	fn signed_distance(&self, point: Vec3d) -> f64
	{
		self.normal.dot(&point) - self.distance
	}
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Side
{
	Inside,
	On,
	Outside,
}

#[derive(PartialEq, Eq)]
enum ClipResult
{
	Unchanged,
	Clipped,
	Empty,
}

struct Loop
{
	plane_index: i32,
	corners: Vec<u32>,
}

struct Polyhedron
{
	corners: Vec<Vec3d>,
	faces: Vec<Loop>,
}

fn edge_key(from: u32, to: u32) -> u64
{
	(u64::from(from) << 32) | u64::from(to)
}

fn nearly_equal(a: Vec3d, b: Vec3d, epsilon: f64) -> bool
{
	let delta = (a - b).abs();

	delta.x <= epsilon && delta.y <= epsilon && delta.z <= epsilon
}

impl Polyhedron
{
	fn make_box(half_extent: f64) -> Polyhedron
	{
		let signed = |set: bool| if set { half_extent } else { -half_extent };

		let corners: Vec<Vec3d> = (0..8u32)
			.map(|c| Vec3d::new(signed(c & 1 != 0), signed(c & 2 != 0), signed(c & 4 != 0)))
			.collect();

		const FACES: [[u32; 4]; 6] = [
			[1, 3, 7, 5],
			[0, 4, 6, 2],
			[2, 6, 7, 3],
			[0, 1, 5, 4],
			[4, 5, 7, 6],
			[0, 2, 3, 1],
		];

		let faces = FACES
			.iter()
			.map(|face| {
				let mut loop_corners = face.to_vec();

				let a = corners[loop_corners[0] as usize];
				let normal = (corners[loop_corners[1] as usize] - a)
					.cross(&(corners[loop_corners[2] as usize] - a));

				if normal.dot(&a) > 0.0 {
					loop_corners.reverse();
				}

				Loop {
					plane_index: BOUNDING_PLANE,
					corners: loop_corners,
				}
			})
			.collect();

		Polyhedron { corners, faces }
	}

	fn clip(&mut self, plane: &DPlane, plane_index: i32) -> ClipResult
	{
		let corner_count = self.corners.len();

		let mut in_use = vec![false; corner_count];
		for face in &self.faces {
			for &corner in &face.corners {
				in_use[corner as usize] = true;
			}
		}

		let mut sides = Vec::with_capacity(corner_count);
		let mut distances = Vec::with_capacity(corner_count);
		let mut has_inside = false;
		let mut has_outside = false;

		for (&corner, &used) in self.corners.iter().zip(&in_use) {
			let distance = plane.signed_distance(corner);
			distances.push(distance);

			if distance > ON_PLANE_EPSILON {
				sides.push(Side::Outside);
				has_outside |= used;
			} else if distance < -ON_PLANE_EPSILON {
				sides.push(Side::Inside);
				has_inside |= used;
			} else {
				sides.push(Side::On);
			}
		}

		if !has_outside {
			return ClipResult::Unchanged;
		}

		if !has_inside {
			return ClipResult::Empty;
		}

		let is_on_plane =
			|corner: u32| corner as usize >= corner_count || sides[corner as usize] == Side::On;

		let mut split_corners: HashMap<u64, u32> = HashMap::new();
		let mut clipped_faces: Vec<Loop> = Vec::new();

		for face in &self.faces {
			let mut clipped = Loop {
				plane_index: face.plane_index,
				corners: Vec::new(),
			};
			let count = face.corners.len();

			for i in 0..count {
				let current = face.corners[i];
				let next = face.corners[(i + 1) % count];

				let current_side = sides[current as usize];
				let next_side = sides[next as usize];

				if current_side != Side::Outside {
					clipped.corners.push(current);
				}

				if (current_side == Side::Inside && next_side == Side::Outside)
					|| (current_side == Side::Outside && next_side == Side::Inside)
				{
					let key = edge_key(current.min(next), current.max(next));

					let split = *split_corners.entry(key).or_insert_with(|| {
						let (a, b) = (current as usize, next as usize);
						let t = distances[a] / (distances[a] - distances[b]);

						let origin = self.corners[a];
						let point = origin + (self.corners[b] - origin) * t;

						self.corners.push(point);
						(self.corners.len() - 1) as u32
					});

					clipped.corners.push(split);
				}
			}

			let is_flat = clipped.corners.iter().all(|&c| is_on_plane(c));

			if clipped.corners.len() >= 3 && !is_flat {
				clipped_faces.push(clipped);
			}
		}

		let mut edges: HashSet<u64> = HashSet::new();
		for face in &clipped_faces {
			for i in 0..face.corners.len() {
				edges.insert(edge_key(
					face.corners[i],
					face.corners[(i + 1) % face.corners.len()],
				));
			}
		}

		let mut cap_next: HashMap<u32, u32> = HashMap::new();
		for face in &clipped_faces {
			for i in 0..face.corners.len() {
				let a = face.corners[i];
				let b = face.corners[(i + 1) % face.corners.len()];

				if is_on_plane(a) && is_on_plane(b) && !edges.contains(&edge_key(b, a)) {
					cap_next.insert(b, a);
				}
			}
		}

		if cap_next.len() < 3 {
			return ClipResult::Empty;
		}

		let Some(&start) = cap_next.keys().min() else {
			return ClipResult::Empty;
		};

		let mut cap = Loop {
			plane_index,
			corners: Vec::new(),
		};
		let mut corner = start;

		loop {
			cap.corners.push(corner);

			match cap_next.get(&corner) {
				Some(&next) if cap.corners.len() <= cap_next.len() => corner = next,
				_ => return ClipResult::Empty,
			}

			if corner == start {
				break;
			}
		}

		if cap.corners.len() != cap_next.len() {
			return ClipResult::Empty;
		}

		clipped_faces.push(cap);
		self.faces = clipped_faces;

		ClipResult::Clipped
	}
}

impl Brush
{
	pub fn rebuild(&mut self) -> bool
	{
		self.faces.clear();
		self.vertices.clear();
		self.bounds_min = Vec3f::splat(0.0);
		self.bounds_max = Vec3f::splat(0.0);

		let mut planes: Vec<DPlane> = Vec::with_capacity(self.planes.len());

		for (plane_index, plane) in self.planes.iter().enumerate() {
			let normal = to_f64(plane.normal);
			let length = normal.length();

			if length.is_nan() || length <= MIN_NORMAL_LENGTH || !plane.distance.is_finite() {
				continue;
			}

			let normalized = DPlane {
				normal: normal * (1.0 / length),
				distance: f64::from(plane.distance) / length,
				source_index: plane_index,
			};

			let is_duplicate = planes.iter().any(|existing| {
				existing.normal.dot(&normalized.normal) > 1.0 - SAME_NORMAL_EPSILON
					&& (existing.distance - normalized.distance).abs() <= ON_PLANE_EPSILON
			});

			if !is_duplicate {
				planes.push(normalized);
			}
		}

		let mut polyhedron = Polyhedron::make_box(EXTENT_TOO_BIG);

		for (i, plane) in planes.iter().enumerate() {
			if polyhedron.clip(plane, i as i32) == ClipResult::Empty {
				return false;
			}
		}

		let corner_count = polyhedron.corners.len();

		let mut merged: Vec<u32> = (0..corner_count as u32).collect();
		for i in 0..corner_count {
			for j in 0..i {
				if merged[j] as usize == j
					&& nearly_equal(
						polyhedron.corners[i],
						polyhedron.corners[j],
						DISTANCE_TO_MERGE,
					) {
					merged[i] = j as u32;
					break;
				}
			}
		}

		let mut kept_planes: Vec<Plane> = Vec::new();
		let mut faces: Vec<Face> = Vec::new();
		let mut vertex_remap = vec![-1i32; corner_count];
		let mut used_vertices: Vec<Vec3d> = Vec::new();
		let mut volume = 0.0f64;

		for face_loop in &polyhedron.faces {
			if face_loop.plane_index == BOUNDING_PLANE {
				return false;
			}

			let mut polygon: Vec<u32> = Vec::with_capacity(face_loop.corners.len());
			for &corner in &face_loop.corners {
				let index = merged[corner as usize];
				if polygon.last() != Some(&index) {
					polygon.push(index);
				}
			}

			while polygon.len() > 1 && polygon[0] == polygon[polygon.len() - 1] {
				polygon.pop();
			}

			if polygon.len() < 3 {
				continue;
			}

			let origin = polyhedron.corners[polygon[0] as usize];
			let mut area_vector = Vec3d::splat(0.0);

			for v in 1..polygon.len() - 1 {
				area_vector = area_vector
					+ (polyhedron.corners[polygon[v + 1] as usize] - origin)
						.cross(&(polyhedron.corners[polygon[v] as usize] - origin));
			}

			volume += area_vector.dot(&origin) / 6.0;

			let mut face = Face {
				plane_index: kept_planes.len() as u32,
				vertices: Vec::with_capacity(polygon.len()),
			};

			for &corner in &polygon {
				let point = polyhedron.corners[corner as usize];
				face.vertices.push(to_f32(point));

				if vertex_remap[corner as usize] < 0 {
					vertex_remap[corner as usize] = used_vertices.len() as i32;
					used_vertices.push(point);
				}
			}

			let plane = &planes[face_loop.plane_index as usize];
			kept_planes.push(Plane {
				normal: to_f32(plane.normal),
				distance: plane.distance as f32,
				texture: self.planes[plane.source_index].texture,
			});
			faces.push(face);
		}

		if faces.len() < 4 || volume < MIN_VOLUME {
			return false;
		}

		self.planes = kept_planes;
		self.faces = faces;
		self.vertices = used_vertices.iter().map(|&v| to_f32(v)).collect();

		self.bounds_min = self.vertices[0];
		self.bounds_max = self.vertices[0];

		for vertex in &self.vertices {
			self.bounds_min = self.bounds_min.min(vertex);
			self.bounds_max = self.bounds_max.max(vertex);
		}

		true
	}
}
