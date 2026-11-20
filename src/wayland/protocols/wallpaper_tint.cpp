// SPDX-License-Identifier: BSD-3-Clause

#include "wayland/protocols/wallpaper_tint.h"

#include <cutefish-wallpaper-tint-v1-server-protocol.h>
#include <new>

#include "compositor/surface.h"
#include "compositor/surface_observer.h"
#include "wayland/server_util.h"

namespace cutefish::wayland {
namespace {

class TintObserver final : public compositor::SurfaceObserver
{
public:
    TintObserver(compositor::Surface* surface, wl_resource* resource)
        : m_surface(surface), m_resource(resource)
    {
        m_surface->addSurfaceObserver(this);
        m_surface->setHasWallpaperTintObserver(true);
        onSurfaceWallpaperTintChanged(surface);
    }

    ~TintObserver() override
    {
        if (m_surface)
            onSurfaceDestroying(m_surface);
    }

    void onSurfaceDestroying(compositor::Surface* surface) override
    {
        surface->removeSurfaceObserver(this);
        surface->setHasWallpaperTintObserver(false);
        m_surface = nullptr;
    }

    void onSurfaceWallpaperTintChanged(compositor::Surface* surface) override
    {
        const auto tint = surface->wallpaperTint();
        cutefish_wallpaper_tint_v1_send_tone(m_resource, tint.light, tint.dark);
    }

private:
    compositor::Surface* m_surface;
    wl_resource* m_resource;
};

void destroy(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

const struct cutefish_wallpaper_tint_v1_interface tintImplementation = {destroy};

void getTint(wl_client* client, wl_resource* manager, uint32_t id, wl_resource* resource)
{
    auto* surface = getUserDataAs<compositor::Surface>(resource);
    if (surface->hasWallpaperTintObserver()) {
        wl_resource_post_error(manager, CUTEFISH_WALLPAPER_TINT_MANAGER_V1_ERROR_ALREADY_EXISTS,
                               "a wallpaper tint object already exists for this surface");
        return;
    }
    auto* tint = wl_resource_create(client, &cutefish_wallpaper_tint_v1_interface, 1, id);
    if (!tint) {
        wl_client_post_no_memory(client);
        return;
    }
    try {
        setImplementation(tint, &tintImplementation, std::make_unique<TintObserver>(surface, tint));
    } catch (const std::bad_alloc&) {
        wl_resource_destroy(tint);
        wl_client_post_no_memory(client);
    }
}

const struct cutefish_wallpaper_tint_manager_v1_interface managerImplementation = {destroy, getTint};

void bind(wl_client* client, void*, uint32_t, uint32_t id)
{
    auto* resource = wl_resource_create(client, &cutefish_wallpaper_tint_manager_v1_interface, 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &managerImplementation, nullptr, nullptr);
}

} // namespace

wl_global* createWallpaperTintGlobal(wl_display* display)
{
    return wl_global_create(display, &cutefish_wallpaper_tint_manager_v1_interface, 1, nullptr, bind);
}

} // namespace cutefish::wayland
