#pragma once
#include <cstdint>

/*
 * Per-game pins for Mortal Shell II.
 *
 * Target: MortalShell2-Win64-Shipping.exe, UE 5.6.1, build
 * 5.6.1-0+++Sparta-Depot+Day1+CL92086, exe SHA-256
 * BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69.
 *
 * Both offsets were READ OFF a successful Dumper-7 v7.1.0 run on this exact executable
 * (research\runs\20261022-175220-dumper7-menu\dumper7.log):
 *
 *     Found FChunkedFixedUObjectArray GObjects at offset 0xb03aef0
 *     GWorld-Offset: 0xAD4E7F0
 *
 * They are fallbacks only: the scan runs first and the pins are used when it fails, or
 * (for GWorld) when the pin validates against the live object array.
 */
namespace D7GamePins
{
	constexpr std::int32_t GObjectsOffset = 0xb03aef0;
	constexpr std::int32_t GWorldOffset   = 0xAD4E7F0;

	/*
	 * InitGWorld() scans the whole address space once per UWorld object it finds. In gameplay
	 * several UWorld objects exist (the log showed "Detected 4 GWorld") and every such scan
	 * costs minutes, which looked like a hang. Stop after this many expensive scans.
	 */
	constexpr int MaxGWorldScans = 3;

	/*
	 * The GObjects text dumps (292 MB + 318 MB in a fully loaded session) are written before the
	 * SDK and are redundant with the UE4SS object dumps, so a headless run skips them.
	 */
	constexpr bool bSkipGObjectsDumpFiles = true;

	/*
	 * MappingGenerator / IDAMappingGenerator / DumpspaceGenerator iterate every object in
	 * GUObjectArray after CppSDK has already been written; with ~2.1M objects that took longer
	 * than the whole CppSDK generation and looked like a hang. We only consume CppSDK.
	 */
	constexpr bool bSkipExtraGenerators = true;
}
