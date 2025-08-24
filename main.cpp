// ===== platform & std headers needed in Release =====
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>   // MAX_PATH, DWORD, GetModuleFileName, etc.
  #include <shellapi.h>  // ShellExecute
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>

// ---- Filesystem (C++17) with fallback for older GCC ----
#if defined(__has_include)
  #if __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
  #elif __has_include(<experimental/filesystem>)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
  #else
    #error "No <filesystem> support available in this toolchain."
  #endif
#else
  // Older compilers may not support __has_include; try standard first
  #include <filesystem>
  namespace fs = std::filesystem;
#endif

#include "MidiFile.h"
#include "MidiMessage.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;
using smf::MidiEvent;
using smf::MidiFile;

// ----------------------------- Utilities -----------------------------

static const char* GM_NAMES[128] = {
    "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
    "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
    "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
    "Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)","Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar harmonics",
    "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
    "Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
    "String Ensemble 1","String Ensemble 2","SynthStrings 1","SynthStrings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
    "Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","SynthBrass 1","SynthBrass 2",
    "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
    "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
    "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope)","Lead 4 (chiff)","Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)",
    "Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)","Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
    "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)","FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)",
    "Sitar","Banjo","Shamisen","Koto","Kalimba","Bag pipe","Fiddle","Shanai",
    "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
    "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
};

static const std::set<int> CYMBAL_NOTES = {
    42,44,46,49,51,52,53,55,57,59 // hihats/crash/ride (GM)
};

struct TrackInfo {
    int trackIndex = -1;
    int eventCount = 0;
    bool hasChannel10 = false;
    int programGuess = -1;
    std::string trackName;
};

struct NoteSpan {
    int startTick;
    int endTick;
    int pitch;
    int velocity;
    int channel;
};

inline void addMsg(MidiFile& mf, int track, int tick, const std::vector<unsigned char>& src) {
    std::vector<unsigned char> tmp = src;
    mf.addEvent(track, tick, tmp);
}

inline bool isMeta(const MidiEvent& e) { return e.size() > 0 && e[0] == 0xFF; }

inline bool isNoteOn(const MidiEvent& e, int& ch, int& pitch, int& vel) {
    if (e.size() < 3) return false;
    unsigned char s = e[0];
    if ((s & 0xF0) == 0x90) {
        ch = s & 0x0F;
        pitch = e[1];
        vel = e[2];
        return vel > 0;
    }
    return false;
}

inline bool isNoteOff(const MidiEvent& e, int& ch, int& pitch, int& vel) {
    if (e.size() < 3) return false;
    unsigned char s = e[0];
    if ((s & 0xF0) == 0x80) {
        ch = s & 0x0F; pitch = e[1]; vel = e[2];
        return true;
    }
    if ((s & 0xF0) == 0x90 && e[2] == 0) {
        ch = s & 0x0F; pitch = e[1]; vel = 0;
        return true;
    }
    return false;
}

inline bool isProgramChange(const MidiEvent& e, int& ch, int& prog) {
    if (e.size() < 2) return false;
    unsigned char s = e[0];
    if ((s & 0xF0) == 0xC0) { ch = s & 0x0F; prog = e[1]; return true; }
    return false;
}

inline bool isChannelMsg(const MidiEvent& e) {
    if (e.size() == 0) return false;
    unsigned char s = e[0];
    return (s >= 0x80 && s <= 0xEF);
}

inline int statusType(const MidiEvent& e) {
    if (e.size() == 0) return -1;
    return (e[0] & 0xF0);
}

inline int channelOf(const MidiEvent& e) {
    if (e.size() == 0) return -1;
    return (e[0] & 0x0F);
}

inline std::string filenameSafe(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c=='-' || c=='_' || c==' ' ) out.push_back(c);
        else out.push_back('_');
    }
    while (!out.empty() && (out.back()==' ' || out.back()=='_')) out.pop_back();
    while (!out.empty() && (out.front()==' ' || out.front()=='_')) out.erase(out.begin());
    if (out.empty()) out = "Instrument";
    return out;
}

fs::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

std::vector<unsigned char> bytesFromEvent(const MidiEvent& e) {
    std::vector<unsigned char> v;
    v.reserve(e.size());
    for (int i = 0; i < e.size(); ++i) v.push_back(e[i]);
    return v;
}

// ------------------------ Logging helper ------------------------

struct Logger {
    std::ofstream file;
    bool ok = false;

    void openAt(const fs::path& p) {
        file.open(p, std::ios::out | std::ios::trunc);
        ok = (bool)file;
    }
    void line(const std::string& s) {
        if (ok) { file << s << "\n"; file.flush(); }
        std::cout << s << "\n";
    }
};

// ------------------------ Scanning & Meta Copy ------------------------

std::vector<TrackInfo> scanTrackInfo(const MidiFile& in) {
    std::vector<TrackInfo> out;
    for (int t = 0; t < in.getTrackCount(); ++t) {
        TrackInfo ti;
        ti.trackIndex = t;
        ti.eventCount = in[t].getEventCount();
        ti.hasChannel10 = false;
        ti.programGuess = -1;

        // Track name (meta 0x03)
        for (int i = 0; i < in[t].getEventCount(); ++i) {
            const auto& ev = in[t][i];
            if (isMeta(ev) && ev.size() >= 3 && ev[1] == 0x03) {
                std::string nm;
                for (int k = 3; k < ev.size(); ++k) nm.push_back((char)ev[k]);
                ti.trackName = nm;
                break;
            }
        }

        std::map<int,int> lastProgByCh;
        for (int i = 0; i < in[t].getEventCount(); ++i) {
            const auto& ev = in[t][i];
            if (isChannelMsg(ev)) {
                int ch = channelOf(ev);
                if (ch == 9) ti.hasChannel10 = true;
                int p; int c;
                if (isProgramChange(ev, c, p)) lastProgByCh[c] = p;
            }
        }

        std::map<int,int> noteCountByCh;
        for (int i = 0; i < in[t].getEventCount(); ++i) {
            const auto& ev = in[t][i];
            int ch, pitch, vel;
            if (isNoteOn(ev, ch, pitch, vel)) noteCountByCh[ch]++;
        }
        int bestCh = -1, bestCount = -1;
        for (auto& kv : noteCountByCh) if (kv.second > bestCount) { bestCount = kv.second; bestCh = kv.first; }
        if (bestCh >= 0 && lastProgByCh.count(bestCh)) ti.programGuess = lastProgByCh[bestCh];

        out.push_back(ti);
    }
    return out;
}

struct MetaCopy {
    std::vector<std::pair<int,std::vector<unsigned char>>> metas; // tick, bytes
    void add(int tick, const std::vector<unsigned char>& b) { metas.emplace_back(tick,b); }
};
MetaCopy collectGlobalMeta(const MidiFile& in) {
    MetaCopy mc;
    for (int t = 0; t < in.getTrackCount(); ++t) {
        for (int i = 0; i < in[t].getEventCount(); ++i) {
            const auto& ev = in[t][i];
            if (!isMeta(ev) || ev.size() < 3) continue;
            unsigned char type = ev[1];
            if (type == 0x51 || type == 0x58 || type == 0x59) { // tempo, time-sig, key-sig
                mc.add(ev.tick, bytesFromEvent(ev));
            }
        }
    }
    std::sort(mc.metas.begin(), mc.metas.end(),
              [](auto& a, auto& b){return a.first < b.first;});
    return mc;
}

void collectChannelSetupAndAutomation(const MidiFile& in, int srcTrack,
                                      const std::set<int>& usedChannels,
                                      std::vector<MidiEvent>& outEv) {
    for (int i = 0; i < in[srcTrack].getEventCount(); ++i) {
        const auto& ev = in[srcTrack][i];
        if (!isChannelMsg(ev)) continue;
        int ch = channelOf(ev);
        if (!usedChannels.count(ch)) continue;

        int st = statusType(ev);
        // CC, Program Change, Pitch Bend, Channel Pressure
        if (st == 0xB0 || st == 0xC0 || st == 0xE0 || st == 0xD0) {
            outEv.push_back(ev);
        }
    }
}

// ------------------------ Note Extraction & Voices ------------------------

static std::vector<NoteSpan> extractTrackNotes(const MidiFile& in, int trackIndex, std::set<int>* channelsSeen = nullptr) {
    struct OnInfo { int tick; int vel; };
    std::unordered_map<int, std::vector<OnInfo>> ons; // (ch<<8)|pitch
    std::vector<NoteSpan> notes;

    for (int i = 0; i < in[trackIndex].getEventCount(); ++i) {
        const auto& ev = in[trackIndex][i];
        int ch, p, v;
        if (isNoteOn(ev, ch, p, v)) {
            if (channelsSeen) channelsSeen->insert(ch);
            int key = (ch<<8) | p;
            ons[key].push_back({ev.tick, v});
        } else if (isNoteOff(ev, ch, p, v)) {
            int key = (ch<<8) | p;
            auto it = ons.find(key);
            if (it != ons.end() && !it->second.empty()) {
                OnInfo on = it->second.back();
                it->second.pop_back();
                int endT = std::max(ev.tick, on.tick + 1); // never zero-length
                notes.push_back({on.tick, endT, p, on.vel, ch});
            }
        }
    }
    std::sort(notes.begin(), notes.end(),
              [](const NoteSpan& a, const NoteSpan& b){
                  if (a.startTick != b.startTick) return a.startTick < b.startTick;
                  return a.pitch > b.pitch;
              });
    return notes;
}

static std::vector<std::vector<NoteSpan>> extractVoicesFromTrack(const MidiFile& in,
                                                                 int trackIndex,
                                                                 bool /*keepOverlaps*/) {
    std::vector<NoteSpan> notes = extractTrackNotes(in, trackIndex, nullptr);

    std::map<int, std::vector<int>> byStart;
    for (int i = 0; i < (int)notes.size(); ++i) byStart[notes[i].startTick].push_back(i);

    std::vector<std::vector<NoteSpan>> voices;
    auto voiceActiveUntil = [&](int vi, int tick)->bool {
        if (voices[vi].empty()) return false;
        return voices[vi].back().endTick > tick;
    };

    for (auto& kv : byStart) {
        int t = kv.first;
        auto idxs = kv.second;
        std::sort(idxs.begin(), idxs.end(), [&](int a, int b){
            return notes[a].pitch > notes[b].pitch;
        });

        std::vector<int> freeLanes;
        for (int vi = 0; vi < (int)voices.size(); ++vi) {
            if (!voiceActiveUntil(vi, t)) freeLanes.push_back(vi);
        }

        for (int id : idxs) {
            if (!freeLanes.empty()) {
                int vi = freeLanes.front();
                freeLanes.erase(freeLanes.begin());
                voices[vi].push_back(notes[id]);
            } else {
                voices.push_back({});
                voices.back().push_back(notes[id]);
            }
        }
    }

    std::vector<std::pair<double,int>> avgPitch;
    for (int vi = 0; vi < (int)voices.size(); ++vi) {
        if (voices[vi].empty()) { avgPitch.push_back({-1e9, vi}); continue; }
        double sum = 0.0; for (auto& n : voices[vi]) sum += n.pitch;
        avgPitch.push_back({ sum / voices[vi].size(), vi });
    }
    std::sort(avgPitch.begin(), avgPitch.end(),
              [](auto& a, auto& b){ return a.first > b.first; });

    std::vector<std::vector<NoteSpan>> ordered;
    ordered.reserve(voices.size());
    for (auto& ap : avgPitch) ordered.push_back(std::move(voices[ap.second]));
    return ordered;
}

static int writeNotesAndReturnLastTick(MidiFile& out, const std::vector<NoteSpan>& notes) {
    int lastTick = 0;
    for (const auto& n : notes) {
        std::vector<unsigned char> on = { (unsigned char)(0x90 | (n.channel & 0x0F)),
                                          (unsigned char)(n.pitch & 0x7F),
                                          (unsigned char)(n.velocity & 0x7F) };
        addMsg(out, 0, n.startTick, on);
        if (n.startTick > lastTick) lastTick = n.startTick;

        std::vector<unsigned char> off = { (unsigned char)(0x80 | (n.channel & 0x0F)),
                                           (unsigned char)(n.pitch & 0x7F),
                                           (unsigned char)0x40 };
        addMsg(out, 0, n.endTick, off);
        if (n.endTick > lastTick) lastTick = n.endTick;
    }
    return lastTick;
}

static void addEndOfTrack(MidiFile& out, int tickHint) {
    int last = tickHint;
    for (int i = 0; i < out[0].getEventCount(); ++i) {
        int t = out[0][i].tick;
        if (t > last) last = t;
    }
    std::vector<unsigned char> eot = { 0xFF, 0x2F, 0x00 };
    addMsg(out, 0, last + 1, eot);
}

// Ensures each track ends with an End-Of-Track meta at or after its last event.
static void ensureEndOfTrack(smf::MidiFile& mf) {
    int tracks = mf.getTrackCount();
    int tpq    = mf.getTPQ();
    for (int t = 0; t < tracks; ++t) {
        long lastAbs = 0;
        bool hasEOT = false;

        // Work in absolute to find last tick & existing EOTs
        mf.absoluteTicks();
        int evcount = mf[t].size();
        for (int i = 0; i < evcount; ++i) {
            const auto& ev = mf[t][i];
            if (ev.tick > lastAbs) lastAbs = ev.tick;
            if (ev.isMeta() && ev.getMetaType() == 0x2F) {
                hasEOT = true;
            }
        }

        if (!hasEOT) {
            // Place EOT one small step after last event (e.g., +1 tick)
            std::vector<unsigned char> msg(3);
            msg[0] = 0xFF; msg[1] = 0x2F; msg[2] = 0x00;
            // addEvent(track, tick, message-bytes)
            mf.addEvent(t, static_cast<int>(lastAbs + 1), msg);
        }
    }
}


static bool writeMidiFile(smf::MidiFile& mf, const std::filesystem::path& p,
                          Logger& log, const char* tag)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec); // ensure folder exists

    // Normalize timing & ordering before writing.
    log.line(std::string("   [") + tag + "] absoluteTicks()");
    mf.absoluteTicks();

    log.line(std::string("   [") + tag + "] sortTracks()");
    mf.sortTracks();

    // Make sure each track ends cleanly.
    log.line(std::string("   [") + tag + "] ensureEndOfTrack()");
    ensureEndOfTrack(mf);

    // Optional but helpful normalization: join then split.
    // (This can resolve odd corner cases in some files.)
    log.line(std::string("   [") + tag + "] joinTracks()");
    mf.joinTracks();

    log.line(std::string("   [") + tag + "] splitTracks()");
    mf.splitTracks();

    // Final conversion to delta before writing.
    log.line(std::string("   [") + tag + "] deltaTicks()");
    mf.deltaTicks();

    // Extra visibility: dump per-track event counts just before write
    int tracks = mf.getTrackCount();
    for (int t = 0; t < tracks; ++t) {
        log.line("   [" + std::string(tag) + "] track " + std::to_string(t) +
                 " events just before write: " + std::to_string((int)mf[t].size()));
    }

    fs::path full = p;
    log.line(std::string("   [") + tag + "] writing: " + full.string());
    bool ok = mf.write(full.string());
    if (!ok) {
        log.line(std::string("   [") + tag + "] ERROR: write() returned false");
    } else {
        log.line(std::string("   [") + tag + "] Wrote: " + full.string());
    }
    return ok;
}



// ------------------------ Drum Split (ch10) ------------------------

static void splitDrumTrack(const MidiFile& in,
                           int trackIndex,
                           const MetaCopy& meta,
                           const fs::path& outDir,
                           const std::string& baseName,
                           Logger& log) {
    std::vector<NoteSpan> notes = extractTrackNotes(in, trackIndex, nullptr);
    log.line("  [Drums] notes: " + std::to_string(notes.size()));
    std::vector<NoteSpan> drums, cymbals;
    for (auto& n : notes) {
        if (n.channel == 9) {
            if (CYMBAL_NOTES.count(n.pitch)) cymbals.push_back(n);
            else drums.push_back(n);
        }
    }
    log.line("   -> drums: " + std::to_string(drums.size()) +
             ", cymbals: " + std::to_string(cymbals.size()));

    auto writeSet = [&](const std::vector<NoteSpan>& set, const std::string& label) {
        if (set.empty()) {
            log.line("   Skip " + label + " (no notes)");
            return;
        }

        MidiFile out;
        out.absoluteTicks();
        out.addTrack(1);
        out.setTicksPerQuarterNote(in.getTicksPerQuarterNote());

        log.line("   [" + label + "] copy global metas: " + std::to_string((int)meta.metas.size()));
        for (auto& m : meta.metas) addMsg(out, 0, m.first, m.second);

        std::set<int> used { 9 };
        std::vector<MidiEvent> chAuto;
        collectChannelSetupAndAutomation(in, trackIndex, used, chAuto);
        int lastTick = 0;
        log.line("   [" + label + "] inject automation: " + std::to_string((int)chAuto.size()));
        for (auto& ev : chAuto) {
            addMsg(out, 0, ev.tick, bytesFromEvent(ev));
            if (ev.tick > lastTick) lastTick = ev.tick;
        }

        int lastNoteTick = writeNotesAndReturnLastTick(out, set);
        log.line("   [" + label + "] lastNoteTick = " + std::to_string(lastNoteTick));
        if (lastNoteTick > lastTick) lastTick = lastNoteTick;

        addEndOfTrack(out, lastTick);
        log.line("   [" + label + "] EOT at ~" + std::to_string(lastTick+1));

        std::string fname = baseName + "-" + label + ".mid";
        writeMidiFile(out, outDir / fname, log, label.c_str());
    };

    writeSet(drums,   "drums");
    writeSet(cymbals, "cymbals");
}

// ------------------------ Voice Split (non-drum) ------------------------

static void splitTrackVoices(const MidiFile& in,
                             int trackIndex,
                             const MetaCopy& meta,
                             const fs::path& outDir,
                             const std::string& baseName,
                             const std::string& instrumentNameSafe,
                             Logger& log) {

    std::set<int> channels;
    auto allNotes = extractTrackNotes(in, trackIndex, &channels);
    log.line("  Notes found: " + std::to_string(allNotes.size()) +
             " | channels used: " + std::to_string(channels.size()));

    auto voices = extractVoicesFromTrack(in, trackIndex, true);
    log.line("  Voices: " + std::to_string(voices.size()));
    if (voices.empty()) {
        log.line("  No voices (skip).");
        return;
    }

    int vnum = 1;
    for (const auto& voice : voices) {
        log.line("   Voice " + std::to_string(vnum) + " notes: " + std::to_string(voice.size()));
        if (voice.empty()) { vnum++; continue; }

        MidiFile out;
        out.absoluteTicks();
        out.addTrack(1);
        out.setTicksPerQuarterNote(in.getTicksPerQuarterNote());

        log.line("   [voice" + std::to_string(vnum) + "] copy global metas: " + std::to_string((int)meta.metas.size()));
        for (auto& m : meta.metas) addMsg(out, 0, m.first, m.second);

        std::vector<MidiEvent> chAuto;
        collectChannelSetupAndAutomation(in, trackIndex, channels, chAuto);
        int lastTick = 0;
        log.line("   [voice" + std::to_string(vnum) + "] inject automation: " + std::to_string((int)chAuto.size()));
        for (auto& ev : chAuto) {
            addMsg(out, 0, ev.tick, bytesFromEvent(ev));
            if (ev.tick > lastTick) lastTick = ev.tick;
        }

        int lastNoteTick = writeNotesAndReturnLastTick(out, voice);
        log.line("   [voice" + std::to_string(vnum) + "] lastNoteTick = " + std::to_string(lastNoteTick));
        if (lastNoteTick > lastTick) lastTick = lastNoteTick;

        addEndOfTrack(out, lastTick);
        log.line("   [voice" + std::to_string(vnum) + "] EOT at ~" + std::to_string(lastTick+1));

        std::string fname = baseName + "-track" + std::to_string(trackIndex) + "-" +
                            instrumentNameSafe + "-voice" + std::to_string(vnum) + ".mid";
        fs::path outPath = outDir / fname;

        if (!writeMidiFile(out, outPath, log, ("voice" + std::to_string(vnum)).c_str())) {
            log.line("   [voice" + std::to_string(vnum) + "] write failed, aborting this track.");
            // continue to next voice rather than abort whole run
        }
        vnum++;
    }
}

// ------------------------ Main ------------------------

int main() {
    std::ios::sync_with_stdio(false);

    std::cout << "Enter full path to a MIDI file (.mid): ";
    std::string inPathStr;
    std::getline(std::cin, inPathStr);
    if (inPathStr.size() >= 2 && inPathStr.front()=='"' && inPathStr.back()=='"') {
        inPathStr = inPathStr.substr(1, inPathStr.size()-2);
    }
    fs::path inPath = fs::path(inPathStr);
    if (!fs::exists(inPath)) {
        std::cerr << "File not found.\n";
        return 1;
    }

    fs::path srcDir  = inPath.parent_path();
    std::string baseName = inPath.stem().string();

    // Logging: try EXE dir, fall back to source dir
    Logger log;
    fs::path logPath = exeDir() / "MIDI_Voice_Separation_Log.txt";
    log.openAt(logPath);
    if (!log.ok) {
        logPath = srcDir / "MIDI_Voice_Separation_Log.txt";
        log.openAt(logPath);
    }
    log.line("=== MIDI Voice Separation ===");
    log.line(std::string("Log: ") + logPath.string());

    // Load MIDI
    MidiFile in;
    if (!in.read(inPath.string())) {
        std::cerr << "Failed to read MIDI.\n";
        log.line("Failed to read MIDI: " + inPath.string());
        return 1;
    }

    // Prepare timing/links/order
    in.absoluteTicks();
    in.doTimeAnalysis();
    in.linkNotePairs();
    in.sortTracks();

    log.line("Input file: " + inPath.string());
    log.line("TicksPerQuarter: " + std::to_string(in.getTicksPerQuarterNote()));
    log.line("Tracks: " + std::to_string(in.getTrackCount()));

    // Scan tracks
    auto infos = scanTrackInfo(in);
    for (auto& ti : infos) {
        std::string inst = ti.hasChannel10
            ? "Percussion (Ch10)"
            : (ti.programGuess >= 0 ? std::string(GM_NAMES[ti.programGuess]) : "Unknown");
        log.line("Track " + std::to_string(ti.trackIndex) + " | events=" + std::to_string(ti.eventCount) +
                 (ti.trackName.empty() ? "" : " | Name: " + ti.trackName) +
                 " | " + inst);
    }

    // Prompt: one or all
    std::cout << "\nSplit a single track or all tracks?\n";
    std::cout << "  1 = Single selected track\n";
    std::cout << "  2 = All tracks (includes drum split)\n";
    std::cout << "Choose 1 or 2: ";
    std::string modeStr; std::getline(std::cin, modeStr);
    int mode = (modeStr == "2") ? 2 : 1;

    // Output folder
    fs::path outDir = (mode == 2) ? (srcDir / (baseName + " - Split chords")) : srcDir;
    {
        std::error_code ec; fs::create_directories(outDir, ec);
    }
    if (mode == 2) {
        log.line("Output folder: " + outDir.string());
    }

    // Global meta
    MetaCopy meta = collectGlobalMeta(in);
    log.line("Global metas copied: " + std::to_string((int)meta.metas.size()));

    // Work
    if (mode == 1) {
        std::cout << "Enter the track number to split: ";
        std::string tstr; std::getline(std::cin, tstr);
        int tsel = 0;
        try { tsel = std::stoi(tstr); } catch(...) { std::cerr << "Invalid track.\n"; return 1; }
        if (tsel < 0 || tsel >= in.getTrackCount()) {
            std::cerr << "Invalid track.\n";
            log.line("Invalid track selected.");
            return 1;
        }
        const auto& ti = infos[tsel];
        log.line("Selected track: " + std::to_string(tsel));

        if (ti.hasChannel10) {
            splitDrumTrack(in, tsel, meta, outDir, baseName + "-track" + std::to_string(tsel), log);
        } else {
            auto noteCheck = extractTrackNotes(in, tsel, nullptr);
            log.line(" Pre-check notes on selected track: " + std::to_string(noteCheck.size()));
            if (noteCheck.empty()) {
                log.line(" Selected track has no notes. Nothing to write.");
            } else {
                std::string inst = (ti.programGuess >= 0) ? filenameSafe(GM_NAMES[ti.programGuess]) : std::string("Instrument");
                splitTrackVoices(in, tsel, meta, outDir, baseName, inst, log);
            }
        }
    } else {
        for (const auto& ti : infos) {
            if (ti.eventCount <= 0) continue;

            log.line("\nProcessing track " + std::to_string(ti.trackIndex) + (ti.hasChannel10 ? " (drums)" : " (inst)") + "...");
            if (ti.hasChannel10) {
                splitDrumTrack(in, ti.trackIndex, meta, outDir, baseName + "-track" + std::to_string(ti.trackIndex), log);
            } else {
                auto noteCheck = extractTrackNotes(in, ti.trackIndex, nullptr);
                log.line("  Pre-check notes: " + std::to_string(noteCheck.size()));
                if (noteCheck.empty()) { log.line("  No notes (skip)."); continue; }
                std::string inst = (ti.programGuess >= 0) ? filenameSafe(GM_NAMES[ti.programGuess]) : std::string("Instrument");
                splitTrackVoices(in, ti.trackIndex, meta, outDir, baseName, inst, log);
            }
        }
    }

    log.line("\nDone.");

#ifdef _WIN32
    fs::path folderToOpen = outDir;
    if (mode == 1) folderToOpen = srcDir;
    std::string cmd = "explorer \"" + folderToOpen.string() + "\"";
    system(cmd.c_str());
#endif

    return 0;
}
