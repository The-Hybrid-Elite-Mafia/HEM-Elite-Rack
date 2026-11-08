#include "hem/ui/Face.hpp"
#include "hem/rack/Plugins.hpp"
#include <cstdio>
// every plug-in's pockets (recesses) and grooves, in plate units
int main() {
    for (const auto& p : hem::rack::plugins()) {
        const auto L = hem::ui::faceLayout(p);
        for (const auto& r : L.recesses) std::printf("%s|R|%.1f %.1f %.1f %.1f|%.1f %d %d\n", p.name, r.r.x, r.r.y, r.r.w, r.r.h, r.radius, r.round ? 1 : 0, r.glass ? 1 : 0);
        for (const auto& w : L.widgets)
            if (w.kind == hem::ui::WKind::Knob) std::printf("%s|K|%.1f %.1f %.1f %.1f|%d\n", p.name, w.r.x, w.r.y, w.r.w, w.r.h, w.small ? 1 : 0);
        for (const auto& g : L.grooves) std::printf("%s|G|%.1f %.1f %.1f %.1f\n", p.name, g[0], g[1], g[2], g[3]);
    }
}
