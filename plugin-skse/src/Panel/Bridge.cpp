// Bridge.cpp
// Lifecast

#include "Panel/Bridge.h"

#include "Panel/Panel.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace Lifecast::Panel::Bridge
{
	namespace
	{
		using json = nlohmann::json;

		// A slider dragged across the panel sends many messages a second.
		// The whole text is logged up to this length and cut after it, with
		// the full length stated, so a long message is visible as long
		// rather than silently shortened.
		//
		// 400 cut the "type" off every long message, and that is not obvious
		// from reading the cap: nlohmann sorts an object's keys, so the dump
		// is {"data":...,"type":...,"v":...} and the only field that says WHAT
		// a message is sits AFTER the big one. Nine payload sizes in the boot
		// 93 and 94 logs ran 406 to 2695 - presets, preset folder, controls,
		// nodes, paint areas and layers - and all nine lost it.
		//
		// 4096 uncovers all nine for about 68 KB a boot, measured by summing
		// min(size, 4096) - 400 over the 41 clipped lines of boot 93. It does
		// NOT add a byte to the drag traffic the note above is about: a
		// stage-move carries five numbers in roughly 100 characters and never
		// reached 400, so the cap never applied to it. The cap only ever bit
		// the catalogues, which go once per panel opening.
		//
		// One payload stays clipped on purpose: the RaceMenu slider catalogue
		// at 31530 chars, five times in boot 93. Raising the cap to cover it
		// would cost another 152 KB a boot for one message; a_logWhole at its
		// own send site is the lever if anyone needs it whole.
		constexpr std::size_t kLogCap = 4096;

		std::string Clip(std::string_view a_text)
		{
			if (a_text.size() <= kLogCap) {
				return std::string(a_text);
			}
			return fmt::format("{}... [{} chars total]", a_text.substr(0, kLogCap), a_text.size());
		}

		// A message sent while the page is still loading may never reach its
		// listener (Meridian's guide says so for ExecEventFunction; on Prisma
		// the receiving function does not exist before DOM ready). Such messages are
		// held here, in order, and delivered on the game thread once the
		// page has loaded. Once one is held, every later one is held behind
		// it, so the page never sees them out of order.
		using Clock = std::chrono::steady_clock;
		constexpr auto kLoadPoll = std::chrono::milliseconds(50);
		constexpr auto kLoadDeadline = std::chrono::seconds(30);

		struct Held
		{
			std::string       type;
			std::string       text;
			Clock::time_point since;
		};

		std::vector<Held>          g_held;
		std::atomic_bool           g_watching = false;
		std::atomic<std::uint32_t> g_watch = 0;  // which watcher is the live one

		// Game thread. a_giveUp: the deadline passed - the page never loaded,
		// and what is held is dropped, said in the log, not kept forever.
		void Release(std::uint32_t a_watch, bool a_giveUp)
		{
			if (a_watch != g_watch) {
				return;  // asked by a watcher that has been replaced
			}
			if (g_held.empty()) {
				g_watching = false;
				return;
			}
			const bool exists = Panel::Exists();
			const bool loaded = exists && Panel::PageReady();
			if (!loaded && !a_giveUp) {
				return;
			}
			g_watching = false;
			const auto ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_held.front().since).count();
			std::string types;
			for (const auto& h : g_held) {
				types += (types.empty() ? "" : ", ") + h.type;
			}
			if (!loaded) {
				spdlog::warn("[bridge] dll->page DROPPED {} held message{} after {} ms: {} ({})", g_held.size(),
					g_held.size() == 1 ? "" : "s", ms, exists ? "the page never loaded" : "no page", types);
				g_held.clear();
				return;
			}
			spdlog::info("[bridge] the page loaded: delivering {} held message{}, the oldest held {} ms: {}",
				g_held.size(), g_held.size() == 1 ? "" : "s", ms, types);
			auto held = std::move(g_held);
			g_held.clear();
			for (const auto& h : held) {
				Panel::Post(h.text);
			}
		}

		// Off the game thread: only waits, and asks the game thread to look.
		// No backend call is made here.
		void Watch()
		{
			const auto watch = ++g_watch;
			std::thread([watch]() {
				const auto deadline = Clock::now() + kLoadDeadline;
				while (g_watching && g_watch == watch) {
					std::this_thread::sleep_for(kLoadPoll);
					const bool giveUp = Clock::now() >= deadline;
					if (auto* tasks = SKSE::GetTaskInterface()) {
						tasks->AddTask([watch, giveUp]() { Release(watch, giveUp); });
					}
					if (giveUp) {
						return;
					}
				}
			}).detach();
		}

		std::unordered_map<std::string, Handler>& Handlers()
		{
			static std::unordered_map<std::string, Handler> s_handlers;
			return s_handlers;
		}

		// Meridian hands every argument over as a JSON document of its own:
		// a JS object arrives as its JSON text, and a JS string arrives
		// QUOTED. A page that calls send(JSON.stringify(msg)) instead of
		// send(msg) therefore delivers a JSON string whose content is the
		// real message. Both are accepted, and the log says which one
		// arrived - the difference is exactly what a bridge defect would
		// look like. Prisma delivers the argument raw: one decode, never two.
		json Decode(const char* a_arg, bool a_mayBeWrapped, bool& a_wasStringWrapped)
		{
			a_wasStringWrapped = false;

			auto outer = json::parse(a_arg, nullptr, false);
			if (outer.is_discarded()) {
				return outer;
			}

			if (a_mayBeWrapped && outer.is_string()) {
				a_wasStringWrapped = true;
				return json::parse(outer.get<std::string>(), nullptr, false);
			}

			return outer;
		}

		void Dispatch(const json& a_message)
		{
			if (!a_message.is_object() || !a_message.contains("type") || !a_message["type"].is_string()) {
				spdlog::warn("[bridge] page->dll: not a message (no string \"type\") - ignored");
				return;
			}

			const auto type = a_message["type"].get<std::string>();

			std::uint32_t version = 0;
			if (a_message.contains("v") && a_message["v"].is_number_unsigned()) {
				version = a_message["v"].get<std::uint32_t>();
			}

			static const json kEmpty = json::object();
			const json& data = a_message.contains("data") ? a_message["data"] : kEmpty;

			// "hello" is the one type that is understood across protocol
			// versions: it is how a mismatched page learns that it is
			// mismatched. Everything else requires the same version.
			if (type != "hello" && version != kProtocolVersion) {
				spdlog::warn(
					"[bridge] page->dll: type \"{}\" speaks protocol v{}, this DLL speaks v{} - ignored",
					type, version, kProtocolVersion);
				Send("error", { { "reason", "protocol" }, { "type", type }, { "expected", kProtocolVersion } });
				return;
			}

			const auto it = Handlers().find(type);
			if (it == Handlers().end()) {
				spdlog::warn("[bridge] page->dll: no handler for type \"{}\"", type);
				Send("error", { { "reason", "unknown-type" }, { "type", type } });
				return;
			}

			it->second(data);
		}
	}

	void On(std::string_view a_type, Handler a_handler)
	{
		Handlers()[std::string(a_type)] = a_handler;
	}

	void Send(std::string_view a_type, const nlohmann::json& a_data, bool a_logWhole)
	{
		const json message = {
			{ "v", kProtocolVersion },
			{ "type", a_type },
			{ "data", a_data },
		};
		const auto text = message.dump();

		// The type is named OUTSIDE the payload on purpose, and it is the half
		// of this fix that survives: a bigger cap stops the current messages
		// from losing their "type", but any payload that outgrows the cap
		// later would lose it again. Naming it here cannot be outgrown.
		if (!Panel::Exists()) {
			spdlog::warn("[bridge] dll->page \"{}\" DROPPED, no page: {}", a_type,
				a_logWhole ? text : Clip(text));
			return;
		}

		if (!g_held.empty() || !Panel::PageReady()) {
			spdlog::info("[bridge] dll->page \"{}\" HELD until the page loads: {}", a_type,
				a_logWhole ? text : Clip(text));
			g_held.push_back({ std::string(a_type), text, Clock::now() });
			if (!g_watching.exchange(true)) {
				Watch();
			}
			return;
		}

		spdlog::info("[bridge] dll->page \"{}\": {}", a_type, a_logWhole ? text : Clip(text));
		Panel::Post(text);
	}

	void Receive(const char* a_arg, int a_argCount, bool a_mayBeWrapped)
	{
		if (!a_arg) {
			spdlog::warn("[bridge] page->dll: send() called with no argument - expected 1");
			return;
		}

		spdlog::info("[bridge] page->dll raw ({} arg{}): {}",
			a_argCount, a_argCount == 1 ? "" : "s", Clip(a_arg));

		if (a_argCount > 1) {
			spdlog::warn("[bridge] page->dll: {} extra arguments ignored", a_argCount - 1);
		}

		bool wasStringWrapped = false;
		const auto message = Decode(a_arg, a_mayBeWrapped, wasStringWrapped);

		if (message.is_discarded()) {
			spdlog::warn("[bridge] page->dll: argument is not valid JSON - ignored");
			return;
		}

		if (wasStringWrapped) {
			spdlog::info("[bridge] page->dll: the message arrived as a JSON STRING and was decoded twice");
		}

		Dispatch(message);
	}
}
