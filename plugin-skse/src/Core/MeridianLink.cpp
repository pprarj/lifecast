// MeridianLink.cpp
// Lifecast

#include "MeridianLink.h"

#include "MeridianUIAPI/SKSELoader.h"

namespace Lifecast::Core::MeridianLink
{
	namespace
	{
		namespace MUI = Meridian::UI;

		MUI::IUIPlatformAPI* g_platform = nullptr;
		MUI::View::IViewAPI* g_views = nullptr;
		MUI::NifScene::INifSceneAPI4* g_nifScene = nullptr;
		MUI::Input::IInputAPI* g_input = nullptr;
		MUI::RenderLayer::IRenderLayerAPI* g_renderLayer = nullptr;

		// The consumer name Meridian sees, and the mod:// host the panel is
		// addressed by. Data/MeridianUI/Lifecast/ is the folder, so
		// mod://Lifecast/index.html is the URL and ViewCreateInfo.ownerName
		// has to match that host - Meridian pins native bindings to the
		// initial trusted origin and rejects a mismatch.
		constexpr auto kConsumerName = "Lifecast";

		// One Settings for the whole plugin. It must outlive the
		// ProcessSKSEMessage call that dispatches it, and the extension
		// queries take it too. Defaults are the release-correct values -
		// never assign structSize, and never memset this after construction.
		MUI::Settings& PluginSettings()
		{
			static MUI::Settings s_settings{};
			return s_settings;
		}
	}

	void Init()
	{
		spdlog::info(
			"[meridian] compiled against MeridianUIAPI lib {} / API {}; "
			"asking for Meridian.View v{}, Meridian.NifScene v{}, "
			"Meridian.Input v{} and Meridian.RenderLayer v{}",
			MUI::LibVersion::AS_STRING,
			MUI::APIVersion::AS_STRING,
			MUI::View::INTERFACE_VERSION,
			MUI::NifScene::ACTOR_APPEARANCE_INTERFACE_VERSION,
			MUI::Input::INTERFACE_VERSION,
			MUI::RenderLayer::INTERFACE_VERSION);
	}

	void ListenToMeridian()
	{
		// Same two steps as SKSELoader::GetUIPlatformAPIWithVersionCheck, done
		// here so the result of the registration can be read. Before SKSE
		// 2.1.5 a named listener can only be registered for a plugin that has
		// already loaded, and the plugin list is built as each one loads; from
		// kPostLoad on every plugin is in it. Registering from the plugin's
		// load entry point fails there whenever this DLL loads before
		// MeridianUI.dll.
		MUI::SKSELoader::LoaderData::s_apiReadyCallback = [](MUI::IUIPlatformAPI* a_api) {
			g_platform = a_api;
			spdlog::info("[meridian] platform API ready: {}",
				static_cast<const void*>(a_api));
		};

		const bool registered = SKSE::GetMessagingInterface()->RegisterListener(
			MUI::LibVersion::PROJECT_NAME,
			[](SKSE::MessagingInterface::Message* a_msg) {
				MUI::SKSELoader::ProcessSKSEMessage(a_msg);
			});

		if (registered) {
			spdlog::info("[meridian] listening to MeridianUI from kPostLoad");
		} else {
			spdlog::error(
				"[meridian] listening to MeridianUI from kPostLoad failed - "
				"MeridianUI is not loaded; there will be no panel");
		}
	}

	void ForwardSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		MUI::SKSELoader::ProcessSKSEMessage(a_msg, &PluginSettings());
	}

	void QueryExtensions(const char* a_moment)
	{
		auto* settings = &PluginSettings();

		if (!g_views) {
			g_views = MUI::View::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.View v{} = {}",
				a_moment, MUI::View::INTERFACE_VERSION,
				static_cast<const void*>(g_views));
		}

		if (!g_nifScene) {
			// Query4 is the actor-appearance interface. Asking for it and
			// getting null does not mean the extension is absent: an older
			// Meridian can answer version 1, 2 or 3 and refuse 4. The
			// side-by-side comparison turns on that distinction, so the
			// failure is narrowed by asking for version 1 as well.
			g_nifScene = MUI::NifScene::Query4(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.NifScene v{} = {}",
				a_moment, MUI::NifScene::ACTOR_APPEARANCE_INTERFACE_VERSION,
				static_cast<const void*>(g_nifScene));

			if (!g_nifScene) {
				auto* base = MUI::NifScene::Query(settings, kConsumerName);
				spdlog::info(
					"[meridian] [{}] Meridian.NifScene v{} = {} - {}",
					a_moment, MUI::NifScene::INTERFACE_VERSION,
					static_cast<const void*>(base),
					base
						? "extension present but refuses the actor-appearance version"
						: "extension not answering at all");
			}
		}

		if (!g_input) {
			g_input = MUI::Input::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.Input v{} = {}",
				a_moment, MUI::Input::INTERFACE_VERSION,
				static_cast<const void*>(g_input));
		}

		if (!g_renderLayer) {
			g_renderLayer = MUI::RenderLayer::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.RenderLayer v{} = {}",
				a_moment, MUI::RenderLayer::INTERFACE_VERSION,
				static_cast<const void*>(g_renderLayer));
		}
	}

	MUI::IUIPlatformAPI* Platform()
	{
		return g_platform;
	}

	MUI::View::IViewAPI* Views()
	{
		return g_views;
	}

	MUI::NifScene::INifSceneAPI4* NifScene()
	{
		return g_nifScene;
	}

	MUI::Input::IInputAPI* Input()
	{
		return g_input;
	}

	MUI::RenderLayer::IRenderLayerAPI* RenderLayer()
	{
		return g_renderLayer;
	}
}
