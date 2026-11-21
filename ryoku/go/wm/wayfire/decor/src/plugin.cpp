#include <memory>
#include <string>
#include <unordered_map>

#include <wayfire/core.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>

#include "options.hpp"
#include "shader.hpp"
#include "transformer.hpp"

namespace ryoku::decor
{
namespace
{
// Below TRANSFORMER_2D, so the decor sits closest to the surface and wobbly,
// animations and every other transformer move the decorated result.
constexpr int transformer_order = 0;
const std::string transformer_name = "ryoku-decor";

struct tracked_view_t
{
    wayfire_toplevel_view view;
    std::shared_ptr<decoration_node_t> node;

    wf::signal::connection_t<wf::view_activated_state_signal> activation_changed;
    wf::signal::connection_t<wf::view_fullscreen_signal> fullscreen_changed;
    wf::signal::connection_t<wf::view_geometry_changed_signal> geometry_changed;
    wf::signal::connection_t<wf::view_tiled_signal> tiled_changed;
    wf::signal::connection_t<wf::view_title_changed_signal> title_changed;
    wf::signal::connection_t<wf::view_app_id_changed_signal> app_id_changed;
};
}

class plugin_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        gles_renderer = wf::get_core().is_gles2();
        if (gles_renderer)
        {
            wf::gles::run_in_context_if_gles([this]
            {
                shader.compile();
            });
        }

        options.set_callback([this]
        {
            refresh_all();
        });

        wf::get_core().connect(&view_mapped);
        wf::get_core().connect(&view_unmapped);

        for (auto view : wf::get_core().get_all_views())
        {
            if (view->is_mapped())
            {
                track(view);
            }
        }
    }

    void fini() override
    {
        options.set_callback({});
        view_mapped.disconnect();
        view_unmapped.disconnect();

        for (auto& entry : views)
        {
            detach(*entry.second);
        }

        views.clear();
        wf::gles::run_in_context_if_gles([this]
        {
            shader.release();
        });
    }

  private:
    wf::view_matcher_t ignored_views{"ryoku-decor/ignore_views"};
    option_state_t options;
    decoration_shader_t shader;
    bool gles_renderer = false;
    std::unordered_map<wf::view_interface_t*, std::unique_ptr<tracked_view_t>> views;

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped =
        [this] (wf::view_mapped_signal *event)
    {
        track(event->view);
    };

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped =
        [this] (wf::view_unmapped_signal *event)
    {
        untrack(event->view.get());
    };

    void track(wayfire_view view)
    {
        auto toplevel = wf::toplevel_cast(view);
        if (!toplevel || !toplevel->is_mapped() || views.contains(view.get()))
        {
            return;
        }

        auto tracked = std::make_unique<tracked_view_t>();
        tracked->view = toplevel;
        tracked->activation_changed = [this] (wf::view_activated_state_signal *event)
        {
            auto found = views.find(event->view.get());
            if ((found != views.end()) && found->second->node)
            {
                found->second->node->set_activated(event->view->activated);
            }
        };
        tracked->fullscreen_changed = [this] (wf::view_fullscreen_signal *event)
        {
            refresh(event->view.get());
        };
        tracked->geometry_changed = [this] (wf::view_geometry_changed_signal *event)
        {
            auto found = views.find(event->view.get());
            if ((found != views.end()) && found->second->node)
            {
                found->second->node->refresh(options.values());
            }
        };
        tracked->tiled_changed = [this] (wf::view_tiled_signal *event)
        {
            refresh(event->view.get());
        };
        tracked->title_changed = [this] (wf::view_title_changed_signal *event)
        {
            refresh(event->view.get());
        };
        tracked->app_id_changed = [this] (wf::view_app_id_changed_signal *event)
        {
            refresh(event->view.get());
        };

        toplevel->connect(&tracked->activation_changed);
        toplevel->connect(&tracked->fullscreen_changed);
        toplevel->connect(&tracked->geometry_changed);
        toplevel->connect(&tracked->tiled_changed);
        toplevel->connect(&tracked->title_changed);
        toplevel->connect(&tracked->app_id_changed);

        auto *key = view.get();
        views.emplace(key, std::move(tracked));
        refresh(key);
    }

    void untrack(wf::view_interface_t *view)
    {
        auto found = views.find(view);
        if (found == views.end())
        {
            return;
        }

        detach(*found->second);
        views.erase(found);
    }

    bool should_attach(const tracked_view_t& tracked)
    {
        return gles_renderer && shader.is_ready() && options.values().has_effect() &&
            !ignored_views.matches(tracked.view);
    }

    void attach(tracked_view_t& tracked)
    {
        if (tracked.node)
        {
            return;
        }

        auto manager = tracked.view->get_transformed_node();
        if (manager->get_transformer(transformer_name))
        {
            manager->rem_transformer(transformer_name);
        }

        tracked.node = std::make_shared<decoration_node_t>(
            tracked.view, &shader, options.values(), gles_renderer);
        manager->add_transformer(tracked.node, transformer_order, transformer_name);
    }

    void detach(tracked_view_t& tracked)
    {
        if (!tracked.node)
        {
            return;
        }

        tracked.view->get_transformed_node()->rem_transformer(tracked.node);
        tracked.node.reset();
    }

    void refresh(wf::view_interface_t *view)
    {
        auto found = views.find(view);
        if (found == views.end())
        {
            return;
        }

        auto& tracked = *found->second;
        if (!should_attach(tracked))
        {
            detach(tracked);
            return;
        }

        attach(tracked);
        tracked.node->refresh(options.values());
    }

    void refresh_all()
    {
        for (auto& [view, tracked] : views)
        {
            refresh(view);
        }
    }
};
}

DECLARE_WAYFIRE_PLUGIN(ryoku::decor::plugin_t);
