pub type Vec3 = [f32; 3];

pub fn add(a: Vec3, b: Vec3) -> Vec3 {
	[a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

pub fn sub(a: Vec3, b: Vec3) -> Vec3 {
	[a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

pub fn cross(a: Vec3, b: Vec3) -> Vec3 {
	[
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0],
	]
}

pub fn length(v: Vec3) -> f32 {
	let squares = [v[0] * v[0], v[1] * v[1], v[2] * v[2]];
	((squares[0] + squares[1]) + (squares[2] + 0.0)).sqrt()
}

pub fn normalized(v: Vec3) -> Vec3 {
	let len = length(v);

	if len > 0.0 && len.is_finite() {
		[v[0] / len, v[1] / len, v[2] / len]
	} else {
		[0.0; 3]
	}
}

pub fn surface_normal(a: Vec3, b: Vec3, c: Vec3) -> Vec3 {
	normalized(cross(sub(c, b), sub(b, a)))
}
