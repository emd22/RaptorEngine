#include "PhysicsManager.hpp"

#include "JoltPhysicsBackend.hpp"

#include <Core/Types.hpp>

namespace fx {

static constexpr uint32 scMaxActivePhysicsBodies = 256;

void PhysicsManager::Create()
{
	std::lock_guard<std::mutex> guard(mInUse);
	mBodies.Init(scMaxActivePhysicsBodies);

	pBackend = new physics::JoltPhysicsBackend;
	pBackend->Create();
}

physics::Body* PhysicsManager::NewBody(const String& name)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	physics::Body* body = mBodies.NewItem(&index);
	body->ID = physics::BodyID(index);
	body->SetName(name);

	UpdateState.fetch_add(1U);

	return body;
}

physics::Body* PhysicsManager::GetBody(physics::BodyID id)
{
	if (id.IsInvalid()) {
		return nullptr;
	}

	return mBodies.GetItem(id.GetID());
}


void PhysicsManager::DestroyBody(physics::BodyID& id)
{
	if (id.IsInvalid()) {
		return;
	}

	std::lock_guard<std::mutex> guard(mInUse);

	// If the ID is passed in directly from a body (as it probably will be in some places), then we want to make sure
	// we dont invalidate our ID before freeing it.
	physics::BodyID id_copy = id;

	// Delete the body id at the definition
	physics::Body* body = mBodies.GetItem(id.GetID());
	body->DestroyPhysicsBody();
	body->ID.Invalidate();

	// Delete the physics body
	mBodies.FreeItem(id_copy.GetID());

	// Invalidate the passed ID
	id.Invalidate();

	UpdateState.fetch_add(1U);
}


physics::Body* PhysicsManager::FindBody(const Hash32 name_hash)
{
	std::lock_guard<std::mutex> guard(mInUse);

	for (uint32 i = 0; i < mBodies.Capacity; i++) {
		if (mBodies.SlotsInUse.Get(i) == false) {
			continue;
		}

		physics::Body* body = mBodies.GetItem(i);

		if (body->GetName().GetHash() == name_hash) {
			return body;
		}
	}

	return nullptr;
}

SizedArray<physics::Body*> PhysicsManager::CollectBodies()
{
	std::lock_guard<std::mutex> guard(mInUse);

	SizedArray<physics::Body*> object_list(mBodies.Size + 1);

	for (uint32 i = 0; i < mBodies.Capacity; i++) {
		if (!mBodies.SlotsInUse.Get(i)) {
			continue;
		}

		object_list.Insert(mBodies.GetItem(i));
	}

	return object_list;
}

PhysicsManager::~PhysicsManager()
{
	if (pBackend != nullptr) {
		delete pBackend;
		pBackend = nullptr;
	}
}


} // namespace fx
