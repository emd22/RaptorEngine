//! First-person view motion: the view model's kick spring, the camera's recoil and the view model's
//! sway behind the camera.

pub mod locomotion;
pub mod player;
pub mod recoil;
pub mod sway;
pub mod view_kick;

pub use locomotion::Locomotion;
pub use player::PlayerState;
pub use recoil::Recoil;
pub use sway::ViewSway;
pub use view_kick::ViewKick;
