#pragma once

#include "SourceInterfaces.h"

#include "WrpConsole.h"

#include <list>

void CalcSmooth(double deltaT, double targetPos, double & lastPos, double & lastVel, double LimitVelocity, double LimitAcceleration);

class IMirvCalc abstract
{
public:
	virtual void AddRef(void) abstract = 0;
	virtual void Release(void) abstract = 0;;

	virtual int GetRefCount(void) abstract = 0;

	virtual  char const * GetName(void) abstract = 0;

	virtual void Console_PrintBegin(void) abstract = 0;
	virtual void Console_PrintEnd(void) abstract = 0;

	virtual void Console_Edit(IWrpCommandArgs * args) abstract = 0;
};

class IMirvHandleCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle) abstract = 0;
};

class IMirvVecAngCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles) abstract = 0;
};

class IMirvCamCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov) abstract = 0;
};

class IMirvFovCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcFov(float & outFov) abstract = 0;
};

class IMirvBoolCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcBool(bool & outResult) abstract = 0;
};

class IMirvIntCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcInt(int & outResult) abstract = 0;
};

class IMirvFloatCalc abstract : public IMirvCalc
{
public:
	virtual bool CalcFloat(float& outResult) abstract = 0;
};

class CMirvHandleCalcs
{
public:
	~CMirvHandleCalcs();

	IMirvHandleCalc * GetByName(char const * name);

	IMirvHandleCalc * NewValueCalc(char const * name, int handle);
	IMirvHandleCalc * NewIndexCalc(char const * name, int entityIndex);
	IMirvHandleCalc * NewKeyCalc(char const * name, int slot);
	IMirvHandleCalc * NewActiveWeaponCalc(char const * name, IMirvHandleCalc * parent, bool world);
	IMirvHandleCalc * NewLocalPlayerCalc(char const * name);
	IMirvHandleCalc * NewObserverTargetCalc(char const * name, IMirvHandleCalc * parent);
	IMirvHandleCalc * NewOrCalc(char const * name, IMirvHandleCalc * a, IMirvHandleCalc * b);
	IMirvHandleCalc * NewOwnerEntityCalc(char const * name, IMirvHandleCalc * calc, unsigned int maxDepth = 1);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvHandleCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvHandleCalc *>::iterator & outIt);
};

extern CMirvHandleCalcs g_MirvHandleCalcs;


class CMirvVecAngCalcs
{
public:
	~CMirvVecAngCalcs();

	IMirvVecAngCalc * GetByName(char const * name);

	IMirvVecAngCalc * NewValueCalc(char const * name, float x, float y, float z, float rX, float rY, float rZ);
	IMirvVecAngCalc * NewAddCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b);
	IMirvVecAngCalc * NewSubtractCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b);
	IMirvVecAngCalc * NewOffsetCalc(char const * name, IMirvVecAngCalc * parent, IMirvVecAngCalc * offset, bool order);
	IMirvVecAngCalc * NewHandleCalc(char const * name, IMirvHandleCalc * handle);
	IMirvVecAngCalc * NewHandleEyeCalc(char const * name, IMirvHandleCalc * handle);
	IMirvVecAngCalc * NewHandleCalcEx(char const * name, IMirvHandleCalc * handle, bool eyeVec, bool eyeAng);
	IMirvVecAngCalc * NewTf2EntityIndexCalc(char const * name, int entityIndex, int boneIndex = -1, int mode = 0);
	IMirvVecAngCalc * NewHandleAttachmentCalc(char const * name, IMirvHandleCalc * handle, char const * attachmentName);
	IMirvVecAngCalc * NewIfCalc(char const * name, IMirvBoolCalc * condition, IMirvVecAngCalc * condTrue, IMirvVecAngCalc * condFalse);
	IMirvVecAngCalc * NewOrCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b);
	IMirvVecAngCalc * NewCamCalc(char const * name, IMirvCamCalc * src);
	IMirvVecAngCalc * NewMotionProfile2Calc(char const * name, IMirvVecAngCalc * parent, IMirvHandleCalc * trackHandle);
	IMirvVecAngCalc * NewSwitchInterpCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * switchHandle, IMirvHandleCalc * resetHandle, float holdTime, float interpTime);
	IMirvVecAngCalc * NewLocalToGlobalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle);
	IMirvVecAngCalc * NewGlobalToLocalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle);
	IMirvVecAngCalc * NewCollisionClipCalc(char const * name, IMirvVecAngCalc * source, IMirvVecAngCalc * clipTo, IMirvHandleCalc * ignoreHandle, float safeZoneDist);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvVecAngCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvVecAngCalc *>::iterator & outIt);
};

extern CMirvVecAngCalcs g_MirvVecAngCalcs;


class CMirvCamCalcs
{
public:
	~CMirvCamCalcs();

	IMirvCamCalc * GetByName(char const * name);

	IMirvCamCalc * NewCamCalc(char const * name, const char * camFileName, const char * startClientTime);
	IMirvCamCalc * NewGameCalc(char const * name);
	IMirvCamCalc * NewCurrentCalc(char const * name);
	IMirvCamCalc * NewPlayerCalc(char const* name, IMirvHandleCalc* handleCalc);
	IMirvCamCalc * NewSmoothCalc(char const * name, IMirvCamCalc * parent, IMirvHandleCalc * trackHandle);
	IMirvCamCalc * NewVecAngFovCalc(char const * name, IMirvVecAngCalc * vecAng, IMirvFovCalc * fov);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvCamCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvCamCalc *>::iterator & outIt);
};

extern CMirvCamCalcs g_MirvCamCalcs;


class CMirvFovCalcs
{
public:
	~CMirvFovCalcs();

	IMirvFovCalc * GetByName(char const * name);

	IMirvFovCalc * NewCamCalc(char const * name, IMirvCamCalc * src);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvFovCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvFovCalc *>::iterator & outIt);
};

extern CMirvFovCalcs g_MirvFovCalcs;



class CMirvBoolCalcs
{
public:
	~CMirvBoolCalcs();

	IMirvBoolCalc * GetByName(char const * name);

	IMirvBoolCalc * NewHandleCalc(char const * name, IMirvHandleCalc * handle);
	IMirvBoolCalc * NewVecAngCalc(char const * name, IMirvVecAngCalc * vecAng);
	IMirvBoolCalc * NewAliveCalc(char const * name, IMirvHandleCalc * vecAng);
	IMirvBoolCalc * NewAndCalc(char const * name, IMirvBoolCalc * a, IMirvBoolCalc * b);
	IMirvBoolCalc * NewOrCalc(char const * name, IMirvBoolCalc * a, IMirvBoolCalc * b);
	IMirvBoolCalc * NewNotCalc(char const * name, IMirvBoolCalc * a);
	IMirvBoolCalc * NewHandlesEqualCalc(char const * name, IMirvHandleCalc * a, IMirvHandleCalc * b);
	IMirvBoolCalc * NewIntsEqualCalc(char const * name, IMirvIntCalc * a, IMirvIntCalc * b);
	IMirvBoolCalc * NewClassnameMatchesCalc(char const * name, IMirvHandleCalc * handle, const char * wildCardString);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvBoolCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvBoolCalc *>::iterator & outIt);
};

extern CMirvBoolCalcs g_MirvBoolCalcs;


class CMirvIntCalcs
{
public:
	~CMirvIntCalcs();

	IMirvIntCalc * GetByName(char const * name);

	IMirvIntCalc * NewValueCalc(const char * name, int value);
	IMirvIntCalc * NewTeamNumberCalc(char const * name, IMirvHandleCalc * handle);

	bool Console_CheckName(char const * name);
	void Console_Remove(char const * name);
	void Console_Print(void);

private:
	std::list<IMirvIntCalc *> m_Calcs;

	void GetIteratorByName(char const * name, std::list<IMirvIntCalc *>::iterator & outIt);
};

extern CMirvIntCalcs g_MirvIntCalcs;


class CMirvFloatCalcs
{
public:
	~CMirvFloatCalcs();

	IMirvFloatCalc* GetByName(char const* name);

	IMirvFloatCalc* NewValueCalc(const char* name, float value);
	IMirvFloatCalc* NewIfCalc(char const* name, IMirvBoolCalc* condition, IMirvFloatCalc* condTrue, IMirvFloatCalc* condFalse);

	bool Console_CheckName(char const* name);
	void Console_Remove(char const* name);
	void Console_Print(void);

private:
	std::list<IMirvFloatCalc*> m_Calcs;

	void GetIteratorByName(char const* name, std::list<IMirvFloatCalc*>::iterator& outIt);
};

extern CMirvFloatCalcs g_MirvFloatCalcs;

void MirvCalcs_LevelInitPreEntity();


// TF2 helpers (used by mirv_listplayers, mirv_listbones and mirv_cam attach).

#define MIRV_TF2_MAX_BONES 128

// How a bone based TF2 entity calc gets its angles:
// bone: The bone's own angles (arbitrary, depends on the skeleton).
// entity: The entity's angles (position is still the bone's).
// aligned: Starts out facing like the entity and then follows the bone's rotation.
#define MIRV_TF2_BONEMODE_BONE 0
#define MIRV_TF2_BONEMODE_ENTITY 1
#define MIRV_TF2_BONEMODE_ALIGNED 2
// world: Follow entity/bone translation with a fixed world-space orientation.
#define MIRV_TF2_BONEMODE_WORLD 3

struct MirvTf2Bone_t
{
	bool valid;
	float origin[3]; // world position
	float angles[3]; // pitch, yaw, roll (degrees)
};

/// <returns>Highest possible player entity index (0 if unknown).</returns>
int Mirv_Tf2_GetMaxPlayers(void);

/// <returns>true if entityIndex is a connected player, name and user id are then filled.</returns>
bool Mirv_Tf2_GetPlayerInfo(int entityIndex, char * outName, int outNameSize, int * outUserId);

/// <returns>true on success. Origin and angles (pitch, yaw, roll) of the entity.</returns>
bool Mirv_Tf2_GetEntityOriginAngles(int entityIndex, float outOrigin[3], float outAngles[3]);

/// <param name="outBones">Array of MIRV_TF2_MAX_BONES elements.</param>
/// <param name="outCount">Highest valid bone index + 1.</param>
/// <returns>true on success.</returns>
bool Mirv_Tf2_GetBones(int entityIndex, MirvTf2Bone_t * outBones, int & outCount);

struct MirvTf2BoneName_t
{
	char name[64];
	int parent; // parent bone index, -1 for root bones
};

/// <param name="outNames">Array of MIRV_TF2_MAX_BONES elements.</param>
/// <returns>Number of bones with names (0 if names are not available).</returns>
int Mirv_Tf2_GetBoneNames(int entityIndex, MirvTf2BoneName_t * outNames);

// Optional TF2 camera collision.
struct MirvCameraCollisionSettings
{
	bool Free = false, Campath = false, Attached = false, Players = true, Slide = true, IgnoreLocal = true;
	double Radius = 6, ReturnHalfLife = .2, ReturnSpeed = 300, FollowHalfLife = 0, FollowMaxDistance = 128;
	double BoneHalfLife = 0, FollowTeleportDistance = 1024;
};
MirvCameraCollisionSettings MirvCameraCollisionGet();
bool MirvCameraCollisionSet(const MirvCameraCollisionSettings &settings);
void MirvCameraCollisionReset();
void MirvCameraAttachedReset();
bool MirvCameraCollisionApply(bool freeCamera, const void *path, unsigned long long revision,
	double sceneTime, double pathTime, const double seed[3], float &x, float &y, float &z);
bool MirvCameraAttachedApply(const void *identity, void *entity, const double pivot[3],
	double sceneTime, float &x, float &y, float &z, const double *entityOrigin = nullptr);
// End optional TF2 camera collision.

// True if tracing is available. outIndex is -1 when no networked entity was hit.
bool Mirv_Tf2_TraceEntity(const double origin[3], const double angles[3], int & outIndex);

/// <summary>Parses a bone argument: A bone index or (part of) a bone name. Prints a warning if it fails.</summary>
/// <returns>true on success.</returns>
bool Mirv_Tf2_ParseBoneArg(int entityIndex, const char * arg, int & outBone);

// Cached bone/entity pivot and entity origin from the latest attachment evaluation.
bool Mirv_Tf2_GetAttachAnchor(IMirvVecAngCalc *calc, double origin[3], void *&entity, double entityOrigin[3]);

/// <summary>Makes the camera pose (e.g. from a free camera) the fixed pose of the camera relative to the target of a TF2 entity calc (created with NewTf2EntityIndexCalc).
/// From now on position and angles follow the target (entity or bone) and keep this relative pose.</summary>
/// <param name="camOrigin">World position.</param>
/// <param name="camAngles">World angles (pitch, yaw, roll).</param>
/// <param name="outLocalOffset">Position relative to the target in the camera's own axes (forward, left, up).</param>
/// <returns>true on success.</returns>
bool Mirv_Tf2_CaptureCamera(IMirvVecAngCalc * calc, const double camOrigin[3], const double camAngles[3], float outLocalOffset[3]);

/// <returns>true on success. The entity's abs angles and render angles (pitch, yaw, roll).</returns>
bool Mirv_Tf2_GetEntityAnglesInfo(int entityIndex, float outAbsAngles[3], float outRenderAngles[3]);

// Renderer classification (unknown classes fall back to Other).
enum MirvTf2EntityCategory_t
{
 MIRV_TF2_ENTITY_PLAYER = 0,
 MIRV_TF2_ENTITY_PROJECTILE = 1,
 MIRV_TF2_ENTITY_WEAPON = 2,
 MIRV_TF2_ENTITY_COSMETIC = 3,
 MIRV_TF2_ENTITY_OTHER = 4,
 MIRV_TF2_ENTITY_CATEGORY_COUNT = 5
};

struct MirvTf2EntityLabel_t
{
	int category;
	int index;
	float origin[3]; // world position: Top center of the entity's world space bounds (its render origin if the bounds are not usable).
};

/// <summary>Collects the entities that should get an index label (see mirv_drawentities).</summary>
/// <param name="playersOnly">If true only players are collected, otherwise all entities that would be drawn.</param>
/// <param name="outLabels">Array with space for maxCount elements.</param>
/// <returns>Number of entities collected.</returns>
int Mirv_Tf2_CollectEntityLabels(bool playersOnly, MirvTf2EntityLabel_t * outLabels, int maxCount);

