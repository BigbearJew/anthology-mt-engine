#include "stdafx.h"
#include "pch_script.h"
#include "AnomalyScriptCamera.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "CameraEffector.h"

namespace
{
// Keep independent of eCEUser, which native camera animations also use.
constexpr ECamEffectorType customCameraType = ECamEffectorType(cefNext + 33);

class CAnomalyScriptCamera final : public CEffectorCam
{
    Fmatrix current = Fidentity;
    bool initialized = false;
public:
    Fvector position = {};
    Fvector hpb = {};
    unsigned smoothing = 1;
    bool hudEnabled = false;

    CAnomalyScriptCamera() : CEffectorCam(customCameraType, FLT_MAX) {}

    BOOL ProcessCam(SCamEffectorInfo& info) override
    {
        Fmatrix target;
        target.setHPB(hpb.x, hpb.y, hpb.z).translate_over(position);
        if (!initialized || smoothing <= 1)
            current = target;
        else
        {
            // Match the Anomaly API's smoothing, whose time step is milliseconds.
            float alpha = 2.f / (float(smoothing) + 1.f) * (float(Device.dwTimeDelta) / smoothing);
            clamp(alpha, 0.f, 1.f);
            current.j.lerp(current.j, target.j, alpha);
            current.k.lerp(current.k, target.k, alpha);
            current.c.lerp(current.c, target.c, alpha);
        }
        initialized = true;
        info.n = current.j;
        info.d = current.k;
        info.p = current.c;
        return TRUE;
    }
};

void set_camera(const Fvector& position, const Fvector& hpb, unsigned smoothing,
    bool hudEnabled, bool hudAffect)
{
    CActor* actor = g_actor;
    if (!actor || !actor->HasCameraEffector() || !_valid(position) || !_valid(hpb))
        return;
    auto& cameras = actor->Cameras();
    auto* camera = static_cast<CAnomalyScriptCamera*>(cameras.GetCamEffector(customCameraType, true));
    if (!camera)
    {
        camera = new CAnomalyScriptCamera();
        cameras.AddCamEffector(camera);
    }
    camera->position = position;
    camera->hpb = hpb;
    camera->smoothing = std::max(1u, smoothing);
    camera->hudEnabled = hudEnabled;
    camera->SetHudAffect(hudAffect);
}

void set_camera2(const Fvector& p, const Fvector& d) { set_camera(p, d, 1, false, false); }
void set_camera3(const Fvector& p, const Fvector& d, unsigned s) { set_camera(p, d, s, false, false); }
void set_camera4(const Fvector& p, const Fvector& d, unsigned s, bool h) { set_camera(p, d, s, h, false); }

void remove_camera()
{
    CActor* actor = g_actor;
    if (actor && actor->HasCameraEffector())
        actor->Cameras().RemoveCamEffector(customCameraType, true);
}
}

bool AnomalyCameraHudEnabled(const CActor* actor)
{
    if (!actor || !actor->HasCameraEffector())
        return true;
    auto* camera = static_cast<CAnomalyScriptCamera*>(
        const_cast<CActor*>(actor)->Cameras().GetCamEffector(customCameraType, true));
    return !camera || camera->hudEnabled;
}

void RegisterAnomalyScriptCamera(lua_State* L)
{
    using namespace luabind;
    module(L, "level")
    [
        def("set_cam_custom_position_direction", &set_camera2),
        def("set_cam_custom_position_direction", &set_camera3),
        def("set_cam_custom_position_direction", &set_camera4),
        def("set_cam_custom_position_direction", &set_camera),
        def("remove_cam_custom_position_direction", &remove_camera)
    ];
}
