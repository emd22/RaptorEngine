#include "Player.hpp"

#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
#include <Core/Random.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>

namespace fx {

using namespace renderer;

static_assert(sizeof(Vec2f) == 4 * sizeof(float32));
static_assert(sizeof(Vec3f) == 4 * sizeof(float32));
static_assert(offsetof(RxPlayerState, speed_multiplier) == 64);
static_assert(offsetof(RxPlayerState, sprinting) == 72);

Player::Player() {}

void Player::Create()
{
	pCamera = MakeRef<PerspectiveCamera>();

	pCamera->SetAspectRatio(gGraphics->Swapchain.GetAspectRatio());
	pCamera->SetFov(scWalkingFov);

	Physics.Create();

	// Since the physics position is the center of the capsule, we will use standing height / 2.
	mpState->camera_offset[1] = physics::PhysicsPlayer::scStandingHeight;

	// Load the view model

	AssetTicket view_model = gAssetManager->LoadObject("view_model",
													   "RaptorData/Data/Demo/Models/viewmodel/viewmodel_baked.glb");

	// Registered before World::Attach() so these callbacks run first: the view model has to be set up before the world
	// adds it to the grid, or a probe bake running on the main thread in between would capture it as level geometry.
	view_model.OnLoaded(
		[&](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);
			if (object == nullptr) {
				return;
			}

			object->SetShadowCaster(false);
			object->SetCullable(false);
			object->SetObjectLayer(eObjectLayer::PlayerLayer);
			object->SetProbeVisible(false);

			mpViewModel = object;

			// The skeleton lives on whichever mesh is skinned, which is one of the attached objects if the model has
			// more than one mesh.
			mpViewModelSkeleton = object->pSkeleton;

			for (ObjectID child_id : object->AttachedNodes) {
				if (mpViewModelSkeleton) {
					break;
				}

				Object* child = gObjectManager->GetObject(child_id);
				if (child != nullptr) {
					mpViewModelSkeleton = child->pSkeleton;
				}
			}

			if (mpViewModelSkeleton) {
				mpViewModelSkeleton->SetRestAnimation(mpViewModelSkeleton->FindAnimation(mIdleAnim));
			}

			// Start in line with the camera so the view model doesn't swing in when it first appears
			rx_player_start_sway(mpState, pCamera->GetCore());
		});

	gWorld->Attach(view_model);

	Weapons.Create(this);
}

void Player::MoveBy(const Vec3f& by) { rx_player_move_by(mpState, &by.mData[0]); }

void Player::RotateHead(const Vec2f& xy) { rx_player_rotate_head(mpState, pCamera->GetCore(), xy.X, xy.Y); }

void Player::Jump() { rx_player_jump(mpState, Physics.IsGrounded()); }

void Player::SetFlyMode(bool value)
{
	rx_player_set_fly_mode(mpState, value);
	Physics.SetGravityDisabled(value);
}

void Player::Move(float64 delta_time, const Vec3f& offset)
{
	Vec3f force;
	rx_player_movement_force(mpState, delta_time, &offset.mData[0], &force.mData[0]);

	Physics.ApplyMovement(force);
}

void Player::UpdateViewModel(double delta_time)
{
	if (mpViewModel == nullptr) {
		return;
	}

	const Vec3f velocity = Physics.GetLinearVelocity();

	float32 position[3];
	float32 rotation[4];

	rx_player_view_model_pose(mpState, pCamera->GetCore(), &velocity.mData[0], static_cast<float32>(delta_time),
							  position, rotation);

	mpViewModel->SetPosition(Vec3f(position[0], position[1], position[2]));
	mpViewModel->SetRotation(Quat(rotation[0], rotation[1], rotation[2], rotation[3]));
}

void Player::DoFireAnimation(float32 kick_degrees, float32 kickback)
{
	const float32 random_yaw = RandomSignedUnit();
	const float32 random_roll = RandomSignedUnit();

	rx_view_kick_fire(&mpState->kick, kick_degrees, kickback, random_yaw, random_roll);

	if (!mpViewModelSkeleton) {
		return;
	}

	const AnimationId fire = mpViewModelSkeleton->FindAnimation(mFireAnim);

	if (fire == NoAnimation) {
		return;
	}

	// Firing again mid-shot restarts the shot rather than stacking another one on top of it
	if (mpViewModelSkeleton->GetActiveAnimation() == fire) {
		mpViewModelSkeleton->PopAnimation();
	}

	mpViewModelSkeleton->PushAnimation(fire);
}

void Player::DoReloadAnimation()
{
	if (!mpViewModelSkeleton) {
		return;
	}

	const AnimationId reload = mpViewModelSkeleton->FindAnimation(mReloadAnim);

	if (reload == NoAnimation || mpViewModelSkeleton->GetActiveAnimation() == reload) {
		return;
	}

	mpViewModelSkeleton->PushAnimation(reload);
}

void Player::SetViewModelAnimations(const char* idle, const char* fire, const char* reload)
{
	mIdleAnim = idle;
	mFireAnim = fire;
	mReloadAnim = reload;

	if (!mpViewModelSkeleton) {
		return;
	}

	const AnimationId idle_anim = mpViewModelSkeleton->FindAnimation(mIdleAnim);

	if (idle_anim != NoAnimation) {
		mpViewModelSkeleton->SetRestAnimation(idle_anim);
	}
}

void Player::AddRecoil(float32 pitch, float32 yaw) { rx_recoil_add(&mpState->recoil, pitch, yaw); }

void Player::Update(float64 delta_time)
{
	Physics.Update(delta_time);
	SyncPhysicsToPlayer();

	rx_player_update(mpState, pCamera->GetCore(), delta_time, Physics.IsGrounded(),
					 gCVars->Get("b_headbob_enabled", false));

	Weapons.Update(static_cast<float32>(delta_time));

	UpdateViewModel(delta_time);
}

} // namespace fx
