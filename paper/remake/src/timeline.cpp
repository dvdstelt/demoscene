// The running order in IRQ0 ticks, from the traced run of the original (paper/re/PARTS.md; flock at 34 fps).
#include "demo.h"

const std::vector<Cue>& timeline() {
    static const std::vector<Cue> t = {
        {0, 104, "precalc"},        // black while the original precalculates
        {104, 1079, "title"},       // "psychic link" / "sixty four k" over the swirling warp
        {1079, 3186, "sketch"},     // paper, the red pencil comes in and sketches the scene
        {3186, 5480, "scene3d"},    // the sketch turns into the coloured 3D scene, banner with "Statix + Vic"
        {5480, 5611, "peel1"},      // the logo page is built, then the 3D scene peels off to reveal it
        {5611, 6018, "logo"},       // "present the Paper intro", ink flood fill
        {6018, 7741, "teal"},       // the logo page peels off; one paper plane over the teal canyon
        {7741, 8818, "greetings"},  // names on the bump-mapped water
        {8818, 10968, "flock"},     // 32 paper planes in the blue canyon
        {10968, 11377, "theend"},   // "t h e   e n d" over the olive warp
        {11377, 12235, "closing"},  // "1996" and "psychic link", fading out
    };
    return t;
}
