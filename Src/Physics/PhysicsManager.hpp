#pragma once

#include "Body.hpp"

#include <Core/FreeArray.hpp>
#include <mutex>

namespace fx {

namespace physics {
class JoltPhysicsBackend;
}

class PhysicsManager
{
public:
	PhysicsManager() = default;

	void Create();

	physics::Body* NewBody(const String& name);
	physics::Body* GetBody(physics::BodyID id);
	void DestroyBody(physics::BodyID& id);

	physics::Body* FindBody(const Hash32 name_hash);

	SizedArray<physics::Body*> CollectBodies();

	~PhysicsManager();

public:
	physics::JoltPhysicsBackend* pBackend = nullptr;

	std::atomic_uint32_t UpdateState = 0;

private:
	FreeArray<physics::Body> mBodies;

	std::mutex mInUse;
};


} // namespace fx
