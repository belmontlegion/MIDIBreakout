# MIDIBreakout

**MIDIBreakout** is a Windows command-line utility that splits a MIDI file into multiple mono-voice tracks per instrument/track.  
It preserves tempo, time/key signatures, channel setup (Program Change, Volume, Pan, Expression, Pitch Bend, etc.), and writes clean, playable `.mid` files.  
Drum tracks (channel 10) are also handled: cymbals can be isolated from the rest of the kit for cleaner stems.

> Built for composers, arrangers, and ROM-hack/retro fans who want quick, reliable chord deconstruction.

---

## ✨ Features
- Split **one track** or **all tracks** (including drums) into **separate mono-voice files**
- **Preserves** tempo map, time-signature, key-signature, SMPTE offset (if present)
- **Copies channel setup & automation** (Program Change, CCs, Pitch Bend, Channel Pressure) up to the first note
- **Optional drum split**: cymbals vs. the rest of the kit (if enabled in your build)
- Smart naming: `Example-track4-Overdriven Guitar-voice2.mid`
- Short, useful logs (no per-tick spam), saved next to the executable
- No external DLLs needed when built with `-static-libstdc++ -static-libgcc`

---

## 📦 What’s in this Release ZIP
```
MIDIBreakout-<version>.zip
├─ MIDIBreakout.exe                 (your compiled binary – you add this before uploading)
├─ README.md                        (this file)
├─ LICENSE                          (MIT for MIDIBreakout)
├─ THIRD_PARTY_NOTICES.md           (attribution for dependencies)
└─ RELEASE_NOTES.md                 (highlights & changes)
```
> Tip: Put your built `MIDIBreakout.exe` into this folder, then zip it as `MIDIBreakout-v1.0.0.zip` for GitHub Releases.

---

## 🖥 Requirements
- Windows 10/11
- A standard MIDI (.mid) file

---

## 🚀 Usage
Open **Command Prompt** and run:
```bat
MIDIBreakout.exe
```

You’ll be prompted to:
1) Enter the **path to a MIDI file** (e.g., `C:\Users\You\Music\song.mid`)
2) Choose **single track** or **all tracks**  
   - If single: you’ll be asked for the track index (as shown in the log).  
   - If all: the program will create a subfolder `"<name> - Split chords"` next to the source file.
3) The program writes the stems and opens the output folder when finished.

**Log file:** `MIDI_Voice_Separation_Log.txt` is written next to `MIDIBreakout.exe`.

---

## 📁 Output Naming
- For instruments: `Example-track4-Overdriven Guitar-voice1.mid`
- For drums: `Example-track8-drums.mid` and `Example-track8-cymbals.mid` (if drum-split logic is enabled in your build)

---

## ℹ Notes
- This tool does not quantize; it preserves original timing.
- Overlapping same-pitch notes are handled carefully to avoid premature note-offs.
- If you see 0 KB outputs, ensure you’re using the latest fixed build and a valid input file.

---

## 🙌 Credits
- **Primary Author:** Scott McKay  
- **Assistant:** GPT-5 Thinking (OpenAI)
- **MIDI parsing:** Craig Stuart Sapp’s *midifile* library (MIT License)

---

## 📝 License
This project is released under the **MIT License** (see `LICENSE`).  
Make sure to include `THIRD_PARTY_NOTICES.md` when redistributing.


Enjoy!
