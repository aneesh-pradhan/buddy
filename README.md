# buddy

Helper application for the CMF Buds Pro 2 (and possibly other Nothing/CMF TWS)

Currently supports bass tuning, toggling ANC modes, and a live volume display. The dial in the header works like the case's Smart Dial: turn it (scroll, drag, or `-`/`+`) to change volume, click it (or `p`) to play/pause. Volume goes through `pactl` and play/pause through MPRIS via `busctl`. I plan on adding more features down the line. 

Primarily built this as a way to control my buds from my laptop without being tethered to the Nothing X mobile app. 

License is MIT.
