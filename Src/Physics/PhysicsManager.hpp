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

	~PhysicsManager();

public:
	physics::JoltPhysicsBackend* pBackend = nullptr;

private:
	FreeArray<physics::Body> mBodies;

	std::mutex mInUse;
};


} // namespace fx
