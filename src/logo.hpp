// Animated header art: a pixel-art take on the CMF Buds Pro 2 case's Smart Dial
// that spins up on launch and then idles, next to a pixel "BUDDY" wordmark.
//
// Pixels are drawn two per terminal cell with half blocks (▀/▄), which makes them
// roughly square in most fonts.
#pragma once

#include <ftxui/dom/elements.hpp>

namespace buddy {

// Dial rotation in radians `seconds` after launch: a fast spin that eases to a
// stop, leaving the dial for the user to turn.
double dial_angle(double seconds);

// The dial at the given rotation (clockwise in radians); `pressed` shades the
// face as if pushed in.
ftxui::Element dial(double angle, bool pressed = false);

// "BUDDY" in a 5x7 pixel font, drawn in the terminal's foreground color.
ftxui::Element wordmark();

}  // namespace buddy
