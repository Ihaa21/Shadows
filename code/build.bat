@echo off

set CodeDir=..\code
set DataDir=..\data
set LibsDir=..\libs
set OutputDir=..\build_win32
set VulkanIncludeDir="C:\VulkanSDK\1.2.135.0\Include\vulkan"
set VulkanBinDir="C:\VulkanSDK\1.2.135.0\Bin"
set AssimpDir=%LibsDir%\framework_vulkan

set CommonCompilerFlags=-Od -MTd -nologo -fp:fast -fp:except- -EHsc -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4127 -wd4201 -wd4100 -wd4189 -wd4505 -Z7 -FC
set CommonCompilerFlags=-I %VulkanIncludeDir% %CommonCompilerFlags%
set CommonCompilerFlags=-I %LibsDir% -I %AssimpDir% %CommonCompilerFlags%
REM Check the DLLs here
set CommonLinkerFlags=-incremental:no -opt:ref user32.lib gdi32.lib Winmm.lib opengl32.lib DbgHelp.lib d3d12.lib dxgi.lib d3dcompiler.lib %AssimpDir%\assimp\libs\assimp-vc142-mt.lib

IF NOT EXIST %OutputDir% mkdir %OutputDir%

pushd %OutputDir%

del *.pdb > NUL 2> NUL

REM USING GLSL IN VK USING GLSLANGVALIDATOR
call glslangValidator -DSHADOW_VERTEX=1 -S vert -e main -g -V -o %DataDir%\shader_shadow_vert.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DSHADOW_VARIANCE_FRAGMENT=1 -S frag -e main -g -V -o %DataDir%\shader_shadow_variance_frag.spv %CodeDir%\shader_forward.cpp

call glslangValidator -DFORWARD_VERTEX=1 -DSTANDARD=1 -S vert -e main -g -V -o %DataDir%\shader_forward_standard_vert.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_FRAGMENT=1 -DSTANDARD=1 -S frag -e main -g -V -o %DataDir%\shader_forward_standard_frag.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_VERTEX=1 -DPCF=1 -S vert -e main -g -V -o %DataDir%\shader_forward_pcf_vert.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_FRAGMENT=1 -DPCF=1 -S frag -e main -g -V -o %DataDir%\shader_forward_pcf_frag.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_VERTEX=1 -DVARIANCE=1 -S vert -e main -g -V -o %DataDir%\shader_forward_variance_vert.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_FRAGMENT=1 -DVARIANCE=1 -S frag -e main -g -V -o %DataDir%\shader_forward_variance_frag.spv %CodeDir%\shader_forward.cpp

call glslangValidator -DGAUSSIAN_BLUR_X=1 -S frag -e main -g -V -o %DataDir%\shader_gaussian_x_frag.spv %CodeDir%\shader_gaussian_blur.cpp
call glslangValidator -DGAUSSIAN_BLUR_Y=1 -S frag -e main -g -V -o %DataDir%\shader_gaussian_y_frag.spv %CodeDir%\shader_gaussian_blur.cpp

call glslangValidator -DFRAGMENT_SHADER=1 -S frag -e main -g -V -o %DataDir%\shader_copy_to_swap_frag.spv %CodeDir%\shader_copy_to_swap.cpp

REM USING HLSL IN VK USING DXC
REM set DxcDir=C:\Tools\DirectXShaderCompiler\build\Debug\bin
REM %DxcDir%\dxc.exe -spirv -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Fo ..\data\write_cs.o -Fh ..\data\write_cs.o.txt ..\code\bw_write_shader.cpp

REM ASSIMP
copy %AssimpDir%\assimp\bin\assimp-vc142-mt.dll %OutputDir%\assimp-vc142-mt.dll

REM 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CodeDir%\shadow_demo.cpp -Fmshadow_demo.map -LD /link %CommonLinkerFlags% -incremental:no -opt:ref -PDB:shadow_demo_%random%.pdb -EXPORT:Init -EXPORT:Destroy -EXPORT:SwapChainChange -EXPORT:CodeReload -EXPORT:MainLoop
del lock.tmp
call cl %CommonCompilerFlags% -DDLL_NAME=shadow_demo -Feshadow_demo.exe %LibsDir%\framework_vulkan\win32_main.cpp -Fmshadow_demo.map /link %CommonLinkerFlags%

popd
