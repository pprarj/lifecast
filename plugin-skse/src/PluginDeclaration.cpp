// PluginDeclaration - the SKSE version data this DLL exports (D328).
//
// WHY THIS IS HAND-WRITTEN. add_commonlibsse_plugin generates this declaration
// and hardcodes StructCompatibility::Independent, which leaves the Address
// Library format 5 bit of the loader's versionIndependenceEx field clear.
// SKSE 2.3.1 (runtime 1.7.x) refuses a plugin that resolves addresses through
// the Address Library without that bit ("must be recompiled for new address
// library"). Declaring it here is the only place the value can be expressed,
// so the generated translation unit is dropped from the target in
// CMakeLists.txt - two definitions of SKSEPlugin_Version would not link.
//
// WHAT SKSE READS. The exported PluginDeclaration is read by the loader as a
// PluginVersionData; StructCompatibility sits where versionIndependenceEx
// lives (SKSE/Interfaces.h, PluginVersionData). That field is a bit field:
//
//     bit 0  kVersionIndependentEx_NoStructUse      == StructCompatibility::Independent
//     bit 1  kVersionIndependentEx_AddressLibraryV5
//
// CommonLib v6.7.1's StructCompatibility enum has no name for bit 1, so the
// value is composed from the named constants of both sides, never a literal.
//
// SKSE 2.2.6 (runtime 1.6.1170) predates format 5 and ignores a bit it does
// not know, so one DLL covers both runtimes. The bit does not pin a runtime;
// RuntimeCompatibility does, and this one lists none: every address is
// resolved through the Address Library.
//
// Without the bit the DLL loaded on 1.7.104 only because the PE timestamp
// that /Brepro derives from the link inputs happened to fall outside the
// window the loader rejects; any rebuild could land inside it.

#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

#include "Version.h"

namespace
{
	inline constexpr auto kStructCompatibility = static_cast<SKSE::StructCompatibility>(
		static_cast<std::uint32_t>(SKSE::StructCompatibility::Independent) |
		static_cast<std::uint32_t>(SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5));
}

// The version comes from Version.h, so project(... VERSION ...) in
// CMakeLists.txt stays the single source of truth.
SKSEPluginInfo(
	.Version = REL::Version{
		Lifecast::Version::kMajor,
		Lifecast::Version::kMinor,
		Lifecast::Version::kPatch,
		0 },
	.Name = Lifecast::Version::kProjectName,
	.Author = "Awrawra"sv,
	.SupportEmail = ""sv,
	.StructCompatibility = kStructCompatibility,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
	.MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 })
