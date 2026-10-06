# Speed Reader (C++)

A full-screen, one-word-at-a-time Windows reader for `.txt`, text-based `.pdf`, and `.epub` files. The middle letter is bold and centered.

## Build

With MinGW g++ installed, run:

`g++ -std=c++17 -O2 -municode -mwindows speed_reader.cpp -o SpeedReader.exe -lcomdlg32 -lgdi32`

Then open `SpeedReader.exe`.

## PDF support

TXT works with no dependency. PDF extraction first uses Poppler's `pdftotext.exe` if available, then automatically falls back to the installed Calibre converter. EPUB extraction uses Calibre's `ebook-convert.exe`. Scanned/image-only or protected PDFs require OCR or an unlocked text-based copy first.

## Controls

- `Space`: pause or resume
- `Left Arrow`: add 50 ms to the delay (slower)
- `Right Arrow`: reduce the delay by 50 ms (faster), minimum 50 ms
- `Up Arrow`: go back 12 words
- `Down Arrow`: go forward 20 words
- `P`: show or hide the gray context previews
- `1`: fullscreen view
- `2`: windowed view
- `3`: show or hide the right-side text overview
- `4`: enlarge the focal word
- `5`: shrink the focal word
- `R`, `W`, `G`, `B`, `Y`, `M`: set the focal text to red, white, green, blue, yellow, or magenta
- `Esc`: exit

The default delay is 300 ms per word.
