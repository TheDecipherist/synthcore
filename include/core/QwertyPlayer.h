#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>

namespace core {

/** Plays QWERTY notes into the MidiKeyboardState no matter which component in
    the window has keyboard focus — attached as a KeyListener to the editor AND
    any floating window it opens, so "the keyboard always works" while the app is
    focused. tick() releases notes whose keys were let go while another app had
    focus (no stuck notes).

    This is also how the Patch Explorer's arrow keys are routed. They go through
    onOtherKey rather than through giving the explorer keyboard focus, because a
    focused control is exactly what silenced a whole synth once: a BPM field took
    the focus and the on-screen keyboard stopped answering. Nothing in the shell
    takes focus; the window keeps it and hands keys down. */
class QwertyPlayer : public juce::KeyListener
{
public:
    /** `baseNote` is the note the leftmost QWERTY key plays: C4 (60) on a synth,
        but 36 on a drum machine, whose map is 35..51 and which silently rejects
        every note outside it. With the synth's base the whole computer keyboard
        goes quiet and nothing reports why. */
    explicit QwertyPlayer (juce::MidiKeyboardState& s, int baseNote = 60)
        : state (s), base (baseNote) {}

    /** Editor shortcuts (arrows for the explorer or a menu, unit-specific keys)
        routed through here so they work whichever component is the key target. */
    std::function<bool (const juce::KeyPress&)> onOtherKey;

    /** True while a text field owns the keyboard (patch search, rename): the
        QWERTY player must not turn typing into notes. */
    std::function<bool()> suppressed;

    /** Physical key state, independent of which view has focus. Defaults to
        JUCE's own tracking; a unit whose panel is a web view installs an
        OS-level poll here (CGEventSourceKeyState on macOS), because JUCE never
        sees the key at all when the web view has it. */
    std::function<bool (int)> isDown = [] (int key) { return juce::KeyPress::isKeyCurrentlyDown (key); };

    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        if (suppressed != nullptr && suppressed())
            return false;                      // let the text editor have the key
        scan();
        if (isNoteKey (k))
            return true;
        if (onOtherKey != nullptr && onOtherKey (k))
            return true;
        // Swallow every other plain key: an unhandled keyDown on macOS falls
        // through to Cocoa, which plays the system alert sound. Cmd/Ctrl
        // combinations pass through so the app menu still gets them.
        return ! (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown());
    }
    bool keyStateChanged (bool, juce::Component*) override
    {
        if (suppressed == nullptr || ! suppressed())
            scan();
        return false;
    }
    /** A key event that arrived some other way than through the window — a web
        view panel, a host that swallows keys. */
    void externalKey (int keyChar, bool down)
    {
        const auto c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keyChar);
        for (size_t i = 0; i < std::size (kMap); ++i)
            if (kMap[i].key == (int) c)
            {
                if (held[i] == down) return;
                held[i] = down;
                if (down) state.noteOn (1, base + kMap[i].semi, 0.79f);
                else      state.noteOff (1, base + kMap[i].semi, 0.0f);
                return;
            }
    }
    void releaseAllKeys() { releaseAll(); }
    void tick()                            // from the editor timer
    {
        // isKeyCurrentlyDown polls GLOBAL key state — only play while this
        // app is foreground, and release everything when focus leaves.
        if (juce::Process::isForegroundProcess())
            scan();
        else
            releaseAll();
    }

private:
    void releaseAll()
    {
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i])
            {
                held[i] = false;
                state.noteOff (1, base + kMap[i].semi, 0.0f);
            }
    }
    // JUCE's classic mapping: A row = white keys from C, W row = blacks.
    static constexpr struct { int key; int semi; } kMap[] = {
        { 'A', 0 },  { 'W', 1 },  { 'S', 2 },  { 'E', 3 },  { 'D', 4 },
        { 'F', 5 },  { 'T', 6 },  { 'G', 7 },  { 'Y', 8 },  { 'H', 9 },
        { 'U', 10 }, { 'J', 11 }, { 'K', 12 }, { 'O', 13 }, { 'L', 14 },
        { 'P', 15 },
    };
    static bool isNoteKey (const juce::KeyPress& k)
    {
        if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown())
            return false;
        const auto c = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
        for (const auto& m : kMap)
            if (m.key == (int) c)
                return true;
        return false;
    }
    void scan()
    {
        if (! juce::Process::isForegroundProcess())
            return;
        for (size_t i = 0; i < std::size (kMap); ++i)
        {
            const bool down = isDown (kMap[i].key);
            if (down == held[i])
                continue;
            held[i] = down;
            const int note = base + kMap[i].semi;
            if (down)
                state.noteOn (1, note, 0.79f);
            else
                state.noteOff (1, note, 0.0f);
        }
    }
    juce::MidiKeyboardState& state;
    const int base;
    std::array<bool, 16> held {};
};

} // namespace core
