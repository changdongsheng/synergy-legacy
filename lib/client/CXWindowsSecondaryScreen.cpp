#include "CXWindowsSecondaryScreen.h"
#include "CXWindowsClipboard.h"
#include "CXWindowsScreen.h"
#include "CXWindowsScreenSaver.h"
#include "CXWindowsUtil.h"
#include "IScreenReceiver.h"
#include "XScreen.h"
#include "CThread.h"
#include "CLog.h"
#if defined(X_DISPLAY_MISSING)
#	error X11 is required to build synergy
#else
#	include <X11/X.h>
#	include <X11/Xutil.h>
#	define XK_MISCELLANY
#	define XK_XKB_KEYS
#	include <X11/keysymdef.h>
#	if defined(HAVE_X11_EXTENSIONS_XTEST_H)
#		include <X11/extensions/XTest.h>
#	else
#		error The XTest extension is required to build synergy
#	endif
#endif

//
// CXWindowsSecondaryScreen
//

CXWindowsSecondaryScreen::CXWindowsSecondaryScreen(IScreenReceiver* receiver) :
	CSecondaryScreen(),
	m_window(None)
{
	m_screen = new CXWindowsScreen(receiver, this);
}

CXWindowsSecondaryScreen::~CXWindowsSecondaryScreen()
{
	assert(m_window == None);
	delete m_screen;
}

void
CXWindowsSecondaryScreen::keyDown(KeyID key, KeyModifierMask mask)
{
	Keystrokes keys;
	KeyCode keycode;

	// get the sequence of keys to simulate key press and the final
	// modifier state.
	m_mask = mapKey(keys, keycode, key, mask, kPress);
	if (keys.empty()) {
		return;
	}

	// generate key events
	doKeystrokes(keys, 1);

	// note that key is now down
	m_keys[keycode] = true;
}

void
CXWindowsSecondaryScreen::keyRepeat(KeyID key,
				KeyModifierMask mask, SInt32 count)
{
	Keystrokes keys;
	KeyCode keycode;

	// get the sequence of keys to simulate key repeat and the final
	// modifier state.
	m_mask = mapKey(keys, keycode, key, mask, kRepeat);
	if (keys.empty()) {
		return;
	}

	// generate key events
	doKeystrokes(keys, count);
}

void
CXWindowsSecondaryScreen::keyUp(KeyID key, KeyModifierMask mask)
{
	Keystrokes keys;
	KeyCode keycode;

	// get the sequence of keys to simulate key release and the final
	// modifier state.
	m_mask = mapKey(keys, keycode, key, mask, kRelease);
	if (keys.empty()) {
		return;
	}

	// generate key events
	doKeystrokes(keys, 1);

	// note that key is now up
	m_keys[keycode] = false;
}

void
CXWindowsSecondaryScreen::mouseDown(ButtonID button)
{
	CDisplayLock display(m_screen);
	XTestFakeButtonEvent(display, mapButton(button), True, CurrentTime);
	XSync(display, False);
}

void
CXWindowsSecondaryScreen::mouseUp(ButtonID button)
{
	CDisplayLock display(m_screen);
	XTestFakeButtonEvent(display, mapButton(button), False, CurrentTime);
	XSync(display, False);
}

void
CXWindowsSecondaryScreen::mouseMove(SInt32 x, SInt32 y)
{
	warpCursor(x, y);
}

void
CXWindowsSecondaryScreen::mouseWheel(SInt32 delta)
{
	// choose button depending on rotation direction
	const unsigned int button = (delta >= 0) ? 4 : 5;

	// now use absolute value of delta
	if (delta < 0) {
		delta = -delta;
	}

	// send as many clicks as necessary
	CDisplayLock display(m_screen);
	for (; delta >= 120; delta -= 120) {
		XTestFakeButtonEvent(display, button, True, CurrentTime);
		XTestFakeButtonEvent(display, button, False, CurrentTime);
	}
	XSync(display, False);
}

IScreen*
CXWindowsSecondaryScreen::getScreen() const
{
	return m_screen;
}

void
CXWindowsSecondaryScreen::onScreensaver(bool)
{
	// ignore
}

bool
CXWindowsSecondaryScreen::onPreDispatch(const CEvent*)
{
	return false;
}

bool
CXWindowsSecondaryScreen::onEvent(CEvent* event)
{
	assert(event != NULL);
	XEvent& xevent = event->m_event;

	// handle event
	switch (xevent.type) {
	case MappingNotify:
		// keyboard mapping changed
		updateKeys();
		return true;

	case LeaveNotify:
		// mouse moved out of hider window somehow.  hide the window.
		hideWindow();
		return true;
	}
}

SInt32
CXWindowsSecondaryScreen::getJumpZoneSize() const
{
	return 0;
}

void
CXWindowsSecondaryScreen::onPreMainLoop()
{
	assert(m_window != None);
}

void
CXWindowsSecondaryScreen::onPreOpen()
{
	assert(m_window == None);
}

void
CXWindowsSecondaryScreen::onPostOpen()
{
	// check for peculiarities
	// FIXME -- may have to get these from some database
	m_numLockHalfDuplex  = false;
	m_capsLockHalfDuplex = false;
//	m_numLockHalfDuplex  = true;
//	m_capsLockHalfDuplex = true;

}

void
CXWindowsSecondaryScreen::onPreEnter()
{
	assert(m_window != None);
}

void
CXWindowsSecondaryScreen::onPreLeave()
{
	assert(m_window != None);
}

void
CXWindowsSecondaryScreen::createWindow()
{
	{
		CDisplayLock display(m_screen);

		// verify the availability of the XTest extension
		int majorOpcode, firstEvent, firstError;
		if (!XQueryExtension(display, XTestExtensionName,
								&majorOpcode, &firstEvent, &firstError)) {
			// FIXME -- subclass exception for more info?
			throw XScreenOpenFailure();
		}

		// cursor hider window attributes.  this window is used to hide the
		// cursor when it's not on the screen.  the window is hidden as soon
		// as the cursor enters the screen or the display's real cursor is
		// moved.
		XSetWindowAttributes attr;
		attr.event_mask            = LeaveWindowMask;
		attr.do_not_propagate_mask = 0;
		attr.override_redirect     = True;
		attr.cursor                = m_screen->getBlankCursor();

		// create the cursor hider window
		m_window = XCreateWindow(display, m_screen->getRoot(),
								0, 0, 1, 1, 0, 0,
								InputOnly, CopyFromParent,
								CWDontPropagate | CWEventMask |
								CWOverrideRedirect | CWCursor,
								&attr);
		if (m_window == None) {
			throw XScreenOpenFailure();
		}
		log((CLOG_DEBUG "window is 0x%08x", m_window));

		// become impervious to server grabs
		XTestGrabControl(display, True);
	}

	// tell generic screen about the window
	m_screen->setWindow(m_window);
}

void
CXWindowsSecondaryScreen::destroyWindow()
{
	{
		CDisplayLock display(m_screen);
		if (display != NULL) {
			// release keys that are still pressed
			releaseKeys(display);

			// no longer impervious to server grabs
			XTestGrabControl(display, False);

			// update
			XSync(display, False);
		}
	}

	// destroy window
	if (m_window != None) {
		m_screen->setWindow(None);
		CDisplayLock display(m_screen);
		if (display != NULL) {
			XDestroyWindow(display, m_window);
		}
		m_window = None;
	}
}

void
CXWindowsSecondaryScreen::showWindow()
{
	// move hider window under the mouse (rather than moving the mouse
	// somewhere else on the screen)
	SInt32 x, y;
	getCursorPos(x, y);
	CDisplayLock display(m_screen);
	XMoveWindow(display, m_window, x, y);

	// raise and show the hider window.  take activation.
	// FIXME -- take focus?
	XMapRaised(display, m_window);

/* XXX -- this should have no effect
	// hide cursor by moving it into the hider window
	XWarpPointer(display, None, m_window, 0, 0, 0, 0, 0, 0);
*/
}

void
CXWindowsSecondaryScreen::hideWindow()
{
	assert(m_window != None);

	CDisplayLock display(m_screen);
	XUnmapWindow(display, m_window);
}

void
CXWindowsSecondaryScreen::warpCursor(SInt32 x, SInt32 y)
{
	CDisplayLock display(m_screen);
	Display* pDisplay = display;
	XTestFakeMotionEvent(display, DefaultScreen(pDisplay), x, y, CurrentTime);
	XSync(display, False);
}

void
CXWindowsSecondaryScreen::setToggleState(KeyModifierMask mask)
{
	CDisplayLock display(m_screen);

	// toggle modifiers that don't match the desired state
	unsigned int xMask = maskToX(mask);
	if ((xMask & m_capsLockMask)   != (m_mask & m_capsLockMask)) {
		toggleKey(display, XK_Caps_Lock, m_capsLockMask);
	}
	if ((xMask & m_numLockMask)    != (m_mask & m_numLockMask)) {
		toggleKey(display, XK_Num_Lock, m_numLockMask);
	}
	if ((xMask & m_scrollLockMask) != (m_mask & m_scrollLockMask)) {
		toggleKey(display, XK_Scroll_Lock, m_scrollLockMask);
	}
}

unsigned int
CXWindowsSecondaryScreen::mapButton(ButtonID id) const
{
	// FIXME -- should use button mapping?
	return static_cast<unsigned int>(id);
}

KeyModifierMask
CXWindowsSecondaryScreen::mapKey(Keystrokes& keys, KeyCode& keycode,
				KeyID id, KeyModifierMask mask, EKeyAction action) const
{
	// note -- must have display locked on entry

	// the system translates key events into characters depending
	// on the modifier key state at the time of the event.  to
	// generate the right keysym we need to set the modifier key
	// states appropriately.
	//
	// the mask passed by the caller is the desired mask.  however,
	// there may not be a keycode mapping to generate the desired
	// keysym with that mask.  we override the bits in the mask
	// that cannot be accomodated.

	// note if the key is the caps lock and it's "half-duplex"
	const bool isHalfDuplex = ((id == kKeyCapsLock && m_capsLockHalfDuplex) ||
								(id == kKeyNumLock && m_numLockHalfDuplex));

	// ignore releases and repeats for half-duplex keys
	if (isHalfDuplex && action != kPress) {
		return m_mask;
	}

	// lookup the a keycode for this key id.  also return the
	// key modifier mask required.
	unsigned int outMask;
	if (!findKeyCode(keycode, outMask, id, maskToX(mask))) {
		// we cannot generate the desired keysym because no key
		// maps to that keysym.  just return the current mask.
		log((CLOG_DEBUG2 "no keycode for KeyID %d modifiers 0x%04x", id, mask));
		return m_mask;
	}
	log((CLOG_DEBUG2 "keysym %d -> KeyID %d modifiers 0x%04x", id, keycode, outMask));

	// if we cannot match the modifier mask then don't return any
	// keys and just return the current mask.
	if ((outMask & m_modifierMask) != outMask) {
		log((CLOG_DEBUG2 "cannot match modifiers to mask 0x%04x", m_modifierMask));
		return m_mask;
	}

	// note if the key is a modifier
	ModifierMap::const_iterator index = m_keycodeToModifier.find(keycode);
	const bool isModifier = (index != m_keycodeToModifier.end());

	// add the key events required to get to the modifier state
	// necessary to generate an event yielding id.  also save the
	// key events required to restore the state.  if the key is
	// a modifier key then skip this because modifiers should not
	// modify modifiers.
	Keystrokes undo;
	Keystroke keystroke;
	if (outMask != m_mask && !isModifier) {
		for (unsigned int i = 0; i < 8; ++i) {
			unsigned int bit = (1 << i);
			if ((outMask & bit) != (m_mask & bit)) {
				// get list of keycodes for the modifier.  if there isn't
				// one then there's no key mapped to this modifier and we
				// can't generate the desired key so bail.
				const KeyCode* modifierKeys =
								&m_modifierToKeycode[i * m_keysPerModifier];
				KeyCode modifierKey = modifierKeys[0];
				if (modifierKey == 0) {
					modifierKey = modifierKeys[1];
				}
				if (modifierKey == 0) {
					log((CLOG_DEBUG1 "no key mapped to modifier 0x%04x", bit));
					return m_mask;
				}

				keystroke.m_keycode = modifierKey;
				keystroke.m_repeat  = false;
				if ((outMask & bit) != 0) {
					// modifier is not active but should be.  if the
					// modifier is a toggle then toggle it on with a
					// press/release, otherwise activate it with a
					// press.  use the first keycode for the modifier.
					log((CLOG_DEBUG2 "modifier 0x%04x is not active", bit));
					if ((bit & m_toggleModifierMask) != 0) {
						log((CLOG_DEBUG2 "modifier 0x%04x is a toggle", bit));
						if ((bit == m_capsLockMask && m_capsLockHalfDuplex) ||
							(bit == m_numLockMask && m_numLockHalfDuplex)) {
							keystroke.m_press = True;
							keys.push_back(keystroke);
							keystroke.m_press = False;
							undo.push_back(keystroke);
						}
						else {
							keystroke.m_press = True;
							keys.push_back(keystroke);
							keystroke.m_press = False;
							keys.push_back(keystroke);
							undo.push_back(keystroke);
							keystroke.m_press = True;
							undo.push_back(keystroke);
						}
					}
					else {
						keystroke.m_press = True;
						keys.push_back(keystroke);
						keystroke.m_press = False;
						undo.push_back(keystroke);
					}
				}

				else {
					// modifier is active but should not be.  if the
					// modifier is a toggle then toggle it off with a
					// press/release, otherwise deactivate it with a
					// release.  we must check each keycode for the
					// modifier if not a toggle.
					log((CLOG_DEBUG2 "modifier 0x%04x is active", bit));
					if ((bit & m_toggleModifierMask) != 0) {
						log((CLOG_DEBUG2 "modifier 0x%04x is a toggle", bit));
						if ((bit == m_capsLockMask && m_capsLockHalfDuplex) ||
							(bit == m_numLockMask && m_numLockHalfDuplex)) {
							keystroke.m_press = False;
							keys.push_back(keystroke);
							keystroke.m_press = True;
							undo.push_back(keystroke);
						}
						else {
							keystroke.m_press = True;
							keys.push_back(keystroke);
							keystroke.m_press = False;
							keys.push_back(keystroke);
							undo.push_back(keystroke);
							keystroke.m_press = True;
							undo.push_back(keystroke);
						}
					}
					else {
						for (unsigned int j = 0; j < m_keysPerModifier; ++j) {
							const KeyCode key = modifierKeys[j];
							if (key != 0 && m_keys[key]) {
								keystroke.m_keycode = key;
								keystroke.m_press   = False;
								keys.push_back(keystroke);
								keystroke.m_press   = True;
								undo.push_back(keystroke);
							}
						}
					}
				}
			}
		}
	}

	// note if the press of a half-duplex key should be treated as a release
	if (isHalfDuplex && (m_mask & (1 << index->second)) != 0) {
		action = kRelease;
	}

	// add the key event
	keystroke.m_keycode = keycode;
	switch (action) {
	case kPress:
		keystroke.m_press  = True;
		keystroke.m_repeat = false;
		keys.push_back(keystroke);
		break;

	case kRelease:
		keystroke.m_press  = False;
		keystroke.m_repeat = false;
		keys.push_back(keystroke);
		break;

	case kRepeat:
		keystroke.m_press  = False;
		keystroke.m_repeat = true;
		keys.push_back(keystroke);
		keystroke.m_press  = True;
		keys.push_back(keystroke);
		break;
	}

	// add key events to restore the modifier state.  apply events in
	// the reverse order that they're stored in undo.
	while (!undo.empty()) {
		keys.push_back(undo.back());
		undo.pop_back();
	}

	// if the key is a modifier key then compute the modifier map after
	// this key is pressed or released.  if repeating then ignore.
	mask = m_mask;
	if (isModifier && action != kRepeat) {
		// get modifier
		const unsigned int modifierBit = (1 << index->second);

		// toggle keys modify the state on release.  other keys set the
		// bit on press and clear the bit on release.  if half-duplex
		// then toggle each time we get here.
		if ((modifierBit & m_toggleModifierMask) != 0) {
			if (isHalfDuplex || action == kRelease) {
				mask ^= modifierBit;
			}
		}
		else if (action == kPress) {
			mask |= modifierBit;
		}
		else {
			// can't reset bit until all keys that set it are released.
			// scan those keys to see if any (except keycode) are pressed.
			bool down = false;
			const KeyCode* modifierKeys = &m_modifierToKeycode[
											index->second * m_keysPerModifier];
			for (unsigned int j = 0; !down && j < m_keysPerModifier; ++j) {
				if (modifierKeys[j] != 0 && m_keys[modifierKeys[j]])
					down = true;
			}
			if (!down)
				mask &= ~modifierBit;
		}
	}

	return mask;
}

bool
CXWindowsSecondaryScreen::findKeyCode(KeyCode& keycode,
				unsigned int& maskOut, KeyID id, unsigned int maskIn) const
{
	// convert id to keysym
	KeySym keysym = 0;
	switch (id & 0xffffff00) {
	case 0x0000:
		// Latin-1
		keysym = static_cast<KeySym>(id);
		break;

	case 0xee00:
		// ISO 9995 Function and Modifier Keys
		if (id == kKeyLeftTab) {
			keysym = XK_ISO_Left_Tab;
		}
		break;

	case 0xef00:
		// MISCELLANY
		keysym = static_cast<KeySym>(id - 0xef00 + 0xff00);
		break;
	}

	// fail if unknown key
	if (keysym == 0) {
		return false;
	}

	// if kKeyTab is requested with shift active then try XK_ISO_Left_Tab
	// instead.  if that doesn't work, we'll fall back to XK_Tab with
	// shift active.  this is to handle primary screens that don't map
	// XK_ISO_Left_Tab sending events to secondary screens that do.
	if (keysym == XK_Tab && (maskIn & ShiftMask) != 0) {
		keysym  = XK_ISO_Left_Tab;
		maskIn &= ~ShiftMask;
	}

	// find a keycode to generate id.  XKeysymToKeycode() almost does
	// what we need but won't tell us which index to use with the
	// keycode.  return false if there's no keycode to generate id.
	KeyCodeMap::const_iterator index = m_keycodeMap.find(keysym);
	if (index == m_keycodeMap.end()) {
		// try backup keysym for certain keys (particularly the numpad
		// keys since most laptops don't have a separate numpad and the
		// numpad overlaying the main keyboard may not have movement
		// key bindings).
		switch (keysym) {
		case XK_KP_Home:
			keysym = XK_Home;
			break;

		case XK_KP_Left:
			keysym = XK_Left;
			break;

		case XK_KP_Up:
			keysym = XK_Up;
			break;

		case XK_KP_Right:
			keysym = XK_Right;
			break;

		case XK_KP_Down:
			keysym = XK_Down;
			break;

		case XK_KP_Prior:
			keysym = XK_Prior;
			break;

		case XK_KP_Next:
			keysym = XK_Next;
			break;

		case XK_KP_End:
			keysym = XK_End;
			break;

		case XK_KP_Insert:
			keysym = XK_Insert;
			break;

		case XK_KP_Delete:
			keysym = XK_Delete;
			break;

		case XK_ISO_Left_Tab:
			keysym  = XK_Tab;
			maskIn |= ShiftMask;
			break;

		default:
			return false;
		}

		index = m_keycodeMap.find(keysym);
		if (index == m_keycodeMap.end()) {
			return false;
		}
	}

	// save the keycode
	keycode = index->second.m_keycode;

	// compute output mask.  that's the set of modifiers that need to
	// be enabled when the keycode event is encountered in order to
	// generate the keysym and match maskIn.  it's possible that
	// maskIn wants, say, a shift key to be down but that would make
	// it impossible to generate the keysym.  in that case we must
	// override maskIn.  this is complicated by caps/shift-lock and
	// num-lock.
	maskOut = (maskIn & ~index->second.m_keyMaskMask);
	log((CLOG_DEBUG2 "maskIn(0x%04x) & ~maskMask(0x%04x) -> 0x%04x", maskIn, index->second.m_keyMaskMask, maskOut));
	if (IsKeypadKey(keysym) || IsPrivateKeypadKey(keysym)) {
		if ((m_mask & m_numLockMask) != 0) {
			maskOut &= ~index->second.m_keyMask;
			maskOut |= m_numLockMask;
			log((CLOG_DEBUG2 "keypad key: & ~mask(0x%04x) | numLockMask(0x%04x) -> 0x%04x", index->second.m_keyMask, m_numLockMask, maskOut));
		}
		else {
			maskOut |= index->second.m_keyMask;
			maskOut &= ~m_numLockMask;
			log((CLOG_DEBUG2 "keypad key: | mask(0x%04x) & ~numLockMask(0x%04x) -> 0x%04x", index->second.m_keyMask, m_numLockMask, maskOut));
		}
	}
	else {
		unsigned int maskShift = (index->second.m_keyMask & ShiftMask);
		log((CLOG_DEBUG2 "maskShift = 0x%04x", maskShift));
		if (maskShift != 0 && (m_mask & m_capsLockMask) != 0) {
			// shift and capsLock cancel out for keysyms subject to
			// case conversion but not for keys with shifted
			// characters that are not case conversions.  see if
			// case conversion is necessary.
			KeySym lKey, uKey;
			XConvertCase(keysym, &lKey, &uKey);
			if (lKey != uKey) {
				log((CLOG_DEBUG2 "case convertable, shift && capsLock -> caps lock"));
				maskShift = m_capsLockMask;
			}
			else {
				log((CLOG_DEBUG2 "case unconvertable, shift && capsLock -> shift, caps lock"));
				maskShift |= m_capsLockMask;
			}
		}
		log((CLOG_DEBUG2 "maskShift = 0x%04x", maskShift));
		maskOut |= maskShift;
		maskOut |= (index->second.m_keyMask & ~(ShiftMask | LockMask));
		log((CLOG_DEBUG2 "| maskShift(0x%04x) | other (0x%04x) -> 0x%04x", maskShift, (index->second.m_keyMask & ~(ShiftMask | LockMask)), maskOut));
	}

	return true;
}

void
CXWindowsSecondaryScreen::doKeystrokes(const Keystrokes& keys, SInt32 count)
{
	// do nothing if no keys or no repeats
	if (count < 1 || keys.empty()) {
		return;
	}

	// lock display
	CDisplayLock display(m_screen);

	// generate key events
	for (Keystrokes::const_iterator k = keys.begin(); k != keys.end(); ) {
		if (k->m_repeat) {
			// repeat from here up to but not including the next key
			// with m_repeat == false count times.
			Keystrokes::const_iterator start = k;
			for (; count > 0; --count) {
				// send repeating events
				for (k = start; k != keys.end() && k->m_repeat; ++k) {
					XTestFakeKeyEvent(display,
								k->m_keycode, k->m_press, CurrentTime);
				}
			}

			// note -- k is now on the first non-repeat key after the
			// repeat keys, exactly where we'd like to continue from.
		}
		else {
			// send event
			XTestFakeKeyEvent(display, k->m_keycode, k->m_press, CurrentTime);

			// next key
			++k;
		}
	}

	// update
	XSync(display, False);
}

unsigned int
CXWindowsSecondaryScreen::maskToX(KeyModifierMask inMask) const
{
	// FIXME -- should be configurable.  also not using Mod3Mask.
	unsigned int outMask = 0;
	if (inMask & KeyModifierShift) {
		outMask |= ShiftMask;
	}
	if (inMask & KeyModifierControl) {
		outMask |= ControlMask;
	}
	if (inMask & KeyModifierAlt) {
		outMask |= Mod1Mask;
	}
	if (inMask & KeyModifierMeta) {
		outMask |= Mod4Mask;
	}
	if (inMask & KeyModifierCapsLock) {
		outMask |= m_capsLockMask;
	}
	if (inMask & KeyModifierNumLock) {
		outMask |= m_numLockMask;
	}
	if (inMask & KeyModifierScrollLock) {
		outMask |= m_scrollLockMask;
	}
	return outMask;
}

void
CXWindowsSecondaryScreen::releaseKeys(Display* display)
{
	assert(display != NULL);

	// key up for each key that's down
	for (UInt32 i = 0; i < 256; ++i) {
		if (m_keys[i]) {
			XTestFakeKeyEvent(display, i, False, CurrentTime);
			m_keys[i] = false;
		}
	}
}

void
CXWindowsSecondaryScreen::updateKeys()
{
	CDisplayLock display(m_screen);

	// ask server which keys are pressed
	char keys[32];
	XQueryKeymap(display, keys);

	// transfer to our state
	for (UInt32 i = 0, j = 0; i < 32; j += 8, ++i) {
		m_keys[j + 0] = ((keys[i] & 0x01) != 0);
		m_keys[j + 1] = ((keys[i] & 0x02) != 0);
		m_keys[j + 2] = ((keys[i] & 0x04) != 0);
		m_keys[j + 3] = ((keys[i] & 0x08) != 0);
		m_keys[j + 4] = ((keys[i] & 0x10) != 0);
		m_keys[j + 5] = ((keys[i] & 0x20) != 0);
		m_keys[j + 6] = ((keys[i] & 0x40) != 0);
		m_keys[j + 7] = ((keys[i] & 0x80) != 0);
	}

	// update mappings and current modifiers
	updateKeycodeMap(display);
	updateModifierMap(display);
	updateModifiers(display);
}

void
CXWindowsSecondaryScreen::updateModifiers(Display* display)
{
	// query the pointer to get the keyboard state
	Window root, window;
	int xRoot, yRoot, xWindow, yWindow;
	unsigned int state;
	if (!XQueryPointer(display, m_window, &root, &window,
								&xRoot, &yRoot, &xWindow, &yWindow, &state)) {
		state = 0;
	}

	// update active modifier mask
	m_mask = 0;
	for (unsigned int i = 0; i < 8; ++i) {
		const unsigned int bit = (1 << i);
		if ((bit & m_toggleModifierMask) == 0) {
			for (unsigned int j = 0; j < m_keysPerModifier; ++j) {
				if (m_keys[m_modifierToKeycode[i * m_keysPerModifier + j]])
					m_mask |= bit;
			}
		}
		else if ((bit & state) != 0) {
			// toggle is on
			m_mask |= bit;
		}
	}
}

void
CXWindowsSecondaryScreen::updateKeycodeMap(Display* display)
{
	// get the number of keycodes
	int minKeycode, maxKeycode;
	XDisplayKeycodes(display, &minKeycode, &maxKeycode);
	const int numKeycodes = maxKeycode - minKeycode + 1;

	// get the keyboard mapping for all keys
	int keysymsPerKeycode;
	KeySym* keysyms = XGetKeyboardMapping(display,
								minKeycode, numKeycodes,
								&keysymsPerKeycode);

	// restrict keysyms per keycode to 2 because, frankly, i have no
	// idea how/what modifiers are used to access keysyms beyond the
	// first 2.
	int numKeysyms = 2;	// keysymsPerKeycode

	// initialize
	KeyCodeMask entry;
	m_keycodeMap.clear();

	// insert keys
	for (int i = 0; i < numKeycodes; ++i) {
		// how many keysyms for this keycode?
		int n;
		for (n = 0; n < numKeysyms; ++n) {
			if (keysyms[i * keysymsPerKeycode + n] == NoSymbol) {
				break;
			}
		}

		// move to next keycode if there are no keysyms
		if (n == 0) {
			continue;
		}

		// set the mask of modifiers that this keycode uses
		entry.m_keyMaskMask = (n == 1) ? 0 : (ShiftMask | LockMask);

		// add entries for this keycode
		entry.m_keycode = static_cast<KeyCode>(minKeycode + i);
		for (int j = 0; j < numKeysyms; ++j) {
			entry.m_keyMask = (j == 0) ? 0 : ShiftMask;
			m_keycodeMap.insert(std::make_pair(keysyms[i *
									keysymsPerKeycode + j], entry));
		}
	}

	// clean up
	XFree(keysyms);
}

void
CXWindowsSecondaryScreen::updateModifierMap(Display* display)
{
	// get modifier map from server
	XModifierKeymap* keymap = XGetModifierMapping(display);

	// initialize
	m_modifierMask       = 0;
	m_toggleModifierMask = 0;
	m_numLockMask        = 0;
	m_capsLockMask       = 0;
	m_scrollLockMask     = 0;
	m_keysPerModifier    = keymap->max_keypermod;
	m_modifierToKeycode.clear();
	m_modifierToKeycode.resize(8 * m_keysPerModifier);

	// set keycodes and masks
	for (unsigned int i = 0; i < 8; ++i) {
		const unsigned int bit = (1 << i);
		for (unsigned int j = 0; j < m_keysPerModifier; ++j) {
			KeyCode keycode = keymap->modifiermap[i * m_keysPerModifier + j];

			// save in modifier to keycode
			m_modifierToKeycode[i * m_keysPerModifier + j] = keycode;

			// save in keycode to modifier
			m_keycodeToModifier.insert(std::make_pair(keycode, i));

			// modifier is enabled if keycode isn't 0
			if (keycode != 0) {
				m_modifierMask |= bit;
			}

			// modifier is a toggle if the keysym is a toggle modifier
			const KeySym keysym = XKeycodeToKeysym(display, keycode, 0);
			if (isToggleKeysym(keysym)) {
				m_toggleModifierMask |= bit;

				// note num/caps-lock
				if (keysym == XK_Num_Lock) {
					m_numLockMask |= bit;
				}
				else if (keysym == XK_Caps_Lock) {
					m_capsLockMask |= bit;
				}
				else if (keysym == XK_Scroll_Lock) {
					m_scrollLockMask |= bit;
				}
			}
		}
	}

	XFreeModifiermap(keymap);
}

void
CXWindowsSecondaryScreen::toggleKey(Display* display,
				KeySym keysym, unsigned int mask)
{
	// lookup the keycode
	KeyCodeMap::const_iterator index = m_keycodeMap.find(keysym);
	if (index == m_keycodeMap.end()) {
		return;
	}
	KeyCode keycode = index->second.m_keycode;

	// toggle the key
	if ((keysym == XK_Caps_Lock && m_capsLockHalfDuplex) ||
		(keysym == XK_Num_Lock && m_numLockHalfDuplex)) {
		// "half-duplex" toggle
		XTestFakeKeyEvent(display, keycode, (m_mask & mask) == 0, CurrentTime);
	}
	else {
		// normal toggle
		XTestFakeKeyEvent(display, keycode, True,  CurrentTime);
		XTestFakeKeyEvent(display, keycode, False, CurrentTime);
	}

	// toggle shadow state
	m_mask ^= mask;
}

bool
CXWindowsSecondaryScreen::isToggleKeysym(KeySym key)
{
	switch (key) {
	case XK_Caps_Lock:
	case XK_Shift_Lock:
	case XK_Num_Lock:
	case XK_Scroll_Lock:
		return true;

	default:
		return false;
	}
}