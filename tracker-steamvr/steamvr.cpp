/*
 * Copyright (c) 2017, Benjamin Flegel
 * Copyright (c) 2017, Stanislaw Halik
 * Copyright (c) 2017, Anthony Coddington
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "steamvr.hpp"

#include "api/plugin-api.hpp"
#include "compat/util.hpp"

#include <cstdlib>
#include <cmath>
#include <type_traits>
#include <algorithm>

#include <QMessageBox>
#include <QDebug>

QMutex device_list::mtx(QMutex::Recursive);

template<typename F>
static auto with_vr_lock(F&& fun) -> decltype(fun(vr_t(), error_t()))
{
    QMutexLocker l(&device_list::mtx);
    error_t e; vr_t v;
    std::tie(v, e) = device_list::vr_init();
    return fun(v, e);
}

void device_list::fill_device_specs(QList<device_spec>& list)
{
    with_vr_lock([&](vr_t v, error_t)
    {
        list.clear();
        list.reserve(max_devices);

        pose_t device_states[max_devices];

        if (v)
        {
            v->GetDeviceToAbsoluteTrackingPose(origin::TrackingUniverseSeated, 0,
                                               device_states, vr::k_unMaxTrackedDeviceCount);

            static constexpr unsigned bufsiz = vr::k_unTrackingStringSize;
            static char str[bufsiz] {}; // vr_lock prevents reentrancy

            for (unsigned k = 0; k < vr::k_unMaxTrackedDeviceCount; k++)
            {
               if (v->GetTrackedDeviceClass(k) == vr::ETrackedDeviceClass::TrackedDeviceClass_Invalid ||
                   v->GetTrackedDeviceClass(k) == vr::ETrackedDeviceClass::TrackedDeviceClass_TrackingReference)
                   continue;

               if (!device_states[k].bDeviceIsConnected)
                   continue;

               unsigned len;

               len = v->GetStringTrackedDeviceProperty(k, vr::ETrackedDeviceProperty::Prop_SerialNumber_String, str, bufsiz);
               if (!len)
               {
                   qDebug() << "steamvr: getting serial number failed for" << k;
                   continue;
               }

               device_spec dev;

               dev.serial = str;

               len = v->GetStringTrackedDeviceProperty(k, vr::ETrackedDeviceProperty::Prop_ModelNumber_String, str, bufsiz);
               if (!len)
               {
                   qDebug() << "steamvr: getting model number failed for" << k;
                   continue;
               }

               switch (v->GetTrackedDeviceClass(k))
               {
               case vr::ETrackedDeviceClass::TrackedDeviceClass_HMD:
                   dev.type = "HMD"; break;
               case vr::ETrackedDeviceClass::TrackedDeviceClass_Controller:
                   dev.type = "Controller"; break;
               default:
                   dev.type = "Unknown"; break;
               }

               dev.model = str;
               dev.pose = device_states[k];
               dev.k = k;

               list.push_back(dev);
            }
        }
    });
}

device_list::device_list()
{
    refresh_device_list();
}

void device_list::refresh_device_list()
{
    fill_device_specs(device_specs);
}

device_list::maybe_pose device_list::get_pose(int k)
{
    if (k < 0 || !(k < max_devices))
        return maybe_pose(false, pose_t{});

    return with_vr_lock([k](vr_t v, error_t)
    {
        static pose_t poses[max_devices] {}; // vr_lock removes reentrancy

        v->GetDeviceToAbsoluteTrackingPose(origin::TrackingUniverseSeated, 0,
                                           poses, max_devices);

        const pose_t& pose = poses[k];

        if (pose.bPoseIsValid && pose.bDeviceIsConnected)
            return maybe_pose(true, poses[k]);
        else
            once_only(qDebug() << "steamvr:"
                               << "no valid pose from device" << k
                               << "valid" << pose.bPoseIsValid
                               << "connected" << pose.bDeviceIsConnected);

        return maybe_pose(false, pose_t{});
    });
}

tt device_list::vr_init()
{
    static tt t = vr_init_();
    return t;
}

tt device_list::vr_init_()
{
    error_t error = error_t::VRInitError_Unknown;
    vr_t v = vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Other);

    if (v)
        std::atexit(vr::VR_Shutdown);
    else
        qDebug() << "steamvr: init failure" << error << device_list::strerror(error);

    return tt(v, error);
}

QString device_list::strerror(error_t err)
{
    const char* str(vr::VR_GetVRInitErrorAsSymbol(err));
    return QString(str ? str : "No description");
}

steamvr::steamvr() : device_index(-1)
{
}

steamvr::~steamvr()
{
}

void steamvr::start_tracker(QFrame*)
{
    with_vr_lock([this](vr_t v, error_t e)
    {
        if (!v)
        {
            QMessageBox::warning(nullptr,
                                 tr("SteamVR init error"), device_list::strerror(e),
                                 QMessageBox::Close, QMessageBox::NoButton);
            return;
        }

        const QString serial = s.device_serial().toString();
        device_list d;
        const QList<device_spec>& specs = d.devices();
        const int sz = specs.count();

        if (sz == 0)
        {
            QMessageBox::warning(nullptr,
                                 tr("SteamVR init error"),
                                 tr("No HMD connected"),
                                 QMessageBox::Close, QMessageBox::NoButton);
            return;
        }

        device_index = -1;

        for (const device_spec& spec : specs)
        {
            if (serial == "" || serial == spec.to_string())
            {
                device_index = int(spec.k);
                break;
            }
        }

        if (device_index == -1)
        {
            QMessageBox::warning(nullptr,
                                 tr("SteamVR init error"),
                                 tr("Can't find device with that serial"),
                                 QMessageBox::Close, QMessageBox::NoButton);
        }
    });
}

void steamvr::data(double* data)
{
    if (device_index != -1)
    {
        pose_t pose; bool ok;
        std::tie(ok, pose) = device_list::get_pose(device_index);
        if (ok)
        {
            static constexpr int c = 10;

            const auto& result = pose.mDeviceToAbsoluteTracking;

            data[TX] = -result.m[0][3] * c;
            data[TY] = result.m[1][3] * c;
            data[TZ] = result.m[2][3] * c;

            matrix_to_euler(data[Yaw], data[Pitch], data[Roll], result);

            static constexpr double r2d = 180 / M_PI;
            data[Yaw] *= r2d; data[Pitch] *= r2d; data[Roll] *= r2d;
        }
    }
}

bool steamvr::center()
{
    with_vr_lock([&](vr_t v, error_t)
    {
        if (v)
            // Reset yaw and position
            v->ResetSeatedZeroPose();
    });
    // Use chaperone universe real world up instead of OpenTrack's initial pose centering
    // Note: Controllers will be centered based on initial headset position.
    // TODO: may want to center controller/tracker yaw and position (only) when used alone
    return true;
}

void steamvr::matrix_to_euler(double& yaw, double& pitch, double& roll, const vr::HmdMatrix34_t& result)
{
    using std::atan2;

    yaw = atan2((double)-result.m[2][0], sqrt((double)result.m[2][1] * result.m[2][1] + result.m[2][2] * result.m[2][2]));
    pitch = atan2((double)result.m[2][1], (double)result.m[2][2]);
    roll = atan2((double)result.m[1][0], (double)result.m[0][0]);

    // TODO: gimbal lock avoidance?
}

steamvr_dialog::steamvr_dialog()
{
    ui.setupUi(this);

    connect(ui.buttonBox, SIGNAL(accepted()), this, SLOT(doOK()));
    connect(ui.buttonBox, SIGNAL(rejected()), this, SLOT(doCancel()));

    ui.device->clear();
    ui.device->addItem("First available", QVariant(QVariant::String));

    device_list list;
    for (const device_spec& spec : list.devices())
        ui.device->addItem(spec.to_string(), spec.to_string());

    tie_setting(s.device_serial, ui.device);
}

void steamvr_dialog::doOK()
{
    s.b->save();
    close();
}

void steamvr_dialog::doCancel()
{
    close();
}



QString device_spec::to_string() const
{
    return QStringLiteral("<%1> %2 [%3]").arg(type).arg(model).arg(serial);
}

OPENTRACK_DECLARE_TRACKER(steamvr, steamvr_dialog, steamvr_metadata)
