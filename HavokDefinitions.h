#pragma once
#include <Common/Base/Types/hkBaseTypes.h>
#include <Common/Base/Memory/hkThreadMemory.h>
#include <skse/NiTypes.h>
static const float scaleToHavok = 0.01425;
class hkVector4 {
public:
	hkVector4() { x = 0; y = 0; z = 0; w = 1; };
	hkVector4(NiPoint3 p) {
		x = p.x;
		y = p.y;
		z = p.z;
		w = 0.0f;
	}
	hkVector4(float _x, float _y, float _z, float _w = 0.0f) {
		x = _x;
		y = _y;
		z = _z;
		w = _w;
	}
	__declspec(align(16)) hkReal x;
	hkReal y;
	hkReal z;
	hkReal w;
};

class hkMatrix3 {
public:
	hkVector4 m_col0;
	hkVector4 m_col1;
	hkVector4 m_col2;
};
#	define HK_FORCE_ALIGN16 __declspec(align(16))
class HK_FORCE_ALIGN16 hkRotation : public hkMatrix3 {
public:
};
#undef HK_FORCE_ALIGN16
class hkTransform {
public:
	hkRotation m_rotation;
	hkVector4 m_translation;
};
class hkQuaternion {
public:
	hkVector4 m_vec;
};
class hkSweptTransform {
public:
	hkVector4 m_centerOfMass0;
	hkVector4 m_centerOfMass1;
	hkQuaternion m_rotation0;
	hkQuaternion m_rotation1;
	hkVector4 m_centerOfMassLocal;
};

class hkpWorld;
class hkBaseObject {
public:
	HK_DECLARE_REFLECTION();
	HK_FORCE_INLINE virtual ~hkBaseObject() {};
};

class hkStatisticsCollector;
class hkReferencedObject : public hkBaseObject {
public:
	HK_DECLARE_CLASS_ALLOCATOR(HK_MEMORY_CLASS_BASE_CLASS);
	HK_DECLARE_REFLECTION();
	virtual ~hkReferencedObject() {};
	virtual const hkClass* getClassType() const;
	virtual void calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;
	enum {
		MASK_MEMSIZE = 0x7fff
	};
	hkUint16 m_memSizeAndFlags; //+nosave
	mutable hkUint16 m_referenceCount; //+nosave
};
class hkpShape;
class hkMotionState {
public:
	hkTransform m_transform;
	hkSweptTransform m_sweptTransform;
	hkVector4 m_deltaAngle;
	hkReal m_objectRadius;
	hkReal	m_linearDamping;
	hkReal	m_angularDamping;
	hkUFloat8 m_maxLinearVelocity;
	hkUFloat8 m_maxAngularVelocity;
	hkUint8	m_deactivationClass;
};
class hkpWorldObject : public hkReferencedObject {
public:
	virtual int setShape(const hkpShape* shape);
	inline virtual ~hkpWorldObject();
	virtual hkMotionState* getMotionState() = 0;
};
class hkpEntity : public hkpWorldObject {
public:
	HK_DECLARE_REFLECTION();
	HK_DECLARE_CLASS_ALLOCATOR(HK_MEMORY_CLASS_ENTITY);
	virtual ~hkpEntity() {};
	virtual void deallocateInternalArrays();
	virtual hkMotionState* getMotionState();
};
class hkpRigidBody : public hkpEntity {
public:
	virtual ~hkpRigidBody() {};
	virtual hkpRigidBody* clone() const;
	virtual hkMotionState* getMotionState();
};
class hkpPhysicsSystem : public hkReferencedObject {
public:
	virtual ~hkpPhysicsSystem() {};
	virtual hkpPhysicsSystem* clone () const;
	virtual hkBool hasContacts () { return false; }
	hkpRigidBody** m_rigidBodies;
	size_t rigidBodyCount;
};
STATIC_ASSERT(offsetof(hkpPhysicsSystem, m_rigidBodies) == 0x8);