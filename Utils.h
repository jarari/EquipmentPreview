#pragma once
#include <skse/GameReferences.h>
class Actor;
class NiMatrix33;
class NiPoint3;
class TESCamera;
class ActiveEffect;
class TESObjectREFR;
struct Quaternion {
public:
	float x, y, z, w;
	Quaternion();
	Quaternion(float _x, float _y, float _z, float _w);
	float Norm();
	NiMatrix33 ToRotationMatrix33();
};
namespace Utils {
	float Scale(NiPoint3 vec);
	float Lerp(float a, float b, float t);
	float LerpUnsafe(float a, float b, float t); //Use this only when you're absolutely sure that t is between 0-1.
	void NormalizeVector(float& x, float& y, float& z);
	void NormalizeVector(NiPoint3& vec);
	void GetCameraForward(TESCamera* cam, NiPoint3& vecnorm);
	void GetCameraRight(TESCamera* cam, NiPoint3& vecnorm);
	void GetCameraUp(TESCamera* cam, NiPoint3& vecnorm);
	void SetMatrix33(float a, float b, float c, float d, float e, float f, float g, float h, float i, NiMatrix33& mat);
	NiMatrix33 GetRotationMatrix33(float pitch, float yaw, float roll);
	NiMatrix33 GetRotationMatrix33(NiPoint3 axis, float angle);
	NiPoint3 GetEulerAngles(NiMatrix33 mat);
	float Determinant(NiMatrix33 mat);
	NiMatrix33 Inverse(NiMatrix33 mat);
	NiPoint3 WorldToLocal(NiPoint3 wpos, NiPoint3 lorigin, NiMatrix33 rot);
	NiPoint3 LocalToWorld(NiPoint3 lpos, NiPoint3 lorigin, NiMatrix33 rot);
	bool GetNodePosition(TESObjectREFR* ref, const char* nodeName, NiPoint3& point);
	bool GetTorsoPos(TESObjectREFR* ref, NiPoint3& point);
	bool GetHeadPos(TESObjectREFR* ref, NiPoint3& point);
	bool IsInMenuMode();
	int GetPauseStack();
	int PauseGame(bool b);
	void Dump(void* mem, unsigned int size);
	const char* GetFullname(TESForm* form);
}