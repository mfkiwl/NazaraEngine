// This file was automatically generated on 26 May 2014 at 01:05:31

#include <NDK/LuaBinding.hpp>

namespace Ndk
{
	LuaBinding::LuaBinding() :
	// Core
	clockClass("Clock"),
	directoryClass("Directory"),
	fileClass("File"),
	streamClass("Stream"),

	// Math
	eulerAnglesClass("EulerAngles"),
	quaternionClass("Quaternion"),
	vector2dClass("Vector2"),
	vector3dClass("Vector3"),

	// Network
	abstractSocketClass("AbstractSocket"),
	ipAddressClass("IpAddress"),

	// Utility
	abstractImage("AbstractImage"),
	nodeClass("Node"),

	// SDK
	nodeComponent("NodeComponent"),
	entityClass("Entity"),
	worldClass("World")

	#ifndef NDK_SERVER
	,

	// Audio
	soundBuffer("SoundBuffer"),
	soundEmitter("SoundEmitter"),
	soundClass("Sound"),

	// Graphics
	instancedRenderable("InstancedRenderable"),
	modelClass("Model"),

	// SDK
	graphicsComponent("GraphicsComponent")
	#endif
	{
		BindCore();
		BindMath();
		BindNetwork();
		BindSDK();
		BindUtility();

		#ifndef NDK_SERVER
		BindAudio();
		BindGraphics();
		BindRenderer();
		#endif
	}

	void LuaBinding::RegisterClasses(Nz::LuaInstance& instance)
	{
		RegisterCore(instance);
		RegisterMath(instance);
		RegisterNetwork(instance);
		RegisterSDK(instance);
		RegisterUtility(instance);

		#ifndef NDK_SERVER
		RegisterAudio(instance);
		RegisterGraphics(instance);
		RegisterRenderer(instance);
		#endif
	}
}