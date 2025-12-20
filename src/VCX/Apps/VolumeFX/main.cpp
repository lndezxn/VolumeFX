#include "Assets/bundled.h"
#include "Apps/VolumeFX/App.h"

int main() {
    using namespace VCX;
    return Engine::RunApp<Apps::VolumeFX::App>(Engine::AppContextOptions {
        .Title      = "VCX VolumeFX",
        .WindowSize = { 1280, 800 },
        .FontSize   = 16,

        .IconFileNames = Assets::DefaultIcons,
        .FontFileNames = Assets::DefaultFonts,
    });
}
