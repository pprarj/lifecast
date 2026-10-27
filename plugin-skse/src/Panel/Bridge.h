// Bridge.h
// Lifecast
//
// The message channel between the panel page and this DLL, in both
// directions. Every section of the panel talks through here, so the shape of
// a message is fixed once and not per feature:
//
//     { "v": <protocol version>, "type": "<name>", "data": <any JSON> }
//
// Page to DLL: the page calls window.Lifecast.send(message) - a fire-and-forget
// binding. It returns undefined at once; the page never waits on the DLL.
//
// DLL to page: Send() below pushes the same shape through the panel's backend
// (Panel/Backend.h). The page subscribes once with
// window.Lifecast.on("message", fn) and receives the message as JSON text.
// On Meridian window.Lifecast is the framework's own binding object; on Prisma
// the page builds it (panel/bridge.js) over two globals, so the rest of the
// page is the same on both.
//
// Why fire-and-forget both ways and no promise natives: a request that
// expects an answer becomes two messages, and the page stays responsive
// while the second one is on its way. Nothing on either side can block on
// the other.
//
// Every message that crosses is logged, raw, before it is interpreted. When
// a future section misbehaves, the log is where "what did the page actually
// send" gets answered.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Bridge
{
	// Bumped when the message shape or the meaning of an existing type
	// changes. The page carries its own copy and says which one it speaks in
	// its "hello"; the DLL and the page ship together, but a player can
	// update one without the other.
	inline constexpr std::uint32_t kProtocolVersion = 1;

	// Handles one message type from the page. Runs on the game thread: every
	// backend hands page messages over from the game thread, so a handler may
	// touch Skyrim state directly.
	using Handler = void (*)(const nlohmann::json& a_data);

	// Registers the handler for a message type. A second registration for
	// the same type replaces the first.
	void On(std::string_view a_type, Handler a_handler);

	// Pushes a message to the page. Dropped, with a log line, when there is
	// no page to push to. Every message is logged as sent; long ones are
	// cut, unless a_logWhole - for messages whose logged text is meant to be
	// replayed into the page as a test case, where a cut line would not parse.
	void Send(std::string_view a_type, const nlohmann::json& a_data = nlohmann::json::object(),
		bool a_logWhole = false);

	// One argument the page passed to send(), handed over by a backend on the
	// game thread. a_argCount is how many arguments the page passed (only the
	// first is read). a_mayBeWrapped: the backend delivers a JS string QUOTED
	// (Meridian does), so a message sent as JSON.stringify(msg) arrives as a
	// JSON string and is decoded a second time. On a backend that delivers the
	// argument raw (Prisma), the one decode is the only one.
	void Receive(const char* a_arg, int a_argCount, bool a_mayBeWrapped);
}
