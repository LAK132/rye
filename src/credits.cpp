#include "main.hpp"

#include <lak/imgui/widgets.hpp>

void credits()
{
	LAK_TREE_NODE("Credits")
	{
		LAK_TREE_NODE("lak")
		{
			ImGui::Text("https://github.com/LAK132/lak");
			ImGui::Text("SPDX-License-Identifier: MIT OR Unlicense");
		}
		LAK_TREE_NODE("Cobalt Renderer")
		{
			ImGui::Text("https://github.com/CobaltRenderer/Cobalt");
			ImGui::Text("SPDX-License-Identifier: MIT");
			LAK_TREE_NODE("Direct3D12")
			{
				LAK_TREE_NODE("D3DX12Residency")
				{
					ImGui::Text("https://github.com/microsoft/DirectX-Graphics-Samples");
					ImGui::Text("SPDX-License-Identifier: MIT");
				}
				LAK_TREE_NODE("DirectX-Headers")
				{
					ImGui::Text("https://github.com/microsoft/DirectX-Headers");
					ImGui::Text("SPDX-License-Identifier: MIT");
				}
				LAK_TREE_NODE("D3D12MemoryAllocator")
				{
					ImGui::Text(
					  "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator");
					ImGui::Text("SPDX-License-Identifier: MIT");
				}
				LAK_TREE_NODE("PixEvents")
				{
					ImGui::Text("https://github.com/microsoft/PixEvents");
					ImGui::Text("SPDX-License-Identifier: MIT");
				}
				LAK_TREE_NODE("DirectXShaderCompiler")
				{
					ImGui::Text("https://github.com/microsoft/DirectXShaderCompiler");
					ImGui::Text(
					  "https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT");
				}
			}
			LAK_TREE_NODE("OpenGL")
			{
				LAK_TREE_NODE("OpenGL-Registry")
				{
					ImGui::Text("https://github.com/KhronosGroup/OpenGL-Registry");
				}
			}
			LAK_TREE_NODE("Vulkan")
			{
				LAK_TREE_NODE("Vulkan-Headers")
				{
					ImGui::Text("https://github.com/KhronosGroup/Vulkan-Headers");
					ImGui::Text("SPDX-License-Identifier: Apache-2.0 OR MIT");
				}
				LAK_TREE_NODE("Vulkan-Loader")
				{
					ImGui::Text("https://github.com/KhronosGroup/Vulkan-Loader");
					ImGui::Text("SPDX-License-Identifier: Apache-2.0");
				}
				LAK_TREE_NODE("VulkanMemoryAllocator")
				{
					ImGui::Text(
					  "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator");
					ImGui::Text("SPDX-License-Identifier: MIT");
				}
				LAK_TREE_NODE("Vulkan SDK")
				{
					ImGui::Text("https://vulkan.lunarg.com/sdk/home");
					ImGui::Text("SPDX-License-Identifier: Apache-2.0 OR MIT");
				}
				LAK_TREE_NODE("MoltenVK")
				{
					ImGui::Text("https://github.com/KhronosGroup/MoltenVK");
					ImGui::Text("SPDX-License-Identifier: Apache-2.0");
				}
				LAK_TREE_NODE("Vulkan-ValidationLayers")
				{
					ImGui::Text(
					  "https://github.com/KhronosGroup/Vulkan-ValidationLayers");
					ImGui::Text("SPDX-License-Identifier: Apache-2.0");
				}
			}
			LAK_TREE_NODE("gli")
			{
				ImGui::Text("https://github.com/g-truc/gli");
				ImGui::Text("SPDX-License-Identifier: MIT");
			}
			LAK_TREE_NODE("glm")
			{
				ImGui::Text("https://github.com/g-truc/glm");
				ImGui::Text("SPDX-License-Identifier: MIT");
			}
			LAK_TREE_NODE("glslang")
			{
				ImGui::Text("https://github.com/KhronosGroup/glslang");
				ImGui::Text(
				  "https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt");
			}
			LAK_TREE_NODE("half")
			{
				ImGui::Text("https://sourceforge.net/projects/half");
				ImGui::Text("SPDX-License-Identifier: MIT");
			}
			LAK_TREE_NODE("libpng")
			{
				ImGui::Text("https://github.com/pnggroup/libpng");
				ImGui::Text("SPDX-License-Identifier: libpng-2.0");
			}
			LAK_TREE_NODE("SPIRV-Cross")
			{
				ImGui::Text("https://github.com/KhronosGroup/SPIRV-Cross");
				ImGui::Text("SPDX-License-Identifier: Apache-2.0");
			}
			LAK_TREE_NODE("SPIRV-Headers")
			{
				ImGui::Text("https://github.com/KhronosGroup/SPIRV-Headers");
				ImGui::Text(
				  "https://github.com/KhronosGroup/SPIRV-Headers/blob/main/LICENSE");
			}
			LAK_TREE_NODE("SPIRV-Tools")
			{
				ImGui::Text("https://github.com/KhronosGroup/SPIRV-Tools");
				ImGui::Text("SPDX-License-Identifier: Apache-2.0");
			}
			LAK_TREE_NODE("zlib")
			{
				ImGui::Text("https://github.com/madler/zlib");
				ImGui::Text("SPDX-License-Identifier: Zlib");
			}
		}
		LAK_TREE_NODE("ImGui")
		{
			ImGui::Text("https://github.com/ocornut/imgui");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
		LAK_TREE_NODE("ImFileDialog")
		{
			ImGui::Text("https://github.com/dfranx/ImFileDialog");
			ImGui::Text("https://github.com/LAK132/ImFileDialog");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
		LAK_TREE_NODE("imgui-node-editor")
		{
			ImGui::Text("https://github.com/thedmd/imgui-node-editor");
			ImGui::Text("https://github.com/LAK132/imgui-node-editor");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
		LAK_TREE_NODE("ImPlot")
		{
			ImGui::Text("https://github.com/epezent/implot");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
		LAK_TREE_NODE("ImPlot3D")
		{
			ImGui::Text("https://github.com/brenocq/implot3d");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
#ifdef LAK_USE_SDL
		LAK_TREE_NODE("SDL2") { ImGui::Text("https://www.libsdl.org/"); }
#endif
		LAK_TREE_NODE("LibRaw")
		{
			ImGui::Text("https://github.com/LibRaw/LibRaw");
			ImGui::Text("SPDX-License-Identifier: CDDL-1.0 OR LGPL-2.1-only");
			LAK_TREE_NODE("dcraw.c")
			{
				ImGui::Text("(LibRaw do not use RESTRICTED code from dcraw.c)");
				ImGui::Text("https://dechifro.org/dcraw/");
				ImGui::Text(R"(dcraw.c -- Dave Coffin's raw photo decoder
Copyright 1997-2018 by Dave Coffin, dcoffin a cybercom o net

This is a command-line ANSI C program to convert raw photos from
any digital camera on any computer running any operating system.

No license is required to download and use dcraw.c.  However,
to lawfully redistribute dcraw, you must either (a) offer, at
no extra charge, full source code* for all executable files
containing RESTRICTED functions, (b) distribute this code under
the GPL Version 2 or later, (c) remove all RESTRICTED functions,
re-implement them, or copy them from an earlier, unrestricted
Revision of dcraw.c, or (d) purchase a license from the author.

The functions that process Foveon images have been RESTRICTED
since Revision 1.237.  All other code remains free for all uses.

*If you have not modified dcraw.c in any way, a link to my
homepage qualifies as "full source code".
)");
			}
			LAK_TREE_NODE("DBC demosaic and FBDD denoise")
			{
				ImGui::Text("SPDX-License-Identifier: BSD-3-Clause");
			}
			LAK_TREE_NODE("X3F tools")
			{
				ImGui::Text("https://github.com/Kalpanika/x3f");
				ImGui::Text("SPDX-License-Identifier: BSD-3-Clause");
			}
			LAK_TREE_NODE("Adobe DNG SDK")
			{
				ImGui::Text("SPDX-License-Identifier: MIT");
			}
		}
		LAK_TREE_NODE("stb")
		{
			ImGui::Text("https://github.com/nothings/stb");
			ImGui::Text("SPDX-License-Identifier: MIT OR Unlicense");
		}
		LAK_TREE_NODE("glm")
		{
			ImGui::Text("https://github.com/g-truc/glm");
			ImGui::Text("SPDX-License-Identifier: MIT");
		}
	}
}
